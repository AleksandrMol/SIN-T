#include <cstdio>
#include "./plugin.h"
#include "../engine/presetLibrary/presetLibrary.h"
#include "../engine/sessionState/sessionState.h"
#include <cstring>
#include <filesystem>
#include <string>

// /////////////////////////////
// // clap_plugin_audio_ports //
// /////////////////////////////

uint32_t my_plug_audio_ports_count(const clap_plugin_t *plugin, bool is_input) {
  // Синтезатор только генерирует звук: входов нет, один стерео-выход
  return is_input ? 0 : 1;
}

bool my_plug_audio_ports_get(
  const clap_plugin_t *plugin,
  uint32_t index,
  bool is_input,
  clap_audio_port_info_t *info
) {
  if (index > 0 || is_input)
    return false;
  info->id = 0;
  snprintf(info->name, sizeof(info->name), "%s", "Output");
  info->channel_count = 2;
  info->flags = CLAP_AUDIO_PORT_IS_MAIN;
  info->port_type = CLAP_PORT_STEREO;
  info->in_place_pair = CLAP_INVALID_ID;
  return true;
}

extern const clap_plugin_audio_ports_t s_my_plug_audio_ports = {
  .count = my_plug_audio_ports_count,
  .get = my_plug_audio_ports_get,
};

// ////////////////////////////
// // clap_plugin_note_ports //
// ////////////////////////////

uint32_t my_plug_note_ports_count(const clap_plugin_t *plugin, bool is_input) {
  // Один нотный вход, нотных выходов нет
  return is_input ? 1 : 0;
}

bool my_plug_note_ports_get(
  const clap_plugin_t *plugin,
  uint32_t index,
  bool is_input,
  clap_note_port_info_t *info
) {
  if (index > 0 || !is_input)
    return false;
  info->id = 0;
  snprintf(info->name, sizeof(info->name), "%s", "Notes");
  info->supported_dialects =
    CLAP_NOTE_DIALECT_CLAP | CLAP_NOTE_DIALECT_MIDI;
  info->preferred_dialect = CLAP_NOTE_DIALECT_CLAP;
  return true;
}

extern const clap_plugin_note_ports_t s_my_plug_note_ports = {
  .count = my_plug_note_ports_count,
  .get = my_plug_note_ports_get,
};

// //////////////////
// // clap_latency //
// //////////////////

uint32_t my_plug_latency_get(const clap_plugin_t *plugin) {
  MyPluginInstance* instance = (MyPluginInstance*)plugin;
  return instance->latency;
}

extern const clap_plugin_latency_t s_my_plug_latency = {
  .get = my_plug_latency_get,
};

// ////////////////
// // clap_state //
// ////////////////

bool my_plug_state_save(const clap_plugin_t *plugin, const clap_ostream_t *stream) {
  MyPluginInstance* instance = (MyPluginInstance*)plugin;

  // Автоматизация могла ещё не дойти до модели
  my_plug_sync_slot_values(instance);

  const std::string text = saveSession(*instance->model, instance->slots, instance->presetName);
  size_t written = 0;

  // Поток может принять только часть данных за один вызов
  while (written < text.size()) {
    const int64_t count = stream->write(stream, text.data() + written, text.size() - written);

    if (count <= 0) {
      return false;
    }

    written += (size_t)count;
  }

  return true;
}

bool my_plug_state_load(const clap_plugin_t *plugin, const clap_istream_t *stream) {
  MyPluginInstance* instance = (MyPluginInstance*)plugin;

  std::string text;
  char chunk[4096];

  while (true) {
    const int64_t count = stream->read(stream, chunk, sizeof(chunk));

    if (count < 0) {
      return false;
    }

    if (count == 0) {
      break;
    }

    text.append(chunk, (size_t)count);
  }

  auto loaded = loadSession(&instance->registry, text);

  if (!loaded) {
    return false;
  }

  // Что не дошло до модели, относится к прежнему состоянию и в новое попасть не должно
  my_plug_sync_slot_values(instance);

  // То же состояние менять незачем: DAW может вернуть его и работающему плагину
  if (loaded->model == *instance->model && loaded->slots == instance->slots) {
    instance->presetName = loaded->presetName;
    return true;
  }

  my_plug_replace_state(instance, std::move(*loaded));

  return true;
}

extern const clap_plugin_state_t s_my_plug_state = {
  .save = my_plug_state_save,
  .load = my_plug_state_load,
};

// /////////////////////
// // clap_preset_load //
// /////////////////////

namespace {
  void reportPresetError(
    MyPluginInstance* instance,
    uint32_t location_kind,
    const char *location,
    const char *load_key,
    const std::string& message
  ) {
    if (instance->hostPresetLoad) {
      instance->hostPresetLoad->on_error(instance->host, location_kind, location, load_key, 0, message.c_str());
    }
  }
}

bool my_plug_preset_load_from_location(
  const clap_plugin_t *plugin,
  uint32_t location_kind,
  const char *location,
  const char *load_key
) {
  MyPluginInstance* instance = (MyPluginInstance*)plugin;

  // Пресеты лежат отдельными файлами, встроенных в плагин хост не видит (D-059)
  if (location_kind != CLAP_PRESET_DISCOVERY_LOCATION_FILE || !location) {
    reportPresetError(instance, location_kind, location, load_key, "плагин загружает пресеты только из файлов");
    return false;
  }

  const std::filesystem::path path(location);
  const auto text = readTextFile(path);

  if (!text) {
    reportPresetError(instance, location_kind, location, load_key, text.error());
    return false;
  }

  if (const auto result = my_plug_load_preset(instance, *text, path.stem().string()); !result) {
    reportPresetError(instance, location_kind, location, load_key, result.error());
    return false;
  }

  if (instance->hostPresetLoad) {
    instance->hostPresetLoad->loaded(instance->host, location_kind, location, load_key);
  }

  return true;
}

extern const clap_plugin_preset_load_t s_my_plug_preset_load = {
  .from_location = my_plug_preset_load_from_location,
};

// Возвращает указатели на расширения (Extensions): 
// поддержка GUI, параметров, MIDI — всё это отдельные расширения.
const void* my_plugin_get_extension(const struct clap_plugin *plugin, const char *id) {
  // Два первых расширения необходимы для обработки MIDI и вывода звука
  if (!strcmp(id, CLAP_EXT_AUDIO_PORTS))
    return &s_my_plug_audio_ports;
  if (!strcmp(id, CLAP_EXT_NOTE_PORTS))
    return &s_my_plug_note_ports;

  if (!strcmp(id, CLAP_EXT_STATE))
    return &s_my_plug_state;
  if (!strcmp(id, CLAP_EXT_PARAMS))
    return &s_my_plug_params;
  if (!strcmp(id, CLAP_EXT_PRESET_LOAD) || !strcmp(id, CLAP_EXT_PRESET_LOAD_COMPAT))
    return &s_my_plug_preset_load;

  // if (!strcmp(id, CLAP_EXT_LATENCY))
  //   return &s_my_plug_latency;

  return my_plugin_get_editor_extension(id);
}
