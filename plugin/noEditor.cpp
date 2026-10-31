#include "./plugin.h"

// Сборки без окна: офлайн-рендер, тесты и плагин с -DSINT_BUILD_GUI=OFF. Им не нужны X11 и OpenGL

const void* my_plugin_get_editor_extension(const char *id) {
  return nullptr;
}

void my_plugin_destroy_editor(MyPluginInstance *instance) {}
