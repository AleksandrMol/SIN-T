#include "../clap/plugin.h"
#include "../tools/console.h"

#include "./plugin.h"

// ID плагина.
extern const char* const MY_PLUGIN_ID = "tutorialPlugin.mycompany.helloclap";

// Cтруктура с описанием плагина - Дескриптор
extern const clap_plugin_descriptor s_my_plugin_desc = {
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
