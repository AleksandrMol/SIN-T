#include <cstdio>
#include "./plugin.h"
#include "../engine/preset/preset.h"
#include <cstring>
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
  snprintf(info->name, sizeof(info->name), "%s", "My Port Name");
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
  snprintf(info->name, sizeof(info->name), "%s", "My Port Name");
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

  const std::string text = saveModel(*instance->model);
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

  auto loaded = loadModel(&instance->registry, text);

  if (!loaded) {
    return false;
  }

  // То же состояние менять незачем: DAW может вернуть его и работающему плагину
  if (*loaded == *instance->model) {
    return true;
  }

  // Подмены плана во время звука пока нет (этап 5), а process идёт, только пока плагин активен
  if (instance->isActive) {
    return false;
  }

  instance->model = std::move(*loaded);
  instance->engine.applyModel(*instance->model);

  return true;
}

extern const clap_plugin_state_t s_my_plug_state = {
  .save = my_plug_state_save,
  .load = my_plug_state_load,
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

  // if (!strcmp(id, CLAP_EXT_LATENCY))
  //   return &s_my_plug_latency;

  return NULL;
}
