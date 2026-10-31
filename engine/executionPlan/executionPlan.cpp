#include "./executionPlan.h"

#include <algorithm>
#include <cstddef>
#include <set>
#include <string>
#include <utility>

#include "../stubProcessor/stubProcessor.h"

namespace {
  /**
   * Модули главного графа, от которых сигнал доходит до выхода
   */
  std::set<ModuleId> findReachable(const SynthModel& model) {
    std::vector<ModuleId> pending = { model.getOutputId() };
    std::set<ModuleId> reachable;

    while (!pending.empty()) {
      const ModuleId current = pending.back();
      pending.pop_back();

      if (!reachable.insert(current).second) {
        continue;
      }

      for (const Connection& connection : model.getConnections()) {
        if (connection.toModule == current) {
          pending.push_back(connection.fromModule);
        }
      }
    }

    return reachable;
  }

  /**
   * Порядок исполнения: модуль идёт после всех своих источников. Среди готовых к исполнению
   * выбирается меньший ID, поэтому порядок всегда один и тот же. Циклов нет (D-007)
   */
  std::vector<ModuleId> sortModules(const SynthModel& model, const std::set<ModuleId>& reachable) {
    std::map<ModuleId, size_t> pendingSources;

    for (const ModuleId id : reachable) {
      pendingSources[id] = 0;
    }

    for (const Connection& connection : model.getConnections()) {
      if (reachable.contains(connection.toModule)) {
        pendingSources[connection.toModule]++;
      }
    }

    std::set<ModuleId> ready;

    for (const auto& [id, count] : pendingSources) {
      if (count == 0) {
        ready.insert(id);
      }
    }

    std::vector<ModuleId> order;

    while (!ready.empty()) {
      const ModuleId current = *ready.begin();
      ready.erase(ready.begin());
      order.push_back(current);

      for (const Connection& connection : model.getConnections()) {
        if (connection.fromModule == current && reachable.contains(connection.toModule)) {
          if (--pendingSources[connection.toModule] == 0) {
            ready.insert(connection.toModule);
          }
        }
      }
    }

    return order;
  }

  template <typename T>
  size_t findPortIndex(const std::vector<T>& ports, const std::string& key) {
    const auto it = std::find_if(ports.begin(), ports.end(), [&key](const T& port) {
      return port.key == key;
    });

    return (size_t)(it - ports.begin());
  }

  size_t countBuffers(const SynthModel& model, const std::set<ModuleId>& reachable) {
    // Буфер тишины, по буферу на каждый выход и на каждый вход на случай нескольких проводов
    size_t count = 1;

    for (const ModuleId id : reachable) {
      const ModuleDescriptor* descriptor = model.findModule(id)->descriptor;

      count += descriptor->inputs.size() + descriptor->outputs.size();
    }

    return count;
  }
}

void syncModuleSlots(const SynthModel& model, ModuleSlots& slots, float sampleRate) {
  // До этапа 3 исполняется только главный граф, а состояния голосов там будут свои
  std::erase_if(slots, [&model](const auto& slot) {
    return model.findModule(slot.first) == nullptr;
  });

  for (const auto& [id, module] : model.getModules()) {
    if (module.owner || module.descriptor->role == MODULE_ROLE::OUTPUT) {
      continue;
    }

    ModuleSlot& slot = slots[id];

    if (!slot.processor) {
      slot.processor = module.descriptor->createProcessor ? module.descriptor->createProcessor() : createStubProcessor();
      slot.processor->prepare(sampleRate);
    }

    slot.parameters.clear();

    for (const ParameterDescriptor& parameter : module.descriptor->parameters) {
      const float value = module.parameters.at(parameter.key);

      slot.parameters.push_back({ value, value });
    }
  }
}

std::unique_ptr<ExecutionPlan> compilePlan(const SynthModel& model, ModuleSlots& slots) {
  const std::set<ModuleId> reachable = findReachable(model);

  auto plan = std::make_unique<ExecutionPlan>();

  // Размер задан сразу: шаги хранят указатели на буферы, вектор больше не перераспределяется
  plan->buffers.assign(countBuffers(model, reachable), AudioBuffer{});

  AudioBuffer* const silence = &plan->buffers[0];
  size_t nextBuffer = 1;

  // Выходы всех модулей раздаются заранее, чтобы входы могли на них сослаться
  std::map<ModuleId, std::vector<AudioBuffer*>> outputs;

  for (const ModuleId id : reachable) {
    for (size_t port = 0; port < model.findModule(id)->descriptor->outputs.size(); port++) {
      outputs[id].push_back(&plan->buffers[nextBuffer++]);
    }
  }

  for (const ModuleId id : sortModules(model, reachable)) {
    const ModuleInstance* module = model.findModule(id);
    const ModuleDescriptor* descriptor = module->descriptor;
    const bool isOutput = id == model.getOutputId();
    ModuleProcessor* const processor = isOutput ? nullptr : slots.at(id).processor.get();

    PlanStep step = {
      .processor = module->enabled ? processor : &plan->bypass,
      .parameters = isOutput ? nullptr : slots.at(id).parameters.data(),
      .mixes = {},
      .inputs = {},
      .outputs = outputs[id],
    };

    std::vector<std::vector<const AudioBuffer*>> sources(descriptor->inputs.size());

    for (const Connection& connection : model.getConnections()) {
      if (connection.toModule == id) {
        const ModuleDescriptor* from = model.findModule(connection.fromModule)->descriptor;

        sources[findPortIndex(descriptor->inputs, connection.toPort)].push_back(
          outputs[connection.fromModule][findPortIndex(from->outputs, connection.fromPort)]
        );
      }
    }

    for (std::vector<const AudioBuffer*>& inputSources : sources) {
      if (inputSources.empty()) {
        step.inputs.push_back(silence);
      } else if (inputSources.size() == 1) {
        step.inputs.push_back(inputSources[0]);
      } else {
        AudioBuffer* mix = &plan->buffers[nextBuffer++];

        step.inputs.push_back(mix);
        step.mixes.push_back({ mix, std::move(inputSources) });
      }
    }

    if (isOutput) {
      plan->output = step.inputs[0];
    }

    plan->steps.push_back(std::move(step));
  }

  return plan;
}
