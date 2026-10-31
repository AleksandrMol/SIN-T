// Открывает окно редактора настоящего плагина и сохраняет кадр: так внешний вид проверяется без DAW.
// Запуск: sint_editor_preview <кадр.ppm> [--open] [--preset файл] [--scale 1.5] [--resize ШxВ] [--hold секунды]
// --hold держит окно открытым: за это время в него можно отправить события мыши снаружи
// Только для разработки, в плагин не входит.

#include <X11/Xlib.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "../../clap/clap.h"
#include "../../gui/pluginWindow/pluginWindow.h"
#include "../../plugin/plugin.h"

// Окно редактора объявлено в plugin/editor.cpp, а здесь нужен только его PluginWindow
extern PluginWindow& my_plugin_get_editor_window(MyPluginInstance* instance);

namespace {
  clap_id s_timerId = 1;

  const clap_host_timer_support_t s_hostTimer = {
    .register_timer = [](const clap_host_t*, uint32_t, clap_id* id) {
      *id = s_timerId;
      return true;
    },
    .unregister_timer = [](const clap_host_t*, clap_id) {
      return true;
    },
  };

  const clap_host_t s_host = {
    .clap_version = CLAP_VERSION_INIT,
    .host_data = nullptr,
    .name = "sint editor preview",
    .vendor = "",
    .url = "",
    .version = "0",
    .get_extension = [](const clap_host_t*, const char* id) -> const void* {
      return !std::strcmp(id, CLAP_EXT_TIMER_SUPPORT) ? &s_hostTimer : nullptr;
    },
    .request_restart = [](const clap_host_t*) {},
    .request_process = [](const clap_host_t*) {},
    .request_callback = [](const clap_host_t*) {},
  };
}

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "Запуск: sint_editor_preview <кадр.ppm> [--open] [--preset файл]\n");
    return 1;
  }

  bool isVoiceOpen = false;
  double scale = 1.0;
  int holdMilliseconds = 0;
  uint32_t resizeWidth = 0;
  uint32_t resizeHeight = 0;
  std::string presetPath;

  for (int index = 2; index < argc; index++) {
    if (!std::strcmp(argv[index], "--open")) {
      isVoiceOpen = true;
    } else if (!std::strcmp(argv[index], "--preset") && index + 1 < argc) {
      presetPath = argv[++index];
    } else if (!std::strcmp(argv[index], "--scale") && index + 1 < argc) {
      scale = std::atof(argv[++index]);
    } else if (!std::strcmp(argv[index], "--hold") && index + 1 < argc) {
      holdMilliseconds = (int)(std::atof(argv[++index]) * 1000.0);
    } else if (!std::strcmp(argv[index], "--resize") && index + 1 < argc) {
      std::sscanf(argv[++index], "%ux%u", &resizeWidth, &resizeHeight);
    }
  }

  const auto* factory = (const clap_plugin_factory_t*)clap_entry.get_factory(CLAP_PLUGIN_FACTORY_ID);
  const clap_plugin_t* plugin = factory->create_plugin(factory, &s_host, factory->get_plugin_descriptor(factory, 0)->id);

  if (!plugin->init(plugin)) {
    return 1;
  }

  auto* instance = (MyPluginInstance*)plugin;

  if (!presetPath.empty()) {
    std::ifstream file(presetPath, std::ios::binary);
    std::stringstream text;

    text << file.rdbuf();

    if (!my_plug_load_preset(instance, text.str(), "preview")) {
      std::fprintf(stderr, "Пресет не загружен\n");
      return 1;
    }
  }

  const auto* gui = (const clap_plugin_gui_t*)plugin->get_extension(plugin, CLAP_EXT_GUI);
  const auto* timer = (const clap_plugin_timer_support_t*)plugin->get_extension(plugin, CLAP_EXT_TIMER_SUPPORT);

  if (!gui->create(plugin, CLAP_WINDOW_API_X11, false)) {
    std::fprintf(stderr, "Окно не создано\n");
    return 1;
  }

  gui->set_scale(plugin, scale);

  uint32_t width = 0;
  uint32_t height = 0;

  gui->get_size(plugin, &width, &height);

  Display* display = XOpenDisplay(nullptr);

  if (!display) {
    std::fprintf(stderr, "Нет дисплея\n");
    return 1;
  }

  const Window parent = XCreateSimpleWindow(display, DefaultRootWindow(display), 0, 0, width, height, 0, 0, 0);

  XStoreName(display, parent, "SIN-T preview");
  XMapWindow(display, parent);
  XSync(display, False);

  clap_window_t window{};

  window.api = CLAP_WINDOW_API_X11;
  window.x11 = parent;

  if (!gui->set_parent(plugin, &window) || !gui->show(plugin)) {
    std::fprintf(stderr, "Окно не встроено\n");
    return 1;
  }

  const auto pump = [&](int milliseconds) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);

    while (std::chrono::steady_clock::now() < end) {
      timer->on_timer(plugin, s_timerId);
      std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
  };

  pump(400);

  if (isVoiceOpen) {
    for (const auto& [id, module] : instance->model->getModules()) {
      if (module.descriptor->role == MODULE_ROLE::GENERATOR) {
        my_plugin_get_editor_window(instance).openGraph(id);
        break;
      }
    }

    pump(300);
  }

  if (resizeWidth > 0) {
    XResizeWindow(display, parent, resizeWidth, resizeHeight);
    XSync(display, False);
    gui->set_size(plugin, resizeWidth, resizeHeight);
    pump(300);
  }

  pump(holdMilliseconds);

  // Окно рисует только при изменениях: за время простоя не должно появиться ни одного кадра
  PluginWindow& editor = my_plugin_get_editor_window(instance);
  const uint64_t framesBefore = editor.getFrameCount();

  pump(500);
  std::printf("Кадров за 0,5 с простоя: %llu\n", (unsigned long long)(editor.getFrameCount() - framesBefore));

  editor.captureFrame(argv[1]);
  pump(300);

  gui->destroy(plugin);
  plugin->destroy(plugin);
  XDestroyWindow(display, parent);
  XCloseDisplay(display);

  return 0;
}
