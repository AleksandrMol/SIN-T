#include "./plugin.h"

#include <algorithm>

bool checkAudioOutputs (const clap_process_t *process) {
  // 1. Проверяем, что вообще есть выходные аудио-шины
  if (process->audio_outputs_count < 1) {
    return false;
  }

  // 2. Проверяем, что массив указателей на каналы существует
  if (!process->audio_outputs[0].data32) {
    return false; 
  }

  float* out_l = process->audio_outputs[0].data32[0];
  float* out_r = process->audio_outputs[0].data32[1];

  // 4. Проверяем каждый канал отдельно перед обработкой
  // (Если хост по какой-то причине дал только один канал или сбросил указатели)
  if (!out_l || !out_r) {
    return false;
  }

  return true;
};

// Сюда DAW будет заходить в аудио-потоке, чтобы плагин обработал звук.
clap_process_status my_plugin_process(
  const struct clap_plugin *plugin, 
  const clap_process_t *process
) {
  MyPluginInstance* instance = (MyPluginInstance*)plugin;

  if(!checkAudioOutputs(process)) {
    return CLAP_PROCESS_CONTINUE;
  }

  float* left = process->audio_outputs[0].data32[0];
  float* right = process->audio_outputs[0].data32[1];
  const uint32_t totalFrames = process->frames_count;
  const uint32_t eventCount = process->in_events->size(process->in_events);
  uint32_t done = 0;

  // Звук до события считается отдельно: так нота начинается точно в свой сэмпл (D-008)
  for (uint32_t index = 0; index < eventCount; index++) {
    const clap_event_header_t* event = process->in_events->get(process->in_events, index);
    const uint32_t time = std::min(event->time, totalFrames);

    if (time > done) {
      instance->engine.process(left + done, right + done, time - done);
      done = time;
    }

    my_plug_process_event(instance, event);
  }

  instance->engine.process(left + done, right + done, totalFrames - done);

  return CLAP_PROCESS_CONTINUE; // Говорим DAW, что мы готовы обрабатывать звук дальше
}
