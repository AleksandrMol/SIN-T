#include "./plugin.h"

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

  const uint32_t total_frames = process->frames_count;
  const uint32_t num_events = process->in_events->size(process->in_events);
  uint32_t event_index = 0;

  if(!checkAudioOutputs(process)) {
    return CLAP_PROCESS_CONTINUE;
  }

  // Цикл по каждому сэмплу в текущем аудио-блоке
  for (uint32_t frame = 0; frame < total_frames; ++frame) {

    // EVENTS
    while (event_index < num_events) {
      const clap_event_header_t* event_header = process->in_events->get(process->in_events, event_index);

      if (event_header->time > frame) {
        break;
      }

      my_plug_process_event(instance, event_header);

      event_index++;
    }

    instance->gen.process();

    // Запись результата работы прагина в аудиовыходы
    process->audio_outputs[0].data32[0][frame] = instance->gen.getOutput();
    process->audio_outputs[0].data32[1][frame] = instance->gen.getOutput();
  }

  return CLAP_PROCESS_CONTINUE; // Говорим DAW, что мы готовы обрабатывать звук дальше
}
