#include "./executionPlan.h"

#include <algorithm>
#include <cstddef>
#include <set>
#include <string>
#include <utility>

#include "../generator/generatorProcessor.h"
#include "../modulationCompiler/modulationCompiler.h"
#include "../planExchange/planExchange.h"
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

  void mixInputs(const PlanMix& mix, const ChunkSpan& span) {
    std::fill_n(mix.target->left.begin(), span.frames, 0.0f);
    std::fill_n(mix.target->right.begin(), span.frames, 0.0f);

    for (const PlanMixSource& source : mix.sources) {
      for (uint32_t frame = 0; frame < span.frames; frame++) {
        const float gain = source.gain ? source.gain->at(span.gridOffset + frame, MAX_CHUNK_FRAMES) : 1.0f;

        mix.target->left[frame] += source.buffer->left[frame] * gain;
        mix.target->right[frame] += source.buffer->right[frame] * gain;
      }
    }
  }

  /**
   * Смешивает выходы модуля с тем, что он отдавал бы отключённым: суммой входов (D-040).
   * Входы и выходы — разные буферы, поэтому сумма считается прямо из входов
   */
  void blendBypass(const PlanStep& step, const ChunkSpan& span) {
    for (AudioBuffer* output : step.outputs) {
      for (uint32_t frame = 0; frame < span.frames; frame++) {
        const float wet = step.wet->at(span.gridOffset + frame, MAX_CHUNK_FRAMES);
        float left = 0.0f;
        float right = 0.0f;

        for (const AudioBuffer* input : step.inputs) {
          left += input->left[frame];
          right += input->right[frame];
        }

        output->left[frame] = output->left[frame] * wet + left * (1.0f - wet);
        output->right[frame] = output->right[frame] * wet + right * (1.0f - wet);
      }
    }
  }

  float findRouteDepth(const SynthModel& model, const ModRoute& route) {
    return model.findModule(route.source.module)->enabled ? route.depth : 0.0f;
  }

  const ModRoute* findRoute(const SynthModel& model, const ModRoute& route) {
    const auto it = std::ranges::find_if(model.getRoutes(), [&route](const ModRoute& other) {
      return other.source == route.source && other.target == route.target;
    });

    return it != model.getRoutes().end() ? &*it : nullptr;
  }

  template <typename Key>
  void addFades(const std::map<Key, FadeRange>& ranges, std::map<Key, PlanFade>& fades) {
    for (const auto& [key, range] : ranges) {
      fades.emplace(key, PlanFade{ { range.from, range.from }, range.from, range.to });
    }
  }

  const ParameterRamp* findFadeRamp(const auto& fades, const auto& key) {
    const auto it = fades.find(key);

    return it != fades.end() ? &it->second.ramp : nullptr;
  }

  /**
   * Убирает из таблицы всё, чего нет в keep. Планы могут ссылаться на убранное, поэтому удалит его exchange
   */
  template <typename Table>
  void removeMissing(Table& table, const std::set<ModuleId>& keep, PlanExchange* exchange) {
    for (auto it = table.begin(); it != table.end();) {
      const auto current = it++;

      if (keep.contains(current->first)) {
        continue;
      }

      if (exchange) {
        exchange->bury(table.extract(current));
      } else {
        table.erase(current);
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

  /**
   * Настройки одного прохода синхронизации
   */
  struct SlotSync {
    float sampleRate;
    PlanExchange* exchange;
    SLOT_VALUES values;
  };

  /**
   * Значения для чужих зон. Аудиопоток пишет их по адресам из плана, поэтому записи оставшихся модулей не трогаются
   */
  void syncLastOutputs(
    const SynthModel& model,
    const ModuleInstance& generator,
    const std::set<ModuleId>& voiceMembers,
    GeneratorProcessor& processor,
    const SlotSync& sync
  ) {
    std::map<ModuleId, std::vector<float>>& lastOutputs = processor.getLastOutputs();
    std::set<ModuleId> keep = voiceMembers;

    keep.insert(generator.id);

    removeMissing(lastOutputs, keep, sync.exchange);

    for (const ModuleId member : keep) {
      if (!lastOutputs.contains(member)) {
        lastOutputs[member].resize(model.findModule(member)->descriptor->modOutputs.size());
      }
    }
  }

  void syncGraph(const SynthModel& model, const std::set<ModuleId>& members, ModuleSlots& slots, const SlotSync& sync) {
    removeMissing(slots, members, sync.exchange);

    for (const ModuleId id : members) {
      const ModuleInstance& module = *model.findModule(id);
      ModuleSlot& slot = slots[id];
      const bool isNew = slot.descriptor != module.descriptor;

      if (isNew) {
        createProcessor(module, slot);
        slot.processor->prepare(sync.sampleRate);

        // Огибающая, появившаяся посреди ноты, начинает атаку, а не молчит до следующей ноты
        if (sync.values == SLOT_VALUES::KEEP) {
          slot.processor->reset();
        }
      }

      if (isNew || sync.values == SLOT_VALUES::ASSIGN) {
        slot.parameters.clear();

        for (const ParameterDescriptor& parameter : module.descriptor->parameters) {
          const float value = module.parameters.at(parameter.key);

          slot.parameters.push_back({ value, value });
        }

        slot.modOutputs.resize(module.descriptor->modOutputs.size());
      }

      if (slot.generator) {
        slot.generator->reserveVoices((uint32_t)module.parameters.at("polyphony"));

        const std::set<ModuleId> voiceMembers = findMembers(model, id);

        for (ModuleSlots& voice : slot.generator->getVoiceSlots()) {
          syncGraph(model, voiceMembers, voice, sync);
        }

        syncLastOutputs(model, module, voiceMembers, *slot.generator, sync);
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
    graph.processors.resize(voices.size());

    for (size_t voice = 0; voice < voices.size(); voice++) {
      for (const auto& [id, slot] : *voices[voice]) {
        graph.processors[voice].push_back(slot.processor.get());
      }
    }

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

      PlanStep step = {
        .mixes = {},
        .inputs = {},
        .outputs = outputs[id],
        .subgraph = nullptr,
        .wet = findFadeRamp(plan.moduleFades, id),
      };

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
        plan.generators.push_back({
          .id = id,
          .processor = generator,
          .subgraph = subgraph.get(),
          .parameters = voices[0]->at(id).parameters.data(),
          .polyphony = (uint32_t)module->parameters.at("polyphony"),
        });
        plan.subgraphs.push_back(std::move(subgraph));
      }

      std::vector<std::vector<PlanMixSource>> sources(descriptor->inputs.size());

      for (const Connection& connection : model.getConnections()) {
        if (connection.toModule == id) {
          const ModuleDescriptor* from = model.findModule(connection.fromModule)->descriptor;

          sources[findPortIndex(descriptor->inputs, connection.toPort)].push_back({
            outputs[connection.fromModule][findPortIndex(from->outputs, connection.fromPort)],
            findFadeRamp(plan.wireFades, connection),
          });
        }
      }

      for (std::vector<PlanMixSource>& inputSources : sources) {
        if (inputSources.empty()) {
          step.inputs.push_back(silence);
        } else if (inputSources.size() == 1 && !inputSources[0].gain) {
          step.inputs.push_back(inputSources[0].buffer);
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

void syncModuleSlots(
  const SynthModel& model,
  ModuleSlots& slots,
  float sampleRate,
  PlanExchange* exchange,
  SLOT_VALUES values
) {
  syncGraph(model, findMembers(model, std::nullopt), slots, { sampleRate, exchange, values });
}

TransitionFades findFades(const SynthModel& previous, const SynthModel& next) {
  TransitionFades fades;

  for (const Connection& connection : next.getConnections()) {
    if (!std::ranges::contains(previous.getConnections(), connection)) {
      fades.wires[connection] = { 0.0f, 1.0f };
    }
  }

  for (const Connection& connection : previous.getConnections()) {
    if (!std::ranges::contains(next.getConnections(), connection)) {
      fades.wires[connection] = { 1.0f, 0.0f };
    }
  }

  for (const auto& [id, module] : next.getModules()) {
    const ModuleInstance* before = previous.findModule(id);

    if (before && before->enabled != module.enabled) {
      fades.modules[id] = { before->enabled ? 1.0f : 0.0f, module.enabled ? 1.0f : 0.0f };
    }
  }

  // Новый и уходящий маршруты записываются всегда: их значение в плане идёт только через переход
  for (const ModRoute& route : next.getRoutes()) {
    const ModRoute* before = findRoute(previous, route);
    const float from = before ? findRouteDepth(previous, *before) : 0.0f;
    const float to = findRouteDepth(next, route);

    if (!before || from != to) {
      fades.routes[{ route.source, route.target }] = { from, to };
    }
  }

  for (const ModRoute& route : previous.getRoutes()) {
    if (!findRoute(next, route)) {
      fades.routes[{ route.source, route.target }] = { findRouteDepth(previous, route), 0.0f };
    }
  }

  return fades;
}

std::unique_ptr<ExecutionPlan> compilePlan(
  const SynthModel& model,
  ModuleSlots& slots,
  ParameterBases& bases,
  const TransitionFades& fades
) {
  auto plan = std::make_unique<ExecutionPlan>();

  addFades(fades.wires, plan->wireFades);
  addFades(fades.modules, plan->moduleFades);
  addFades(fades.routes, plan->routeFades);

  compileGraph(model, model.getOutputId(), { &slots }, plan->main, *plan);
  compileModulation(model, slots, bases, *plan);

  for (auto& [id, values] : bases) {
    plan->bases.emplace(id, values);
  }

  return plan;
}

void runGraph(const GraphPlan& graph, uint32_t voice, const ChunkSpan& span, const VoiceContext& context) {
  const std::vector<StepBinding>& bindings = graph.voices[voice];

  for (size_t index = 0; index < graph.steps.size(); index++) {
    const PlanStep& step = graph.steps[index];
    const StepBinding& binding = bindings[index];

    for (const PlanMix& mix : step.mixes) {
      mixInputs(mix, span);
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

    if (step.wet) {
      blendBypass(step, span);
    }
  }
}
