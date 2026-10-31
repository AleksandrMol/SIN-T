#include <utility>

#include "../engine/preset/preset.h"
#include "./plugin.h"

// Правки со стороны плагина: редактор меняет модель только отсюда (D-030). Всё на главном потоке (ТЗ §11.2)

namespace {
  void notifyHost(MyPluginInstance *instance, const SlotTable& previous) {
    if (!instance->hostParams || instance->slots == previous) {
      return;
    }

    for (const uint32_t slot : instance->slots.findReplaced(previous)) {
      instance->hostParams->clear(instance->host, slot, CLAP_PARAM_CLEAR_ALL);
    }

    // Имена сдвигаются вслед за номерами модулей, новые слоты становятся видимыми
    instance->hostParams->rescan(instance->host, CLAP_PARAM_RESCAN_VALUES | CLAP_PARAM_RESCAN_TEXT | CLAP_PARAM_RESCAN_INFO);
  }

  /**
   * Сообщает хосту о значении: ни process, ни flush в этот момент не идут, поэтому событие ждёт их
   */
  void postValue(MyPluginInstance *instance, const SetParameter& command) {
    const auto slot = instance->slots.findSlot(command.module, command.parameter);

    if (!slot) {
      return;
    }

    const float value = instance->model->findModule(command.module)->parameters.at(command.parameter);

    instance->slotOutbox.post(*slot, toNormalized(*instance->slots.find(*slot)->descriptor, value));
    my_plug_request_host_events(instance);
  }
}

void my_plug_request_host_events(MyPluginInstance *instance) {
  if (instance->hostParams) {
    instance->hostParams->request_flush(instance->host);
  }
}

CommandResult my_plug_apply_command(MyPluginInstance *instance, const Command& command) {
  // Значения от хоста приходят раньше правки: новый план должен собираться из свежей модели
  my_plug_sync_slot_values(instance);

  const CommandResult result = instance->model->apply(command);

  if (!result) {
    return result;
  }

  instance->modelRevision++;

  if (instance->hostState) {
    instance->hostState->mark_dirty(instance->host);
  }

  if (const auto* parameter = std::get_if<SetParameter>(&command)) {
    instance->engine.applyModel(*instance->model);
    postValue(instance, *parameter);

    return result;
  }

  const SlotTable previous = instance->slots;

  instance->slots.sync(*instance->model);
  instance->engine.setSlotTargets(instance->slots.getTargets());
  instance->engine.applyModel(*instance->model);
  notifyHost(instance, previous);

  return result;
}

void my_plug_begin_gesture(MyPluginInstance *instance, ModuleId module, const std::string& parameter) {
  if (const auto slot = instance->slots.findSlot(module, parameter)) {
    instance->slotOutbox.beginGesture(*slot);
    my_plug_request_host_events(instance);
  }
}

void my_plug_end_gesture(MyPluginInstance *instance, ModuleId module, const std::string& parameter) {
  if (const auto slot = instance->slots.findSlot(module, parameter)) {
    instance->slotOutbox.endGesture(*slot);
    my_plug_request_host_events(instance);
  }
}

std::expected<void, std::string> my_plug_load_preset(MyPluginInstance *instance, const std::string& text, std::string name) {
  auto loaded = loadModel(&instance->registry, text);

  if (!loaded) {
    return std::unexpected(loaded.error());
  }

  my_plug_sync_slot_values(instance);

  // Таблица остаётся: слот сохранит тот модуль, который есть и в новом пресете под тем же ID и типом
  SessionState state = { .model = std::move(*loaded), .slots = instance->slots, .presetName = std::move(name) };

  state.slots.sync(state.model);
  my_plug_replace_state(instance, std::move(state));

  if (instance->hostState) {
    instance->hostState->mark_dirty(instance->host);
  }

  return {};
}

std::expected<void, std::string> my_plug_save_preset(MyPluginInstance *instance, const std::string& name, PRESET_WRITE mode) {
  if (!instance->presetLibrary) {
    return std::unexpected("система не сообщает домашнюю папку");
  }

  my_plug_sync_slot_values(instance);

  if (const auto written = instance->presetLibrary->write(name, saveModel(*instance->model), mode); !written) {
    return written;
  }

  instance->presetName = name;
  instance->modelRevision++;

  if (instance->hostState) {
    instance->hostState->mark_dirty(instance->host);
  }

  return {};
}
