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

// Вызывается в основном потоке (Main Thread) для фоновых задач плагина
void my_plugin_on_main_thread(const struct clap_plugin *plugin) {}
