#include <cstring>

#include "../clap/plugin.h"
#include "../clap/factory/plugin-factory.h"
#include "../tools/console.h"

#include "./plugin.h"

// Фабрика плагина
// Нужна для того, чтобы DAW могла создать экземпляр плагина для себя, так как код внутри библиотеки изолирован от DAW

// Возвращает количество плагинов
static uint32_t plugin_factory_get_plugin_count (const struct clap_plugin_factory *factory) {
  console.log("plugin_factory_get_plugin_count");
  return 1; // Сейчас у меня один плагин
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

// Фабрика
extern const clap_plugin_factory s_my_plugin_factory = {
  .get_plugin_count = plugin_factory_get_plugin_count,
  .get_plugin_descriptor = plugin_factory_get_plugin_descriptor,
  .create_plugin = plugin_factory_create_plugin,
};