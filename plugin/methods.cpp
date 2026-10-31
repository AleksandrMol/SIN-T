#include <stdlib.h>
#include <assert.h>
#include <math.h>

#include "../clap/plugin.h"
#include "../engine/defaultPreset/defaultPreset.h"
#include "../engine/moduleCatalog/moduleCatalog.h"
#include "./plugin.h"

// Колбэк инициализации конкретного инстанса плагина
bool my_plugin_init(const struct clap_plugin *plugin) {
  MyPluginInstance* instance = (MyPluginInstance*)plugin;

  if (!addAllModules(instance->registry)) {
    return false;
  }

  auto model = createDefaultModel(&instance->registry);

  if (!model) {
    return false;
  }

  instance->model = std::move(*model);
  instance->loadedModel = instance->model;
  instance->hostParams = (const clap_host_params_t*)instance->host->get_extension(instance->host, CLAP_EXT_PARAMS);
  instance->hostPresetLoad = (const clap_host_preset_load_t*)instance->host->get_extension(instance->host, CLAP_EXT_PRESET_LOAD);

  instance->slots.sync(*instance->model);
  instance->engine.setSlotTargets(instance->slots.getTargets());
  instance->engine.applyModel(*instance->model);

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
  instance->engine.prepare((float)sample_rate);
  instance->engine.reset(); // Голоса и состояния прошлого запуска или прошлого пресета не должны звучать
  instance->engine.setLive(true); // Теперь process может идти параллельно с главным потоком
  instance->isActive = true;

  return true;
}

void my_plugin_deactivate(const struct clap_plugin *plugin) {
  MyPluginInstance* instance = (MyPluginInstance*)plugin;

  instance->engine.setLive(false);
  instance->isActive = false;
}

void my_plug_reset(const struct clap_plugin *plugin) {
  MyPluginInstance* instance = (MyPluginInstance*)plugin;

  instance->engine.reset();
}

// Начинает или останавливает обработку звука (вызывается перед стартом воспроизведения)
bool my_plugin_start_processing(const struct clap_plugin *plugin) {
  return true;
}
void my_plugin_stop_processing(const struct clap_plugin *plugin) {}

// Вызывается в основном потоке (Main Thread) для фоновых задач плагина
void my_plugin_on_main_thread(const struct clap_plugin *plugin) {
  MyPluginInstance* instance = (MyPluginInstance*)plugin;

  my_plug_sync_slot_values(instance);
  instance->engine.update();
}
