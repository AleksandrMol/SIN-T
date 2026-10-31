#include "./slotTable.h"

#include <algorithm>
#include <utility>

namespace {
  const ParameterDescriptor* findParameter(const ModuleInstance& module, const std::string& key) {
    const auto& parameters = module.descriptor->parameters;
    const auto it = std::ranges::find_if(parameters, [&key](const ParameterDescriptor& parameter) {
      return parameter.key == key;
    });

    return it != parameters.end() ? &*it : nullptr;
  }

  /**
   * Запись ещё описывает параметр модели. Заглушка не подходит: её описание живёт вместе с моделью
   */
  bool isValid(const SlotBinding& binding, const SynthModel& model) {
    const ModuleInstance* module = model.findModule(binding.module);

    return module
      && !module->isStub()
      && module->descriptor->type == binding.type
      && findParameter(*module, binding.parameter);
  }

  /**
   * Порядковый номер среди модулей того же типа в том же графе
   */
  uint32_t findNumber(const SynthModel& model, const ModuleInstance& module) {
    uint32_t number = 1;

    for (const auto& [id, other] : model.getModules()) {
      if (id < module.id && other.owner == module.owner && other.descriptor->type == module.descriptor->type) {
        number++;
      }
    }

    return number;
  }

  std::string makeLabel(const SynthModel& model, const ModuleInstance& module) {
    return module.descriptor->name + " " + std::to_string(findNumber(model, module));
  }

  /**
   * Reaper показывает только имя параметра, поэтому модуль стоит и в имени,
   * иначе два осциллятора выглядели бы в списке одинаково (D-057)
   */
  SlotInfo makeInfo(const SynthModel& model, const ModuleInstance& module, const ParameterDescriptor& parameter) {
    SlotInfo info = {
      .descriptor = &parameter,
      .target = {
        .module = module.id,
        .parameter = (uint32_t)(&parameter - module.descriptor->parameters.data()),
      },
      .name = parameter.name,
      .path = module.descriptor->name,
    };

    if (module.descriptor->role == MODULE_ROLE::MACROS) {
      return info;
    }

    const std::string label = makeLabel(model, module);

    if (module.descriptor->role == MODULE_ROLE::GENERATOR) {
      info.name = "G" + std::to_string(findNumber(model, module)) + " " + parameter.name;
      info.path = label;
    } else if (module.owner) {
      const ModuleInstance& generator = *model.findModule(*module.owner);

      info.name = "G" + std::to_string(findNumber(model, generator)) + " " + label + " " + parameter.name;
      info.path = makeLabel(model, generator) + "/" + label;
    } else {
      info.name = label + " " + parameter.name;
      info.path = label;
    }

    return info;
  }
}

SlotTable::SlotTable() {
  for (uint32_t slot = 0; slot < MACRO_COUNT; slot++) {
    this->bindings[slot] = { MACROS_MODULE_ID, MACROS_DESCRIPTOR.type, MACROS_DESCRIPTOR.parameters[slot].key };
  }
}

void SlotTable::sync(const SynthModel& model) {
  std::erase_if(this->bindings, [&model](const auto& entry) {
    return !isValid(entry.second, model);
  });

  this->infos.clear();

  std::map<std::pair<ModuleId, std::string>, uint32_t> occupied;

  // Повтор возможен только в испорченном состоянии: у параметра остаётся первый слот
  std::erase_if(this->bindings, [&occupied](const auto& entry) {
    return !occupied.try_emplace({ entry.second.module, entry.second.parameter }, entry.first).second;
  });

  uint32_t freeSlot = MACRO_COUNT;

  for (const auto& [id, module] : model.getModules()) {
    if (module.isStub()) {
      continue;
    }

    for (const ParameterDescriptor& parameter : module.descriptor->parameters) {
      const auto existing = occupied.find({ id, parameter.key });
      uint32_t slot;

      if (existing != occupied.end()) {
        slot = existing->second;
      } else {
        while (this->bindings.contains(freeSlot)) {
          freeSlot++;
        }

        if (freeSlot >= SLOT_COUNT) {
          continue;
        }

        slot = freeSlot;
        this->bindings[slot] = { id, module.descriptor->type, parameter.key };
      }

      this->infos.insert_or_assign(slot, makeInfo(model, module, parameter));
    }
  }
}

bool SlotTable::restore(uint32_t slot, SlotBinding binding) {
  if (slot >= SLOT_COUNT) {
    return false;
  }

  // Макросы заняты с самого начала, поэтому их номера сюда не пройдут
  return this->bindings.try_emplace(slot, std::move(binding)).second;
}

const SlotInfo* SlotTable::find(uint32_t slot) const {
  const auto it = this->infos.find(slot);

  return it != this->infos.end() ? &it->second : nullptr;
}

std::map<uint32_t, SlotBinding> SlotTable::getPoolBindings() const {
  std::map<uint32_t, SlotBinding> pool = this->bindings;

  std::erase_if(pool, [](const auto& entry) {
    return entry.first < MACRO_COUNT;
  });

  return pool;
}

std::vector<SlotTarget> SlotTable::getTargets() const {
  std::vector<SlotTarget> targets(SLOT_COUNT);

  for (const auto& [slot, info] : this->infos) {
    targets[slot] = info.target;
  }

  return targets;
}

std::vector<uint32_t> SlotTable::findReplaced(const SlotTable& previous) const {
  std::vector<uint32_t> replaced;

  for (const auto& [slot, binding] : previous.bindings) {
    const auto current = this->bindings.find(slot);

    if (current == this->bindings.end() || current->second != binding) {
      replaced.push_back(slot);
    }
  }

  return replaced;
}
