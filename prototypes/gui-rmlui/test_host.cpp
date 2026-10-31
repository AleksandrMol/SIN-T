// Минимальный тестовый CLAP-хост для Linux: открывает окна плагина, кликает, тянет блок,
// закрывает и открывает окно заново, печатает расход памяти. Настоящую DAW не заменяет.
//
// Запуск: test_host <путь к .clap> <папка для кадров>

#include <X11/Xlib.h>
#include <dlfcn.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include "clap/clap.h"

struct Timer {
  clap_id id;
  uint32_t periodMs;
};

static std::vector<Timer> s_timers;

static bool registerTimer(const clap_host_t *, uint32_t periodMs, clap_id *id) {
  *id = (clap_id)s_timers.size() + 1;
  s_timers.push_back({*id, periodMs});
  return true;
}

static bool unregisterTimer(const clap_host_t *, clap_id id) {
  std::erase_if(s_timers, [id](const Timer &timer) { return timer.id == id; });
  return true;
}

static const clap_host_timer_support_t s_hostTimer = {.register_timer = registerTimer, .unregister_timer = unregisterTimer};

static const void *hostGetExtension(const clap_host_t *, const char *id) {
  return strcmp(id, CLAP_EXT_TIMER_SUPPORT) == 0 ? &s_hostTimer : nullptr;
}

static const clap_host_t s_host = {
  .clap_version = CLAP_VERSION_INIT,
  .host_data = nullptr,
  .name = "sint test host",
  .vendor = "",
  .url = "",
  .version = "0",
  .get_extension = hostGetExtension,
  .request_restart = [](const clap_host_t *) {},
  .request_process = [](const clap_host_t *) {},
  .request_callback = [](const clap_host_t *) {},
};

struct Instance {
  const clap_plugin_t *plugin;
  const clap_plugin_gui_t *gui;
  const clap_plugin_timer_support_t *timer;
  Window parent;
};

static long rssKb() {
  std::ifstream file("/proc/self/status");
  std::string line;

  while (std::getline(file, line)) {
    if (line.rfind("VmRSS:", 0) == 0) {
      return atol(line.c_str() + 6);
    }
  }

  return 0;
}

static void pump(std::vector<Instance> &instances, int ms) {
  const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);

  while (std::chrono::steady_clock::now() < end) {
    for (Instance &instance : instances) {
      for (const Timer &timer : std::vector<Timer>(s_timers)) {
        instance.timer->on_timer(instance.plugin, timer.id);
      }
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(16));
  }
}

static Window childOf(Display *display, Window parent) {
  Window root, returnedParent, *children = nullptr;
  unsigned int count = 0;
  XQueryTree(display, parent, &root, &returnedParent, &children, &count);
  Window child = count > 0 ? children[0] : 0;
  XFree(children);
  return child;
}

static void sendMotion(Display *display, Window window, int x, int y, unsigned int state) {
  XEvent event{};
  event.xmotion.type = MotionNotify;
  event.xmotion.window = window;
  event.xmotion.x = x;
  event.xmotion.y = y;
  event.xmotion.state = state;
  XSendEvent(display, window, False, 0, &event);
  XFlush(display);
}

static void sendButton(Display *display, Window window, int x, int y, bool press) {
  XEvent event{};
  event.xbutton.type = press ? ButtonPress : ButtonRelease;
  event.xbutton.window = window;
  event.xbutton.x = x;
  event.xbutton.y = y;
  event.xbutton.button = Button1;
  XSendEvent(display, window, False, 0, &event);
  XFlush(display);
}

static void openGui(Display *display, Instance &instance) {
  const clap_window_t window = {.api = CLAP_WINDOW_API_X11, .x11 = instance.parent};

  if (!instance.gui->create(instance.plugin, CLAP_WINDOW_API_X11, false) || !instance.gui->set_parent(instance.plugin, &window) || !instance.gui->show(instance.plugin)) {
    fprintf(stderr, "FAIL: не удалось открыть GUI\n");
    exit(1);
  }

  XFlush(display);
}

int main(int argc, char **argv) {
  if (argc < 3) {
    fprintf(stderr, "usage: test_host <plugin.clap> <dump dir>\n");
    return 2;
  }

  const std::string dumpDir = argv[2];
  const std::string dumpPath = dumpDir + "/frame.ppm";
  setenv("SINT_PROTO_DUMP", dumpPath.c_str(), 1);

  Display *display = XOpenDisplay(nullptr);

  if (!display) {
    fprintf(stderr, "FAIL: нет X11 display\n");
    return 1;
  }

  const long rssStart = rssKb();

  void *library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);

  if (!library) {
    fprintf(stderr, "FAIL: %s\n", dlerror());
    return 1;
  }

  auto *entry = (const clap_plugin_entry_t *)dlsym(library, "clap_entry");
  entry->init(argv[1]);
  auto *factory = (const clap_plugin_factory_t *)entry->get_factory(CLAP_PLUGIN_FACTORY_ID);
  const char *id = factory->get_plugin_descriptor(factory, 0)->id;

  const auto makeInstance = [&](int x) {
    Instance instance{};
    instance.plugin = factory->create_plugin(factory, &s_host, id);
    instance.plugin->init(instance.plugin);
    instance.gui = (const clap_plugin_gui_t *)instance.plugin->get_extension(instance.plugin, CLAP_EXT_GUI);
    instance.timer = (const clap_plugin_timer_support_t *)instance.plugin->get_extension(instance.plugin, CLAP_EXT_TIMER_SUPPORT);
    instance.parent = XCreateSimpleWindow(display, DefaultRootWindow(display), x, 0, 900, 560, 0, 0, 0);
    XMapWindow(display, instance.parent);
    XSync(display, False);
    openGui(display, instance);
    return instance;
  };

  std::vector<Instance> instances;
  instances.push_back(makeInstance(0));
  pump(instances, 800);

  const long rssOne = rssKb();
  system(("cp /proc/" + std::to_string(getpid()) + "/smaps " + dumpDir + "/smaps.txt").c_str());
  printf("RSS: хост %ld КБ, с одним окном %ld КБ (+%ld КБ)\n", rssStart, rssOne, rssOne - rssStart);

  Window view = childOf(display, instances[0].parent);

  // Клик по кнопке
  sendMotion(display, view, 100, 518, 0);
  pump(instances, 100);
  sendButton(display, view, 100, 518, true);
  pump(instances, 100);
  sendButton(display, view, 100, 518, false);
  pump(instances, 400);
  system(("cp " + dumpPath + " " + dumpDir + "/after-click.ppm").c_str());

  // Перетаскивание блока Oscillator, провод должен последовать за ним
  sendMotion(display, view, 150, 160, 0);
  pump(instances, 100);
  sendButton(display, view, 150, 160, true);
  pump(instances, 100);

  for (int step = 1; step <= 20; step++) {
    sendMotion(display, view, 150 + step * 5, 160 + step * 12, Button1Mask);
    pump(instances, 20);
  }

  sendButton(display, view, 250, 400, false);
  pump(instances, 400);
  system(("cp " + dumpPath + " " + dumpDir + "/after-drag.ppm").c_str());

  // Второй экземпляр одновременно с первым
  instances.push_back(makeInstance(40));
  pump(instances, 800);
  const long rssTwo = rssKb();
  printf("RSS: два окна %ld КБ (второе +%ld КБ)\n", rssTwo, rssTwo - rssOne);

  // Закрыть и открыть окно много раз: проверка утечек и освобождения ресурсов
  const int reopens = argc > 3 ? atoi(argv[3]) : 20;

  for (int i = 0; i < reopens; i++) {
    instances[1].gui->destroy(instances[1].plugin);
    pump(instances, 30);
    openGui(display, instances[1]);
    pump(instances, 60);
  }

  pump(instances, 500);
  system(("cp " + dumpPath + " " + dumpDir + "/after-reopen.ppm").c_str());
  const long rssReopen = rssKb();
  printf("RSS: после %d переоткрытий %ld КБ (%+ld КБ)\n", reopens, rssReopen, rssReopen - rssTwo);

  // Второе окно закрывается, первое должно продолжить рисовать текст (шрифты общие)
  instances[1].gui->destroy(instances[1].plugin);
  remove(dumpPath.c_str());
  std::vector<Instance> first(instances.begin(), instances.begin() + 1);
  pump(first, 500);
  system(("cp " + dumpPath + " " + dumpDir + "/first-alone.ppm").c_str());
  instances[1].plugin->destroy(instances[1].plugin);
  XDestroyWindow(display, instances[1].parent);
  instances.resize(1);

  for (Instance &instance : instances) {
    instance.gui->destroy(instance.plugin);
    instance.plugin->destroy(instance.plugin);
    XDestroyWindow(display, instance.parent);
  }

  entry->deinit();
  dlclose(library);
  XCloseDisplay(display);

  printf("OK\n");
  return 0;
}
