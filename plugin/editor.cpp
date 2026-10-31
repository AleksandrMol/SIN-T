#include <cstring>

#include "../gui/pluginWindow/pluginWindow.h"
#include "./plugin.h"
#include "./pluginEditorBackend.h"

// Окно редактора в DAW: расширения clap.gui и clap.timer-support

struct PluginEditor {
  PluginEditorBackend backend;
  PluginWindow window;
  clap_id timerId = CLAP_INVALID_ID;

  explicit PluginEditor(MyPluginInstance* instance) : backend(instance), window(backend) {}
};

namespace {
  constexpr uint32_t TIMER_PERIOD_MS = 16;

  // Меньше окно уже не вмещает панели
  constexpr uint32_t MIN_WIDTH = 800;
  constexpr uint32_t MIN_HEIGHT = 520;

  const char* getNativeApi() {
#ifdef _WIN32
    return CLAP_WINDOW_API_WIN32;
#else
    return CLAP_WINDOW_API_X11;
#endif
  }

  bool gui_is_api_supported(const clap_plugin_t *plugin, const char *api, bool is_floating) {
    return !is_floating && !strcmp(api, getNativeApi());
  }

  bool gui_get_preferred_api(const clap_plugin_t *plugin, const char **api, bool *is_floating) {
    *api = getNativeApi();
    *is_floating = false;

    return true;
  }

  bool gui_create(const clap_plugin_t *plugin, const char *api, bool is_floating) {
    MyPluginInstance* instance = (MyPluginInstance*)plugin;

    if (!gui_is_api_supported(plugin, api, is_floating) || instance->editor) {
      return false;
    }

#ifndef _WIN32
    // На Linux события окна приходят только через таймер хоста: своего цикла событий у плагина нет
    if (!instance->hostTimer) {
      return false;
    }
#endif

    instance->editor = new PluginEditor(instance);

    return true;
  }

  void gui_destroy(const clap_plugin_t *plugin) {
    my_plugin_destroy_editor((MyPluginInstance*)plugin);
  }

  bool gui_set_scale(const clap_plugin_t *plugin, double scale) {
    ((MyPluginInstance*)plugin)->editor->window.setScale(scale);

    return true;
  }

  bool gui_get_size(const clap_plugin_t *plugin, uint32_t *width, uint32_t *height) {
    const PluginWindow& window = ((MyPluginInstance*)plugin)->editor->window;

    *width = window.getWidth();
    *height = window.getHeight();

    return true;
  }

  bool gui_can_resize(const clap_plugin_t *plugin) {
    return true;
  }

  bool gui_get_resize_hints(const clap_plugin_t *plugin, clap_gui_resize_hints_t *hints) {
    hints->can_resize_horizontally = true;
    hints->can_resize_vertically = true;
    hints->preserve_aspect_ratio = false;
    hints->aspect_ratio_width = 0;
    hints->aspect_ratio_height = 0;

    return true;
  }

  bool gui_adjust_size(const clap_plugin_t *plugin, uint32_t *width, uint32_t *height) {
    *width = *width < MIN_WIDTH ? MIN_WIDTH : *width;
    *height = *height < MIN_HEIGHT ? MIN_HEIGHT : *height;

    return true;
  }

  bool gui_set_size(const clap_plugin_t *plugin, uint32_t width, uint32_t height) {
    return ((MyPluginInstance*)plugin)->editor->window.setSize(width, height);
  }

  bool gui_set_parent(const clap_plugin_t *plugin, const clap_window_t *window) {
    MyPluginInstance* instance = (MyPluginInstance*)plugin;

#ifdef _WIN32
    const uintptr_t parent = (uintptr_t)window->win32;
#else
    const uintptr_t parent = (uintptr_t)window->x11;
#endif

    if (!instance->editor->window.attach(parent)) {
      return false;
    }

    if (instance->hostTimer) {
      return instance->hostTimer->register_timer(instance->host, TIMER_PERIOD_MS, &instance->editor->timerId);
    }

    instance->editor->window.startOwnTimer();

    return true;
  }

  bool gui_set_transient(const clap_plugin_t *plugin, const clap_window_t *window) {
    return false;
  }

  void gui_suggest_title(const clap_plugin_t *plugin, const char *title) {}

  bool gui_show(const clap_plugin_t *plugin) {
    return ((MyPluginInstance*)plugin)->editor->window.show();
  }

  bool gui_hide(const clap_plugin_t *plugin) {
    return ((MyPluginInstance*)plugin)->editor->window.hide();
  }

  void timer_on_timer(const clap_plugin_t *plugin, clap_id timer_id) {
    MyPluginInstance* instance = (MyPluginInstance*)plugin;

    if (instance->editor && instance->editor->timerId == timer_id) {
      instance->editor->window.tick();
    }
  }

  const clap_plugin_gui_t s_gui = {
    .is_api_supported = gui_is_api_supported,
    .get_preferred_api = gui_get_preferred_api,
    .create = gui_create,
    .destroy = gui_destroy,
    .set_scale = gui_set_scale,
    .get_size = gui_get_size,
    .can_resize = gui_can_resize,
    .get_resize_hints = gui_get_resize_hints,
    .adjust_size = gui_adjust_size,
    .set_size = gui_set_size,
    .set_parent = gui_set_parent,
    .set_transient = gui_set_transient,
    .suggest_title = gui_suggest_title,
    .show = gui_show,
    .hide = gui_hide,
  };

  const clap_plugin_timer_support_t s_timer = {
    .on_timer = timer_on_timer,
  };
}

const void* my_plugin_get_editor_extension(const char *id) {
  if (!strcmp(id, CLAP_EXT_GUI)) {
    return &s_gui;
  }

  if (!strcmp(id, CLAP_EXT_TIMER_SUPPORT)) {
    return &s_timer;
  }

  return nullptr;
}

PluginWindow& my_plugin_get_editor_window(MyPluginInstance *instance) {
  return instance->editor->window;
}

void my_plugin_destroy_editor(MyPluginInstance *instance) {
  if (!instance->editor) {
    return;
  }

  if (instance->editor->timerId != CLAP_INVALID_ID) {
    instance->hostTimer->unregister_timer(instance->host, instance->editor->timerId);
  }

  delete instance->editor;
  instance->editor = nullptr;
}
