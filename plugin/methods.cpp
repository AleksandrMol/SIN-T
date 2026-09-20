#include <stdlib.h>
#include <assert.h>
#include <math.h>

#include "../clap/plugin.h"
#include "../tools/console.h"

#include "./plugin.h"

// Колбэк инициализации конкретного инстанса плагина
bool my_plugin_init(const struct clap_plugin *plugin) {
  // Приводим указатель к нашему типу, чтобы получить доступ к стейту
  MyPluginInstance* instance = (MyPluginInstance*)plugin;

  // Тут можно выделить память под внутренние нужды плагина, если необходимо
  return true;
}

// Колбэк деинициализации (деструктор) — вызывается, когда плагин удаляют с дорожки
void my_plugin_destroy(const struct clap_plugin *plugin) {
  MyPluginInstance* instance = (MyPluginInstance*)plugin;

  // Освобождаем память, которую занимает сам объект плагина.
  delete instance;
}

// Вызывается хостом сразу после init, сообщает аудио-параметры среды
bool my_plugin_activate(
  const struct clap_plugin *plugin,
  double sample_rate,
  uint32_t min_frames_count,
  uint32_t max_frames_count
) {
  MyPluginInstance* instance = (MyPluginInstance*)plugin;

  instance->sample_rate = (float)sample_rate; // Сохраняем частоту дискретизации
  instance->gen.setSampleRate((float)sample_rate);

  return true;
}

void my_plugin_deactivate(const struct clap_plugin *plugin) {}

void my_plug_reset(const struct clap_plugin *plugin) {}

// Начинает или останавливает обработку звука (вызывается перед стартом воспроизведения)
bool my_plugin_start_processing(const struct clap_plugin *plugin) {
  return true;
}
void my_plugin_stop_processing(const struct clap_plugin *plugin) {}

// Сюда DAW будет заходить в аудио-потоке, чтобы плагин обработал звук.
// Самое «горячее» место в коде (Real-time thread). К нему мы вернемся отдельно.
clap_process_status my_plugin_process(
  const struct clap_plugin *plugin, 
  const clap_process_t *process
) {
  MyPluginInstance* instance = (MyPluginInstance*)plugin;

  const uint32_t total_frames = process->frames_count;
  const uint32_t num_events = process->in_events->size(process->in_events);
  uint32_t event_index = 0;

  // 1. Проверяем, что вообще есть выходные аудио-шины
  if (process->audio_outputs_count < 1) {
    return CLAP_PROCESS_CONTINUE;
  }

  // 2. Проверяем, что массив указателей на каналы существует
  if (!process->audio_outputs[0].data32) {
    return CLAP_PROCESS_CONTINUE; 
  }

  // 3. Безопасно получаем указатели на левый и правый каналы
  float* out_l = process->audio_outputs[0].data32[0];
  float* out_r = process->audio_outputs[0].data32[1];

  // 4. Проверяем каждый канал отдельно перед обработкой
  // (Если хост по какой-то причине дал только один канал или сбросил указатели)
  if (!out_l || !out_r) {
    return CLAP_PROCESS_CONTINUE;
  }

  // Цикл по каждому сэмплу в текущем аудио-блоке
  for (uint32_t frame = 0; frame < total_frames; ++frame) {

    // 1. ПРОВЕРКА MIDI СОБЫТИЙ
    // Если на текущем сэмпле (frame) есть событие — обрабатываем его
    while (event_index < num_events) {
      const clap_event_header_t* event_header = process->in_events->get(process->in_events, event_index);

      // Если событие произойдет позже текущего сэмпла — выходим из while и обрабатываем его в свой черед
      if (event_header->time > frame) {
        break;
      }

      // Проверяем, что это событие из ядра CLAP (MIDI/Note события)
      if (event_header->type == CLAP_EVENT_NOTE_ON) {
        const clap_event_note_t* note_event =
          (const clap_event_note_t*)event_header;

        if (note_event->velocity == 0.0) {
          instance->gen.noteOff(note_event->key);
        } else {
          instance->gen.noteOn(note_event->key);
        }
      }
      else if (event_header->type == CLAP_EVENT_NOTE_OFF) {
        const clap_event_note_t* note_event =
          (const clap_event_note_t*)event_header;

          instance->gen.noteOff(note_event->key);
      }

      event_index++; // Переходим к следующему событию в очереди
    }

    instance->gen.process();

    // Записываем получившийся сэмпл в левый и правый каналы DAW
    out_l[frame] = instance->gen.getOutput();
    out_r[frame] = instance->gen.getOutput();
  }

  return CLAP_PROCESS_CONTINUE; // Говорим DAW, что мы готовы обрабатывать звук дальше
}

// Вызывается в основном потоке (Main Thread) для фоновых задач плагина
void my_plugin_on_main_thread(const struct clap_plugin *plugin) {}
