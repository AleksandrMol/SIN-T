#include "./executionPlan.h"

#include <algorithm>
#include <cstddef>
#include <set>
#include <string>
#include <utility>

#include "../generator/generatorProcessor.h"
#include "../modulationCompiler/modulationCompiler.h"
#include "../stubProcessor/stubProcessor.h"

namespace {
  /**
   * Модули графа, от которых сигнал доходит до выхода
   */
  std::set<ModuleId> findReachable(const SynthModel& model, ModuleId outputId) {
    std::vector<ModuleId> pending = { outputId };
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

  ModuleId findVoiceOutput(const SynthModel& model, ModuleId generator) {
    for (const auto& [id, module] : model.getModules()) {
      if (module.owner == generator && module.descriptor->role == MODULE_ROLE::OUTPUT) {
        return id;
      }
    }

    return 0;
  }

  void mixInputs(const PlanMix& mix, uint32_t frames) {
    std::fill_n(mix.target->left.begin(), frames, 0.0f);
    std::fill_n(mix.target->right.begin(), frames, 0.0f);

    for (const AudioBuffer* source : mix.sources) {
      for (uint32_t frame = 0; frame < frames; frame++) {
        mix.target->left[frame] += source->left[frame];
        mix.target->right[frame] += source->right[frame];
      }
    }
  }

  void createProcessor(const ModuleInstance& module, ModuleSlot& slot) {
    slot.descriptor = module.descriptor;
    slot.generator = nullptr;

    if (module.descriptor->role == MODULE_ROLE::GENERATOR) {
      auto generator = std::make_unique<GeneratorProcessor>();

      slot.generator = generator.get();
      slot.processor = std::move(generator);
    } else {
      slot.processor = module.descriptor->createProcessor ? module.descriptor->createProcessor() : createStubProcessor();
    }
  }

  bool isSharedModulator(const ModuleInstance& module) {
    return !module.owner && module.descriptor->isVoiceModulator;
  }

  /**
   * Модули графа без его выхода. Генератору к подграфу добавляются общие модуляторы, от которых
   * зависят его модули, напрямую или через другие общие модуляторы: им нужно состояние на голос (D-005)
   */
  std::set<ModuleId> findMembers(const SynthModel& model, std::optional<ModuleId> owner) {
    std::set<ModuleId> members;

    for (const auto& [id, module] : model.getModules()) {
      if (module.owner == owner && module.descriptor->role != MODULE_ROLE::OUTPUT) {
        members.insert(id);
      }
    }

    std::vector<ModuleId> pending(members.begin(), members.end());

    while (!pending.empty()) {
      const ModuleId current = pending.back();
      pending.pop_back();

      for (const ModRoute& route : model.getRoutes()) {
        if (route.target.module == current
          && isSharedModulator(*model.findModule(route.source.module))
          && members.insert(route.source.module).second) {
          pending.push_back(route.source.module);
        }
      }
    }

    return members;
  }

  void syncGraph(const SynthModel& model, const std::set<ModuleId>& members, ModuleSlots& slots, float sampleRate) {
    std::erase_if(slots, [&members](const auto& slot) {
      return !members.contains(slot.first);
    });

    for (const ModuleId id : members) {
      const ModuleInstance& module = *model.findModule(id);
      ModuleSlot& slot = slots[id];

      if (slot.descriptor != module.descriptor) {
        createProcessor(module, slot);
        slot.processor->prepare(sampleRate);
      }

      slot.parameters.clear();

      for (const ParameterDescriptor& parameter : module.descriptor->parameters) {
        const float value = module.parameters.at(parameter.key);

        slot.parameters.push_back({ value, value });
      }

      slot.modOutputs.resize(module.descriptor->modOutputs.size());

      if (slot.generator) {
        slot.generator->setPolyphony((uint32_t)module.parameters.at("polyphony"));

        const std::set<ModuleId> voiceMembers = findMembers(model, id);

        for (ModuleSlots& voice : slot.generator->getVoiceSlots()) {
          syncGraph(model, voiceMembers, voice, sampleRate);
        }

        std::map<ModuleId, std::vector<float>>& lastOutputs = slot.generator->getLastOutputs();

        lastOutputs.clear();
        lastOutputs[id].resize(module.descriptor->modOutputs.size());

        for (const ModuleId member : voiceMembers) {
          lastOutputs[member].resize(model.findModule(member)->descriptor->modOutputs.size());
        }
      }
    }
  }

  /**
   * Собирает план одного графа: voices — состояния модулей по голосам. Генератор собирает свой подграф тем же кодом
   */
  void compileGraph(
    const SynthModel& model,
    ModuleId outputId,
    const std::vector<ModuleSlots*>& voices,
    GraphPlan& graph,
    ExecutionPlan& plan
  ) {
    const std::set<ModuleId> reachable = findReachable(model, outputId);

    // Размер задан сразу: шаги хранят указатели на буферы, вектор больше не перераспределяется
    graph.buffers.assign(countBuffers(model, reachable), AudioBuffer{});
    graph.voices.resize(voices.size());

    AudioBuffer* const silence = &graph.buffers[0];
    size_t nextBuffer = 1;

    // Выходы всех модулей раздаются заранее, чтобы входы могли на них сослаться
    std::map<ModuleId, std::vector<AudioBuffer*>> outputs;

    for (const ModuleId id : reachable) {
      for (size_t port = 0; port < model.findModule(id)->descriptor->outputs.size(); port++) {
        outputs[id].push_back(&graph.buffers[nextBuffer++]);
      }
    }

    for (const ModuleId id : sortModules(model, reachable)) {
      const ModuleInstance* module = model.findModule(id);
      const ModuleDescriptor* descriptor = module->descriptor;
      const bool isOutput = id == outputId;

      PlanStep step = { .mixes = {}, .inputs = {}, .outputs = outputs[id], .subgraph = nullptr };

      graph.modules.push_back(id);

      for (size_t voice = 0; voice < voices.size(); voice++) {
        ModuleSlot* slot = isOutput ? nullptr : &voices[voice]->at(id);
        ModuleProcessor* processor = slot ? slot->processor.get() : nullptr;

        graph.voices[voice].push_back({
          .processor = module->enabled ? processor : &graph.bypass,
          .parameters = slot ? slot->parameters.data() : nullptr,
        });
      }

      if (descriptor->role == MODULE_ROLE::GENERATOR && module->enabled) {
        GeneratorProcessor* generator = voices[0]->at(id).generator;
        std::vector<ModuleSlots*> generatorVoices;

        for (ModuleSlots& voice : generator->getVoiceSlots()) {
          generatorVoices.push_back(&voice);
        }

        auto subgraph = std::make_unique<GraphPlan>();

        compileGraph(model, findVoiceOutput(model, id), generatorVoices, *subgraph, plan);

        step.subgraph = subgraph.get();
        plan.generators.push_back({ id, generator, subgraph.get(), voices[0]->at(id).parameters.data() });
        plan.subgraphs.push_back(std::move(subgraph));
      }

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
          AudioBuffer* mix = &graph.buffers[nextBuffer++];

          step.inputs.push_back(mix);
          step.mixes.push_back({ mix, std::move(inputSources) });
        }
      }

      if (isOutput) {
        graph.output = step.inputs[0];
      }

      graph.steps.push_back(std::move(step));
    }
  }
}

void syncModuleSlots(const SynthModel& model, ModuleSlots& slots, float sampleRate) {
  syncGraph(model, findMembers(model, std::nullopt), slots, sampleRate);
}

std::unique_ptr<ExecutionPlan> compilePlan(const SynthModel& model, ModuleSlots& slots, ParameterBases& bases) {
  auto plan = std::make_unique<ExecutionPlan>();

  compileGraph(model, model.getOutputId(), { &slots }, plan->main, *plan);
  compileModulation(model, slots, bases, *plan);

  return plan;
}

void runGraph(const GraphPlan& graph, uint32_t voice, const ChunkSpan& span, const VoiceContext& context) {
  const std::vector<StepBinding>& bindings = graph.voices[voice];

  for (size_t index = 0; index < graph.steps.size(); index++) {
    const PlanStep& step = graph.steps[index];
    const StepBinding& binding = bindings[index];

    for (const PlanMix& mix : step.mixes) {
      mixInputs(mix, span.frames);
    }

    if (binding.processor) {
      binding.processor->process({
        .frames = span.frames,
        .rampOffset = span.rampOffset,
        .rampFrames = span.rampFrames,
        .inputCount = (uint32_t)step.inputs.size(),
        .outputCount = (uint32_t)step.outputs.size(),
        .inputs = step.inputs.data(),
        .outputs = step.outputs.data(),
        .parameters = binding.parameters,
        .voice = &context,
        .subgraph = step.subgraph,
      });
    }
  }
}
