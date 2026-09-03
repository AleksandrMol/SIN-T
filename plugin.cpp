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

#include "clap/clap.h"

// ------------
// Буду каждый плагин начинать с ID плагина.
const char* MY_PLUGIN_ID = "tutorialPlugin.mycompany.helloclap";

// Далее идёт структура с описанием плагина - Дескриптор
static const clap_plugin_descriptor s_my_plugin_desc = {
  .clap_version = CLAP_VERSION_INIT, // Версия плагина берётся из макроса
  .id = MY_PLUGIN_ID, // ID из константы выше
  .name = "Tutorial plugin", // Имя плагина 
  .vendor = "ChillySwim", // Имя разработчика
  .url = "URL",
  .version = "1.0.0",
  .description = "Desc",

  // Теги для DAW, чтобы DAW понимала тип плагина
  .features = (const char *[]) {
    CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, // Аудио эффект
    CLAP_PLUGIN_FEATURE_STEREO, // для работы со стерео звуком
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

  // Можно добавить параметры плагина
};
// ------------

// ------------
// Реализация методов плагина
// Колбэк инициализации конкретного инстанса плагина
static bool my_plugin_init(const struct clap_plugin *plugin) {
  // Приводим указатель к нашему типу, чтобы получить доступ к стейту
  MyPluginInstance* instance = (MyPluginInstance*)plugin;
  
  // Тут можно выделить память под внутренние нужды плагина, если необходимо
  return true;
}

// Колбэк деинициализации (деструктор) — вызывается, когда плагин удаляют с дорожки
static void my_plugin_destroy(const struct clap_plugin *plugin) {
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
  MyPluginInstance* instance = (MyPluginInstance*)plugin;
  instance->sample_rate = (float)sample_rate; // Сохраняем частоту дискретизации
  return true;
}

static void my_plugin_deactivate(const struct clap_plugin *plugin) {}

// Начинает или останавливает обработку звука (вызывается перед стартом воспроизведения)
static bool my_plugin_start_processing(const struct clap_plugin *plugin) { return true; }
static void my_plugin_stop_processing(const struct clap_plugin *plugin) {}

// Сюда DAW будет заходить в аудио-потоке, чтобы плагин обработал звук.
// Самое «горячее» место в коде (Real-time thread). К нему мы вернемся отдельно.
static clap_process_status my_plugin_process(
  const struct clap_plugin *plugin, 
  const clap_process_t *process
) {
  return CLAP_PROCESS_CONTINUE; // Говорим DAW, что мы готовы обрабатывать звук дальше
}

// Возвращает указатели на расширения (Extensions). CLAP модульный: 
// поддержка GUI, параметров, MIDI — всё это отдельные расширения. Пока возвращаем NULL.
static const void* my_plugin_get_extension(const struct clap_plugin *plugin, const char *id) {
  return NULL;
}

// Вызывается в основном потоке (Main Thread) для фоновых задач плагина
static void my_plugin_on_main_thread(const struct clap_plugin *plugin) {}
// ------------

// ------------
// Фабрика плагина
// Нужна для того, чтобы DAW могла создать экземпляр плагина для себя, так как код внутри библиотеки изолирован от DAW
// Шаг 1. объявляем функции, которые будут внутри фабрики

// Возвращает количество плагинов
static uint32_t plugin_factory_get_plugin_count (const struct clap_plugin_factory *factory) {
  return 1; // Сейчас у нас один плагин
};

// Возвращает дескриптор плагина по его индексу.
static const clap_plugin_descriptor* plugin_factory_get_plugin_descriptor (
  const struct clap_plugin_factory *factory,
  uint32_t index
) {
  if (index == 0) {
    return &s_my_plugin_desc; // Возвращаем указатель на наш дескриптор из Шага 4
  }
  return NULL;
};

// Самый важный метод: DAW вызывает его, чтобы создать реальный инстанс плагина на дорожке
static const clap_plugin* plugin_factory_create_plugin(
  const struct clap_plugin_factory *factory,
  const clap_host_t *host, // Указатель на интерфейс самой DAW (хоста)
  const char *plugin_id
) {
  // Проверяем, что DAW запрашивает именно наш плагин
  if (strcmp(plugin_id, MY_PLUGIN_ID) != 0) {
    return NULL; 
  }

  // Пока возвращаем NULL, чтобы код компилировался.
  return NULL; 
};

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
  .get_factory = [](const char *factory_id) -> const void* {
    // Проверяем, запрашивает ли DAW стандартную фабрику плагинов
    if (strcmp(factory_id, CLAP_PLUGIN_FACTORY_ID) == 0) {
      return &s_my_plugin_factory; // Возвращаем адрес нашей фабрики
    }
    return NULL;
  },
  .deinit = []() {
    // Деинициализация плагина
    // Эта функция вызывается перед выгрузкой плагина из памяти DAW
    // Тут стоит освобождать всё, что захватили в init
  }
};
// ------------

