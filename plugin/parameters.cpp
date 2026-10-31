#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <utility>

#include "./plugin.h"

// Слоты хоста (D-011). Хост видит постоянный список из SLOT_COUNT параметров 0..1, номер слота
// и есть clap_id, а что стоит за номером, решает таблица. Значения от хоста идут двумя путями
// (D-056): сразу в звук, из аудиопотока, и с задержкой в модель, на главном потоке

namespace {
  float readValue(const SynthModel& model, const SlotInfo& info) {
    return model.findModule(info.target.module)->parameters.at(info.descriptor->key);
  }

  /**
   * Значение по умолчанию (D-061). Модуля, созданного в редакторе, в загруженной модели нет
   */
  float readDefault(const MyPluginInstance* instance, const SlotInfo& info) {
    return instance->loadedModel->findModule(info.target.module)
      ? readValue(*instance->loadedModel, info)
      : info.descriptor->defaultValue;
  }

  uint32_t my_plug_params_count(const clap_plugin_t *plugin) {
    return SLOT_COUNT;
  }

  bool my_plug_params_get_info(const clap_plugin_t *plugin, uint32_t index, clap_param_info_t *info) {
    MyPluginInstance* instance = (MyPluginInstance*)plugin;

    if (index >= SLOT_COUNT) {
      return false;
    }

    const SlotInfo* slot = instance->slots.find(index);

    *info = {};
    info->id = index;
    info->flags = CLAP_PARAM_IS_AUTOMATABLE | (slot ? 0 : CLAP_PARAM_IS_HIDDEN);
    info->min_value = 0.0;
    info->max_value = 1.0;

    if (slot) {
      info->default_value = toNormalized(*slot->descriptor, readDefault(instance, *slot));
      snprintf(info->name, sizeof(info->name), "%s", slot->name.c_str());
      snprintf(info->module, sizeof(info->module), "%s", slot->path.c_str());
    } else {
      snprintf(info->name, sizeof(info->name), "Slot %u", index - MACRO_COUNT + 1);
    }

    return true;
  }

  bool my_plug_params_get_value(const clap_plugin_t *plugin, clap_id param_id, double *out_value) {
    MyPluginInstance* instance = (MyPluginInstance*)plugin;

    if (param_id >= SLOT_COUNT) {
      return false;
    }

    my_plug_sync_slot_values(instance);

    const SlotInfo* slot = instance->slots.find(param_id);

    *out_value = slot ? toNormalized(*slot->descriptor, readValue(*instance->model, *slot)) : 0.0;

    return true;
  }

  bool my_plug_params_value_to_text(
    const clap_plugin_t *plugin,
    clap_id param_id,
    double value,
    char *out_buffer,
    uint32_t out_buffer_capacity
  ) {
    MyPluginInstance* instance = (MyPluginInstance*)plugin;

    if (param_id >= SLOT_COUNT || out_buffer_capacity == 0) {
      return false;
    }

    const SlotInfo* slot = instance->slots.find(param_id);
    const std::string text = slot
      ? formatParameter(*slot->descriptor, toReal(*slot->descriptor, (float)value))
      : std::to_string(value);

    snprintf(out_buffer, out_buffer_capacity, "%s", text.c_str());

    return true;
  }

  bool my_plug_params_text_to_value(
    const clap_plugin_t *plugin,
    clap_id param_id,
    const char *param_value_text,
    double *out_value
  ) {
    MyPluginInstance* instance = (MyPluginInstance*)plugin;

    if (param_id >= SLOT_COUNT) {
      return false;
    }

    static const ParameterDescriptor FREE_SLOT = {
      .key = "slot",
      .name = "Slot",
      .minValue = 0.0f,
      .maxValue = 1.0f,
      .defaultValue = 0.0f,
    };

    const SlotInfo* slot = instance->slots.find(param_id);
    const ParameterDescriptor& descriptor = slot ? *slot->descriptor : FREE_SLOT;
    const auto real = parseParameter(descriptor, param_value_text);

    if (!real) {
      return false;
    }

    *out_value = toNormalized(descriptor, *real);

    return true;
  }

  /**
   * Изменения без звука. На неактивном плагине process не будет, поэтому значения применяются сразу
   */
  void my_plug_params_flush(const clap_plugin_t *plugin, const clap_input_events_t *in, const clap_output_events_t *out) {
    MyPluginInstance* instance = (MyPluginInstance*)plugin;
    const uint32_t eventCount = in->size(in);

    for (uint32_t index = 0; index < eventCount; index++) {
      const clap_event_header_t* event = in->get(in, index);

      if (event->space_id == CLAP_CORE_EVENT_SPACE_ID && event->type == CLAP_EVENT_PARAM_VALUE) {
        my_plug_set_slot(instance, (const clap_event_param_value_t *)event);
      }
    }

    my_plug_send_host_events(instance, out);

    if (instance->isActive) {
      my_plug_request_slot_sync(instance);
    } else {
      my_plug_sync_slot_values(instance);
    }
  }
}

extern const clap_plugin_params_t s_my_plug_params = {
  .count = my_plug_params_count,
  .get_info = my_plug_params_get_info,
  .get_value = my_plug_params_get_value,
  .value_to_text = my_plug_params_value_to_text,
  .text_to_value = my_plug_params_text_to_value,
  .flush = my_plug_params_flush,
};

/**
 * Значение от хоста. Не число отбрасывается, значение вне 0..1 ограничивается.
 * Вызывается из аудиопотока, а вне работы звука с главного
 */
void my_plug_set_slot(MyPluginInstance *instance, const clap_event_param_value_t *event) {
  if (event->param_id >= SLOT_COUNT || !std::isfinite(event->value)) {
    return;
  }

  const float value = std::clamp((float)event->value, 0.0f, 1.0f);

  instance->engine.setSlot(event->param_id, value);
  instance->slotInbox.post(event->param_id, value);
}

/**
 * Отдаёт хосту то, что накопил редактор. Вызывается из process и flush: только там можно писать события
 */
void my_plug_send_host_events(MyPluginInstance *instance, const clap_output_events_t *out) {
  instance->slotOutbox.drain([out](uint32_t slot, OUTBOX_EVENT type, float value) {
    if (type == OUTBOX_EVENT::VALUE) {
      clap_event_param_value_t event{};

      event.header.size = sizeof(event);
      event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
      event.header.type = CLAP_EVENT_PARAM_VALUE;
      event.param_id = slot;
      event.note_id = -1;
      event.port_index = -1;
      event.channel = -1;
      event.key = -1;
      event.value = value;
      out->try_push(out, &event.header);

      return;
    }

    clap_event_param_gesture_t event{};

    event.header.size = sizeof(event);
    event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    event.header.type = type == OUTBOX_EVENT::GESTURE_BEGIN ? CLAP_EVENT_PARAM_GESTURE_BEGIN : CLAP_EVENT_PARAM_GESTURE_END;
    event.param_id = slot;
    out->try_push(out, &event.header);
  });
}

void my_plug_request_slot_sync(MyPluginInstance *instance) {
  if (instance->slotInbox.takeRequest()) {
    instance->host->request_callback(instance->host);
  }
}

/**
 * Переносит значения от хоста в модель. Звук их уже услышал, поэтому движку достаточно сообщить об этом.
 * Параметр из плана и правки вне работы звука движку ещё предстоит применить. Вызывается на главном потоке
 */
void my_plug_sync_slot_values(MyPluginInstance *instance) {
  bool isPlanNeeded = false;

  instance->slotInbox.drain([instance, &isPlanNeeded](uint32_t slotId, float value) {
    const SlotInfo* slot = instance->slots.find(slotId);

    if (!slot) {
      return;
    }

    const SetParameter command = { slot->target.module, slot->descriptor->key, toReal(*slot->descriptor, value) };

    if (!instance->model->apply(command)) {
      return;
    }

    instance->modelRevision++;

    if (slot->descriptor->isStructural || !instance->isActive) {
      isPlanNeeded = true;
    } else {
      instance->engine.acknowledgeParameter(command);
    }
  });

  if (isPlanNeeded) {
    instance->engine.applyModel(*instance->model);
  }
}

/**
 * Заменяет модель и таблицу слотов и сообщает хосту, что изменилось. Значения от хоста, не дошедшие
 * до модели, нужно забрать заранее: они относятся к прежнему состоянию. Вызывается на главном потоке
 */
void my_plug_replace_state(MyPluginInstance *instance, SessionState state) {
  const SlotTable previous = std::exchange(instance->slots, std::move(state.slots));

  instance->presetName = std::move(state.presetName);
  instance->modelRevision++;

  // Модель меняется сразу: хост может тут же сохранить состояние, а звук догонит её сам (D-054)
  instance->model = std::move(state.model);
  instance->loadedModel = instance->model;
  instance->engine.setSlotTargets(instance->slots.getTargets());
  instance->engine.replaceModel(*instance->model);

  if (!instance->hostParams) {
    return;
  }

  for (const uint32_t slot : instance->slots.findReplaced(previous)) {
    instance->hostParams->clear(instance->host, slot, CLAP_PARAM_CLEAR_ALL);
  }

  // INFO нужен всегда: вместе с состоянием меняются значения по умолчанию
  instance->hostParams->rescan(instance->host, CLAP_PARAM_RESCAN_VALUES | CLAP_PARAM_RESCAN_TEXT | CLAP_PARAM_RESCAN_INFO);
}
