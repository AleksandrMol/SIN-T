#include <cstring>

#include "../clap/entry.h"
#include "../clap/factory/plugin-factory.h"

#include "./plugin.h"

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