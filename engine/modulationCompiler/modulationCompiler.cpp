#include "./modulationCompiler.h"

#include <algorithm>
#include <compare>
#include <cstddef>
#include <set>
#include <string>
#include <utility>

#include "../generator/generatorProcessor.h"

namespace {
  // Зона экземпляра: главный граф или ID генератора. ID модулей начинаются с 1
  constexpr ModuleId MAIN_ZONE = 0;

  /**
   * Экземпляр модуля: у голосовых модулей состояние своё в каждой зоне
   */
  struct Instance {
    ModuleId zone;
    ModuleId module;

    auto operator<=>(const Instance&) const = default;
  };

  struct Zone {
    GraphPlan* graph;
    GeneratorProcessor* generator; // Пусто у главного графа
  };

  uint32_t findOutputIndex(const ModuleDescriptor& descriptor, const std::string& key) {
    const auto it = std::ranges::find_if(descriptor.modOutputs, [&key](const ModOutputDescriptor& output) {
      return output.key == key;
    });

    return (uint32_t)(it - descriptor.modOutputs.begin());
  }

  bool hasPath(const std::map<ModuleId, std::set<ModuleId>>& edges, ModuleId from, ModuleId to) {
    std::vector<ModuleId> pending = { from };
    std::set<ModuleId> visited;

    while (!pending.empty()) {
      const ModuleId current = pending.back();
      pending.pop_back();

      if (current == to) {
        return true;
      }

      if (!visited.insert(current).second) {
        continue;
      }

      if (const auto targets = edges.find(current); targets != edges.end()) {
        pending.insert(pending.end(), targets->second.begin(), targets->second.end());
      }
    }

    return false;
  }

  /**
   * Порядок модуляторов зоны. Маршруты рассматриваются в порядке создания, и маршрут, замыкающий цикл,
   * в зависимости не попадает (D-010). Его цель всегда идёт раньше источника, поэтому она
   * читает значение источника с прошлого шага. Среди готовых выбирается меньший ID
   */
  std::vector<ModuleId> sortModulators(const SynthModel& model, const std::set<ModuleId>& modulators) {
    std::map<ModuleId, std::set<ModuleId>> edges;

    for (const ModRoute& route : model.getRoutes()) {
      const ModuleId source = route.source.module;
      const ModuleId target = route.target.module;

      if (modulators.contains(source) && modulators.contains(target) && !hasPath(edges, target, source)) {
        edges[source].insert(target);
      }
    }

    std::map<ModuleId, size_t> pendingSources;

    for (const ModuleId id : modulators) {
      pendingSources[id] = 0;
    }

    for (const auto& [source, targets] : edges) {
      for (const ModuleId target : targets) {
        pendingSources[target]++;
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

      if (const auto targets = edges.find(current); targets != edges.end()) {
        for (const ModuleId target : targets->second) {
          if (--pendingSources[target] == 0) {
            ready.insert(target);
          }
        }
      }
    }

    return order;
  }

  class ModulationCompiler {
    public:
      ModulationCompiler(const SynthModel& model, ModuleSlots& slots, ParameterBases& bases, ExecutionPlan& plan)
        : model(model), slots(slots), bases(bases), plan(plan) {
        this->zones[MAIN_ZONE] = { &plan.main, nullptr };

        for (const PlanGenerator& generator : plan.generators) {
          this->zones[generator.id] = { generator.subgraph, generator.processor };
        }
      }

      void compile() {
        std::vector<Instance> executed;

        for (const auto& [zone, info] : this->zones) {
          std::vector<Instance> zoneExecuted = this->findExecuted(zone);

          executed.insert(executed.end(), zoneExecuted.begin(), zoneExecuted.end());
        }

        const std::set<Instance> sources = this->collectSources(executed);

        for (const auto& [zone, info] : this->zones) {
          this->compileZone(zone, sources);
        }

        this->addLastValues();

        for (const Instance& source : sources) {
          if (source.zone == MAIN_ZONE && this->model.findModule(source.module)->descriptor->isVoiceModulator) {
            this->plan.sharedModulators.push_back(this->slots.at(source.module).processor.get());
          }
        }
      }

    private:
      const SynthModel& model;
      ModuleSlots& slots;
      ParameterBases& bases;
      ExecutionPlan& plan;
      std::map<ModuleId, Zone> zones;
      std::map<ModuleId, std::set<std::pair<ModuleId, uint32_t>>> lastValues; // [генератор]: (модуль, выход)

      /**
       * В какой зоне живёт экземпляр источника, из которого читает получатель. Голосовой источник
       * своего генератора читается из голоса, общий модулятор — из своего экземпляра у получателя
       */
      ModuleId findSourceZone(const ModuleInstance& source, ModuleId consumer) const {
        if (source.descriptor->role == MODULE_ROLE::GENERATOR) {
          return source.id;
        }

        if (source.owner) {
          return *source.owner;
        }

        return source.descriptor->isVoiceModulator ? consumer : MAIN_ZONE;
      }

      /**
       * Источник маршрута или nullptr, если он даёт 0: отключён или его генератор не исполняется.
       * Заглушка особого случая не требует: её модулятор ничего не считает и выдаёт 0 (D-021)
       */
      const ModuleInstance* findActiveSource(const ModRoute& route, ModuleId consumer) const {
        const ModuleInstance* source = this->model.findModule(route.source.module);

        if (!source->enabled || !this->zones.contains(this->findSourceZone(*source, consumer))) {
          return nullptr;
        }

        return source;
      }

      ModuleSlots& getSlots(ModuleId zone, uint32_t voice) {
        return zone == MAIN_ZONE ? this->slots : this->zones.at(zone).generator->getVoiceSlots()[voice];
      }

      /**
       * Исполняемые модули зоны. У отключённых и заглушек маршруты сохраняются, но не действуют
       */
      std::vector<Instance> findExecuted(ModuleId zone) const {
        std::vector<Instance> executed;

        for (const ModuleId id : this->zones.at(zone).graph->modules) {
          const ModuleInstance* module = this->model.findModule(id);

          if (module->enabled && !module->isStub() && module->descriptor->role != MODULE_ROLE::OUTPUT) {
            executed.push_back({ zone, id });
          }
        }

        return executed;
      }

      /**
       * Экземпляры модуляторов, от которых зависят заданные экземпляры, напрямую или через другие модуляторы.
       * Генератор здесь тоже источник (velocity), но считать в нём нечего
       */
      std::set<Instance> collectSources(std::vector<Instance> pending) const {
        std::set<Instance> visited;
        std::set<Instance> sources;

        while (!pending.empty()) {
          const Instance current = pending.back();
          pending.pop_back();

          if (!visited.insert(current).second) {
            continue;
          }

          for (const ModRoute& route : this->model.getRoutes()) {
            if (route.target.module != current.module) {
              continue;
            }

            const ModuleInstance* source = this->findActiveSource(route, current.zone);

            if (source && source->descriptor->role != MODULE_ROLE::GENERATOR) {
              const Instance instance = { this->findSourceZone(*source, current.zone), source->id };

              sources.insert(instance);
              pending.push_back(instance);
            }
          }
        }

        return sources;
      }

      /**
       * Адрес значения источника. Из чужой зоны голосовой источник виден как значение последнего голоса (D-004)
       */
      const float* resolveSource(const ModRoute& route, const ModuleInstance& source, ModuleId zone, uint32_t voice) {
        const ModuleId sourceZone = this->findSourceZone(source, zone);
        const uint32_t output = findOutputIndex(*source.descriptor, route.source.output);

        if (sourceZone == MAIN_ZONE) {
          return &this->slots.at(source.id).modOutputs[output];
        }

        GeneratorProcessor* generator = this->zones.at(sourceZone).generator;

        if (sourceZone != zone) {
          this->lastValues[sourceZone].insert({ source.id, output });

          return &generator->getLastOutputs().at(source.id)[output];
        }

        return source.descriptor->role == MODULE_ROLE::GENERATOR
          ? &generator->getVelocities()[voice]
          : &generator->getVoiceSlots()[voice].at(source.id).modOutputs[output];
      }

      std::vector<ParameterBinding> bindParameters(ModuleId id, ModuleId zone, uint32_t voice) {
        const ModuleDescriptor* descriptor = this->model.findModule(id)->descriptor;
        ModuleSlot& slot = this->getSlots(zone, voice).at(id);
        std::vector<ParameterBinding> parameters;

        for (size_t index = 0; index < descriptor->parameters.size(); index++) {
          ParameterBinding binding = { &this->bases.at(id)[index], &slot.parameters[index], {} };

          for (const ModRoute& route : this->model.getRoutes()) {
            if (route.target.module != id || route.target.parameter != descriptor->parameters[index].key) {
              continue;
            }

            if (const ModuleInstance* source = this->findActiveSource(route, zone)) {
              binding.routes.push_back({ this->resolveSource(route, *source, zone, voice), route.depth });
            }
          }

          parameters.push_back(std::move(binding));
        }

        return parameters;
      }

      void compileZone(ModuleId zone, const std::set<Instance>& sources) {
        const std::vector<Instance> executed = this->findExecuted(zone);
        GraphPlan& graph = *this->zones.at(zone).graph;

        std::set<ModuleId> modulators;

        for (const Instance& source : sources) {
          if (source.zone == zone) {
            modulators.insert(source.module);
          }
        }

        // Голос держат только те, от кого цепочка маршрутов доходит до его собственных модулей (D-051).
        // Какой из них действительно держит, решает он сам через isFinished
        std::vector<ModuleId> holders;

        for (const Instance& source : this->collectSources(executed)) {
          if (source.zone == zone) {
            holders.push_back(source.module);
          }
        }

        const std::vector<ModuleId> order = sortModulators(this->model, modulators);

        graph.modulation.resize(graph.voices.size());

        for (uint32_t voice = 0; voice < graph.modulation.size(); voice++) {
          VoiceModulation& modulation = graph.modulation[voice];
          ModuleSlots& voiceSlots = this->getSlots(zone, voice);

          for (const ModuleId id : order) {
            ModuleSlot& slot = voiceSlots.at(id);

            modulation.modulators.push_back({
              .processor = slot.processor.get(),
              .ramps = slot.parameters.data(),
              .outputs = slot.modOutputs.data(),
              .outputCount = (uint32_t)slot.modOutputs.size(),
              .parameters = this->bindParameters(id, zone, voice),
            });
          }

          for (const Instance& instance : executed) {
            std::vector<ParameterBinding> parameters = this->bindParameters(instance.module, zone, voice);

            modulation.parameters.insert(
              modulation.parameters.end(),
              std::make_move_iterator(parameters.begin()),
              std::make_move_iterator(parameters.end())
            );
          }

          for (const ModuleId id : holders) {
            modulation.holders.push_back(voiceSlots.at(id).processor.get());
          }
        }
      }

      /**
       * Значения голосов, которые читают чужие зоны. Вызывается после всех маршрутов
       */
      void addLastValues() {
        for (const auto& [zone, values] : this->lastValues) {
          GeneratorProcessor* generator = this->zones.at(zone).generator;
          GraphPlan& graph = *this->zones.at(zone).graph;

          for (uint32_t voice = 0; voice < graph.modulation.size(); voice++) {
            for (const auto& [module, output] : values) {
              const float* source = module == zone
                ? &generator->getVelocities()[voice]
                : &generator->getVoiceSlots()[voice].at(module).modOutputs[output];

              graph.modulation[voice].lastValues.push_back({ source, &generator->getLastOutputs().at(module)[output] });
            }
          }
        }
      }
  };
}

void syncParameterBases(const SynthModel& model, ParameterBases& bases) {
  std::erase_if(bases, [&model](const auto& entry) {
    return model.findModule(entry.first) == nullptr;
  });

  for (const auto& [id, module] : model.getModules()) {
    std::vector<BaseValue>& values = bases[id];

    values.clear();

    for (const ParameterDescriptor& parameter : module.descriptor->parameters) {
      const float value = toNormalized(parameter, module.parameters.at(parameter.key));

      values.push_back({ &parameter, value, value, value, 0.0f });
    }
  }
}

void compileModulation(const SynthModel& model, ModuleSlots& slots, ParameterBases& bases, ExecutionPlan& plan) {
  ModulationCompiler(model, slots, bases, plan).compile();
}
