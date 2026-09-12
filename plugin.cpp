#include "clap/plugin.h"
#include <cstddef>
#include <string.h> 
#include <stdlib.h> 
#include <stdio.h> 
#include <assert.h> 
#include <math.h> 
#include "clap/entry.h"
#include "clap/factory/plugin-factory.h"
#include "clap/plugin-features.h"
#include "clap/version.h"
#include "tools/console.h"

#include "clap/clap.h"

#include "./tools/tools.h"

// ------------
// Буду каждый плагин начинать с ID плагина.
const char* MY_PLUGIN_ID = "tutorialPlugin.mycompany.helloclap";

// Далее идёт структура с описанием плагина - Дескриптор
static const clap_plugin_descriptor s_my_plugin_desc = {
  .clap_version = CLAP_VERSION_INIT, // Версия плагина берётся из макроса
  .id = MY_PLUGIN_ID, // ID из константы выше
  .name = "SIN-T", // Имя плагина
  .vendor = "ChillySwim", // Имя разработчика
  .url = "URL",
  .version = "1.0.0",
  .description = "Desc",

  // Теги для DAW, чтобы DAW понимала тип плагина
  .features = (const char *[]) {
    CLAP_PLUGIN_FEATURE_INSTRUMENT, // Инструмент
    CLAP_PLUGIN_FEATURE_SYNTHESIZER, // Аудио эффект
    CLAP_PLUGIN_FEATURE_STEREO, // Для работы со стерео звуком
    NULL
  },
};
// ------------

// ------------
// Объект плагина
// Шаг 1. Объявление собственной структуры плагина.
struct MyPluginInstance {
  // Первым полем ВСЕГДА И ОБЯЗАТЕЛЬНО идёт стандартный плагин CLAP
  clap_plugin plugin;

  // Далее можно указывать любые свои собственные переменные
  const clap_host_t* host; // Указатель на DAW
  float sample_rate; // Частота дискретизации

  bool is_note_on;      // Зажата ли сейчас нота?
  int32_t active_note;  // Номер MIDI ноты (0-127)
  float phase;          // Текущая фаза осциллятора (от 0.0 до 1.0)
  float phase_step;     // На сколько сдвигать фазу с каждым сэмплом (зависит от частоты ноты)
};
// ------------

// ------------
// Реализация методов плагина
// Колбэк инициализации конкретного инстанса плагина
static bool my_plugin_init(const struct clap_plugin *plugin) {
  console.log("my_plugin_init");
  // Приводим указатель к нашему типу, чтобы получить доступ к стейту
  MyPluginInstance* instance = (MyPluginInstance*)plugin;
  
  // Тут можно выделить память под внутренние нужды плагина, если необходимо
  return true;
}

// Колбэк деинициализации (деструктор) — вызывается, когда плагин удаляют с дорожки
static void my_plugin_destroy(const struct clap_plugin *plugin) {
  console.log("my_plugin_destroy");
  MyPluginInstance* instance = (MyPluginInstance*)plugin;
  
  // Освобождаем память, которую занимает сам объект плагина.
  // free() — это стандартная функция C для очистки памяти, выделенной через malloc().
  free(instance);
}

// Вызывается хостом сразу после init, сообщает аудио-параметры среды
static bool my_plugin_activate(
  const struct clap_plugin *plugin,
  double sample_rate,
  uint32_t min_frames_count,
  uint32_t max_frames_count
) {
  console.log("my_plugin_activate");
  MyPluginInstance* instance = (MyPluginInstance*)plugin;
  instance->sample_rate = (float)sample_rate; // Сохраняем частоту дискретизации
  return true;
}

static void my_plugin_deactivate(const struct clap_plugin *plugin) {
  console.log("my_plugin_deactivate");
}

// Начинает или останавливает обработку звука (вызывается перед стартом воспроизведения)
static bool my_plugin_start_processing(const struct clap_plugin *plugin) {
  console.log("my_plugin_start_processing");
  return true;
}
static void my_plugin_stop_processing(const struct clap_plugin *plugin) {
  console.log("my_plugin_stop_processing");
}

// Сюда DAW будет заходить в аудио-потоке, чтобы плагин обработал звук.
// Самое «горячее» место в коде (Real-time thread). К нему мы вернемся отдельно.
static clap_process_status my_plugin_process(
  const struct clap_plugin *plugin, 
  const clap_process_t *process
) {
  MyPluginInstance* instance = (MyPluginInstance*)plugin;

  const uint32_t total_frames = process->frames_count;
  const uint32_t num_events = process->in_events->size(process->in_events);
  uint32_t event_index = 0;

  // Получаем указатели на левый (0) и правый (1) аудио-каналы DAW
  float* out_l = process->audio_outputs[0].data32[0];
  float* out_r = process->audio_outputs[0].data32[1];
  
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
      if (event_header->space_id == CLAP_CORE_EVENT_SPACE_ID) {
        // Нажатие клавиши (Note On)
        if (event_header->type == CLAP_EVENT_NOTE_ON) {
          const clap_event_note_t* note_event = (const clap_event_note_t*)event_header;
          instance->is_note_on = true;
          instance->active_note = note_event->key;

          // Вычисляем, как быстро будет двигаться фаза синусоиды для этой ноты
          float freq = midiToFreq(note_event->key);
          instance->phase_step = freq / instance->sample_rate;
        }

        // Отпускание клавиши (Note Off)
        else if (event_header->type == CLAP_EVENT_NOTE_OFF) {
          const clap_event_note_t* note_event = (const clap_event_note_t*)event_header;
          // Выключаем звук только если отпустили именно ту ноту, которая сейчас звучит
          if (note_event->key == instance->active_note) {
            instance->is_note_on = false;
          }
        }
      }

      event_index++; // Переходим к следующему событию в очереди
    }

    // 2. ГЕНЕРАЦИЯ ЗВУКА
    float sample_value = 0.0f;

    if (instance->is_note_on) {
      // Математика синусоиды: sin(2 * PI * фаза)
      // Умножаем на 0.2f, чтобы сделать звук потише и не оглушить тебя при тесте
      sample_value = sinf(2.0f * M_PI * instance->phase) * 0.2f;
      
      // Сдвигаем фазу для следующего сэмпла
      instance->phase += instance->phase_step;
      
      // Удерживаем фазу в пределах от 0.0 до 1.0, чтобы избежать переполнения float
      if (instance->phase >= 1.0f) {
        instance->phase -= 1.0f;
      }
    } else {
      // Если нота не горит — плавно возвращаем фазу в ноль, чтобы не было щелчков при следующем нажатии
      instance->phase = 0.0f;
    }

    // Записываем получившийся сэмпл в левый и правый каналы DAW
    out_l[frame] = sample_value;
    out_r[frame] = sample_value;
  }

  return CLAP_PROCESS_CONTINUE; // Говорим DAW, что мы готовы обрабатывать звук дальше
}

// Возвращает указатели на расширения (Extensions). CLAP модульный: 
// поддержка GUI, параметров, MIDI — всё это отдельные расширения. Пока возвращаем NULL.
static const void* my_plugin_get_extension(const struct clap_plugin *plugin, const char *id) {
  console.log("my_plugin_get_extension ", id);
  return NULL;
}

// Вызывается в основном потоке (Main Thread) для фоновых задач плагина
static void my_plugin_on_main_thread(const struct clap_plugin *plugin) {
  console.log("my_plugin_on_main_thread");
}
// ------------

// ------------
// Фабрика плагина
// Нужна для того, чтобы DAW могла создать экземпляр плагина для себя, так как код внутри библиотеки изолирован от DAW
// Шаг 1. объявляем функции, которые будут внутри фабрики

// Возвращает количество плагинов
static uint32_t plugin_factory_get_plugin_count (const struct clap_plugin_factory *factory) {
  console.log("plugin_factory_get_plugin_count");
  return 1; // Сейчас у нас один плагин
};

// Возвращает дескриптор плагина по его индексу.
static const clap_plugin_descriptor* plugin_factory_get_plugin_descriptor (
  const struct clap_plugin_factory *factory,
  uint32_t index
) {
  console.log("plugin_factory_get_plugin_descriptor");
  if (index == 0) {
    return &s_my_plugin_desc; // Возвращаем указатель на наш дескриптор из Шага 4
  }
  return NULL;
};

// Самый важный метод: DAW вызывает его, чтобы создать реальный инстанс плагина на дорожке
static const clap_plugin* plugin_factory_create_plugin(
  const struct clap_plugin_factory *factory,
  const clap_host_t *host,
  const char *plugin_id
) {
  console.log("plugin_factory_create_plugin");
  if (strcmp(plugin_id, MY_PLUGIN_ID) != 0) return NULL;

  MyPluginInstance* instance = (MyPluginInstance*)malloc(sizeof(MyPluginInstance));
  if (!instance) return NULL;
  memset(instance, 0, sizeof(MyPluginInstance));

  instance->host = host;

  instance->plugin.desc = &s_my_plugin_desc;
  instance->plugin.plugin_data = instance;
  
  instance->plugin.init = my_plugin_init;
  instance->plugin.destroy = my_plugin_destroy;
  instance->plugin.activate = my_plugin_activate;
  instance->plugin.deactivate = my_plugin_deactivate;
  instance->plugin.start_processing = my_plugin_start_processing;
  instance->plugin.stop_processing = my_plugin_stop_processing;
  instance->plugin.process = my_plugin_process;
  instance->plugin.get_extension = my_plugin_get_extension;
  instance->plugin.on_main_thread = my_plugin_on_main_thread;

  return &instance->plugin;
}

// Шаг 2. собираем функции в структуру фабрики
static const clap_plugin_factory s_my_plugin_factory = {
  .get_plugin_count = plugin_factory_get_plugin_count,
  .get_plugin_descriptor = plugin_factory_get_plugin_descriptor,
  .create_plugin = plugin_factory_create_plugin,
};

// Экспорт функций в DAW с clap_plugin_entry
// Объявляем функцию, которую DAW сможет увидеть снаружи нашей библиотеки
extern "C" const clap_plugin_entry clap_entry = {
  .clap_version = CLAP_VERSION_INIT,
  .init = [](const char *plugin_path) -> bool {
    // Это фунция вызывается один раз при загрузке плагина в DAW
    // тут можно инициализировать глобальные ресурсы, если они нужны
    return true;
  },
  .deinit = []() {
    // Деинициализация плагина
    // Эта функция вызывается перед выгрузкой плагина из памяти DAW
    // Тут стоит освобождать всё, что захватили в init
  },
  .get_factory = [](const char *factory_id) -> const void* {
    // Проверяем, запрашивает ли DAW стандартную фабрику плагинов
    if (strcmp(factory_id, CLAP_PLUGIN_FACTORY_ID) == 0) {
      return &s_my_plugin_factory; // Возвращаем адрес нашей фабрики
    }
    return NULL;
  },

};
// ------------
