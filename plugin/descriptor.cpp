#include "../clap/plugin.h"

#include "./plugin.h"

// ID плагина.
extern const char* const MY_PLUGIN_ID = "com.chillyswim.sin-t";

// Теги для DAW, чтобы DAW понимала тип плагина
static const char* const s_my_plugin_features[] = {
  CLAP_PLUGIN_FEATURE_INSTRUMENT, // Инструмент
  CLAP_PLUGIN_FEATURE_SYNTHESIZER, // Синтезатор
  CLAP_PLUGIN_FEATURE_STEREO, // Для работы со стерео звуком
  NULL
};

// Cтруктура с описанием плагина - Дескриптор
extern const clap_plugin_descriptor s_my_plugin_desc = {
  .clap_version = CLAP_VERSION_INIT, // Версия плагина берётся из макроса
  .id = MY_PLUGIN_ID, // ID из константы выше
  .name = "SIN-T", // Имя плагина
  .vendor = "ChillySwim", // Имя разработчика
  .url = "",
  .manual_url = "",
  .support_url = "",
  .version = "1.0.0",
  .description = "Modular software synthesizer",
  .features = s_my_plugin_features,
};
