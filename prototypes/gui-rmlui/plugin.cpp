// Прототип GUI: CLAP-плагин без звука, окно на pugl, интерфейс на RmlUi.
// Проверяет встраивание в окно хоста, ввод мышью, свой элемент (провод) и расход памяти.

#include <RmlUi/Core.h>
#include <RmlUi_Renderer_GL3.h>
#include <pugl/gl.h>
#include <pugl/pugl.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "clap/clap.h"

extern const unsigned char embeddedFont[];
extern const unsigned int embeddedFontSize;

static constexpr uint32_t WINDOW_WIDTH = 900;
static constexpr uint32_t WINDOW_HEIGHT = 560;

static const char *const DOCUMENT = R"(
<rml>
<head>
<title>SIN-T GUI prototype</title>
<style>
  body, div { display: block; }
  body {
    width: 100%; height: 100%; background-color: #16181d;
    font-family: "Lato Latin"; font-size: 16px; color: #e6e8ec;
  }
  wire-layer { display: block; position: absolute; left: 0; top: 0; width: 100%; height: 100%; pointer-events: none; }
  .block {
    position: absolute; width: 170px; height: 96px; padding: 10px; box-sizing: border-box;
    background-color: #2a2f3a; border: 1px #4d5668; border-radius: 8px; drag: drag;
  }
  .block:hover { border-color: #7aa2f7; }
  .port { position: absolute; width: 14px; height: 14px; top: 40px; border-radius: 7px; background-color: #7aa2f7; }
  .out { right: -7px; }
  .in { left: -7px; }
  #title { position: absolute; left: 24px; top: 18px; font-size: 22px; }
  #button {
    position: absolute; left: 24px; top: 500px; width: 150px; height: 36px; line-height: 36px;
    text-align: center; background-color: #3b4252; border-radius: 6px; cursor: pointer;
  }
  #button:hover { background-color: #4c566a; }
  #button:active { background-color: #7aa2f7; color: #16181d; }
  #count { position: absolute; left: 196px; top: 508px; }
</style>
</head>
<body>
  <wire-layer id="wires"/>
  <div id="title">SIN-T / RmlUi + pugl</div>
  <div class="block" id="osc" style="left: 80px; top: 120px;">Oscillator<div class="port out" id="osc-out"></div></div>
  <div class="block" id="filter" style="left: 400px; top: 280px;">Filter<div class="port in" id="filter-in"></div></div>
  <div id="button">Click me</div>
  <div id="count">Clicks: 0</div>
</body>
</rml>
)";

/**
 * Свой элемент RmlUi: рисует провод между двумя портами кривой Безье
 */
class WireLayer : public Rml::Element {
  public:
    using Rml::Element::Element;

  protected:
    void OnRender() override {
      Rml::ElementDocument *document = this->GetOwnerDocument();
      Rml::Element *from = document->GetElementById("osc-out");
      Rml::Element *to = document->GetElementById("filter-in");

      if (!from || !to) {
        return;
      }

      const Rml::Vector2f start = from->GetAbsoluteOffset(Rml::BoxArea::Border) + from->GetBox().GetSize(Rml::BoxArea::Border) * 0.5f;
      const Rml::Vector2f end = to->GetAbsoluteOffset(Rml::BoxArea::Border) + to->GetBox().GetSize(Rml::BoxArea::Border) * 0.5f;

      Rml::Geometry geometry = this->GetRenderManager()->MakeGeometry(this->makeWire(start, end));
      geometry.Render(Rml::Vector2f(0.0f, 0.0f));
    }

  private:
    /**
     * Лента вдоль кривой из четырёх рядов вершин: края прозрачные, поэтому линия сглажена
     */
    Rml::Mesh makeWire(Rml::Vector2f start, Rml::Vector2f end) {
      static constexpr int SEGMENTS = 48;
      static constexpr int ROWS = 4;
      static constexpr float OFFSETS[ROWS] = {-2.5f, -1.5f, 1.5f, 2.5f};
      static constexpr float ALPHA[ROWS] = {0.0f, 1.0f, 1.0f, 0.0f};

      const float bend = std::max(60.0f, std::abs(end.x - start.x) * 0.5f);
      const Rml::Vector2f control1 = start + Rml::Vector2f(bend, 0.0f);
      const Rml::Vector2f control2 = end - Rml::Vector2f(bend, 0.0f);

      Rml::Mesh mesh;

      for (int i = 0; i <= SEGMENTS; i++) {
        const float t = (float)i / SEGMENTS;
        const float u = 1.0f - t;
        const Rml::Vector2f point = start * (u * u * u) + control1 * (3.0f * u * u * t) + control2 * (3.0f * u * t * t) + end * (t * t * t);
        const Rml::Vector2f tangent = (control1 - start) * (3.0f * u * u) + (control2 - control1) * (6.0f * u * t) + (end - control2) * (3.0f * t * t);
        const Rml::Vector2f normal = Rml::Vector2f(-tangent.y, tangent.x).Normalise();

        for (int row = 0; row < ROWS; row++) {
          Rml::Vertex vertex;
          vertex.position = point + normal * OFFSETS[row];
          vertex.colour = Rml::Colourb(122, 162, 247, 255).ToPremultiplied(ALPHA[row]);
          vertex.tex_coord = Rml::Vector2f(0.0f, 0.0f);
          mesh.vertices.push_back(vertex);
        }

        if (i == SEGMENTS) {
          continue;
        }

        for (int row = 0; row < ROWS - 1; row++) {
          const int current = i * ROWS + row;
          const int next = current + ROWS;

          mesh.indices.insert(mesh.indices.end(), {current, next, current + 1, current + 1, next, next + 1});
        }
      }

      return mesh;
    }
};

/**
 * Общие для всех экземпляров ресурсы RmlUi: инициализируются первым окном, освобождаются последним
 */
struct SharedRmlUi {
  int users = 0;
  Rml::ElementInstancerGeneric<WireLayer> wireInstancer;
};

static SharedRmlUi &sharedRmlUi() {
  static SharedRmlUi shared;
  return shared;
}

struct ProtoPlugin {
  clap_plugin_t plugin;
  const clap_host_t *host;
  const clap_host_timer_support_t *hostTimer;
  clap_id timerId;

  PuglWorld *world;
  PuglView *view;
  RenderInterface_GL3 *renderer;
  Rml::Context *context;
  Rml::ElementDocument *document;

  int clicks;
  bool dragging;
  Rml::Vector2f dragOffset;
  uint64_t frames;
};

/**
 * Обработчик событий документа: нажатие кнопки и перетаскивание блоков
 */
class DocumentListener : public Rml::EventListener {
  public:
    explicit DocumentListener(ProtoPlugin *instance) : instance(instance) {}

    void ProcessEvent(Rml::Event &event) override {
      Rml::Element *element = event.GetCurrentElement();

      switch (event.GetId()) {
        case Rml::EventId::Click: {
          this->instance->clicks++;
          std::string text = "Clicks: " + std::to_string(this->instance->clicks);
          this->instance->document->GetElementById("count")->SetInnerRML(text);
          break;
        }

        case Rml::EventId::Dragstart: {
          const Rml::Vector2f mouse(event.GetParameter("mouse_x", 0.0f), event.GetParameter("mouse_y", 0.0f));
          this->instance->dragOffset = mouse - element->GetAbsoluteOffset(Rml::BoxArea::Border);
          break;
        }

        case Rml::EventId::Drag: {
          const Rml::Vector2f mouse(event.GetParameter("mouse_x", 0.0f), event.GetParameter("mouse_y", 0.0f));
          const Rml::Vector2f position = mouse - this->instance->dragOffset;
          element->SetProperty(Rml::PropertyId::Left, Rml::Property(position.x, Rml::Unit::PX));
          element->SetProperty(Rml::PropertyId::Top, Rml::Property(position.y, Rml::Unit::PX));
          break;
        }

        default:
          break;
      }
    }

  private:
    ProtoPlugin *instance;
};

static int rmlModifiers(PuglMods mods) {
  int result = 0;

  if (mods & PUGL_MOD_SHIFT) {
    result |= Rml::Input::KM_SHIFT;
  }

  if (mods & PUGL_MOD_CTRL) {
    result |= Rml::Input::KM_CTRL;
  }

  if (mods & PUGL_MOD_ALT) {
    result |= Rml::Input::KM_ALT;
  }

  return result;
}

/**
 * Создание интерфейса. Вызывается из PUGL_REALIZE: контекст OpenGL уже активен
 */
static void onRealize(ProtoPlugin *instance) {
  SharedRmlUi &shared = sharedRmlUi();
  std::string message;

  if (shared.users == 0) {
    if (!RmlGL3::Initialize(&message)) {
      fprintf(stderr, "sint-proto: OpenGL init failed: %s\n", message.c_str());
      return;
    }

    Rml::Initialise();
    Rml::Factory::RegisterElementInstancer("wire-layer", &shared.wireInstancer);
    Rml::LoadFontFace(Rml::Span<const Rml::byte>(embeddedFont, embeddedFontSize), "Lato Latin", Rml::Style::FontStyle::Normal, Rml::Style::FontWeight::Normal);
    fprintf(stderr, "sint-proto: %s\n", message.c_str());
  }

  shared.users++;

  instance->renderer = new RenderInterface_GL3();
  instance->renderer->SetViewport(WINDOW_WIDTH, WINDOW_HEIGHT);

  const std::string contextName = "sint-" + std::to_string((uintptr_t)instance);
  instance->context = Rml::CreateContext(contextName, Rml::Vector2i(WINDOW_WIDTH, WINDOW_HEIGHT), instance->renderer);
  instance->document = instance->context->LoadDocumentFromMemory(DOCUMENT);
  instance->document->Show();

  auto *listener = new DocumentListener(instance);
  instance->document->GetElementById("button")->AddEventListener(Rml::EventId::Click, listener);

  for (const char *id : {"osc", "filter"}) {
    Rml::Element *block = instance->document->GetElementById(id);
    block->AddEventListener(Rml::EventId::Dragstart, listener);
    block->AddEventListener(Rml::EventId::Drag, listener);
  }
}

/**
 * Освобождение интерфейса. Контекст OpenGL ещё активен
 */
static void onUnrealize(ProtoPlugin *instance) {
  if (!instance->context) {
    return;
  }

  Rml::RemoveContext(instance->context->GetName());
  instance->context = nullptr;
  instance->document = nullptr;

  Rml::ReleaseRenderManagers();
  delete instance->renderer;
  instance->renderer = nullptr;

  SharedRmlUi &shared = sharedRmlUi();
  shared.users--;

  if (shared.users == 0) {
    Rml::Shutdown();
    RmlGL3::Shutdown();
  }
}

static void dumpFrame(const char *path) {
  std::vector<unsigned char> pixels(WINDOW_WIDTH * WINDOW_HEIGHT * 3);
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glReadPixels(0, 0, WINDOW_WIDTH, WINDOW_HEIGHT, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());

  FILE *file = fopen(path, "wb");

  if (!file) {
    return;
  }

  fprintf(file, "P6 %u %u 255\n", WINDOW_WIDTH, WINDOW_HEIGHT);

  for (uint32_t row = WINDOW_HEIGHT; row > 0; row--) {
    fwrite(&pixels[(row - 1) * WINDOW_WIDTH * 3], 3, WINDOW_WIDTH, file);
  }

  fclose(file);
}

static void onExpose(ProtoPlugin *instance) {
  if (!instance->context) {
    return;
  }

  instance->context->Update();
  instance->renderer->BeginFrame();
  instance->renderer->Clear();
  instance->context->Render();
  instance->renderer->EndFrame();

  // Тестовая запись кадра в файл, используется только в test_host
  const char *dumpPath = getenv("SINT_PROTO_DUMP");
  instance->frames++;

  if (dumpPath && instance->frames % 10 == 0) {
    dumpFrame(dumpPath);
  }
}

static PuglStatus onEvent(PuglView *view, const PuglEvent *event) {
  auto *instance = (ProtoPlugin *)puglGetHandle(view);

  switch (event->type) {
    case PUGL_REALIZE:
      onRealize(instance);
      break;
    case PUGL_UNREALIZE:
      onUnrealize(instance);
      break;
    case PUGL_EXPOSE:
      onExpose(instance);
      break;
    case PUGL_TIMER:
      puglObscureView(view);
      break;
    case PUGL_MOTION:
      if (instance->context) {
        instance->context->ProcessMouseMove((int)event->motion.x, (int)event->motion.y, rmlModifiers(event->motion.state));
      }
      break;
    case PUGL_BUTTON_PRESS:
      if (instance->context) {
        instance->context->ProcessMouseButtonDown((int)event->button.button, rmlModifiers(event->button.state));
      }
      break;
    case PUGL_BUTTON_RELEASE:
      if (instance->context) {
        instance->context->ProcessMouseButtonUp((int)event->button.button, rmlModifiers(event->button.state));
      }
      break;
    case PUGL_POINTER_OUT:
      if (instance->context) {
        instance->context->ProcessMouseLeave();
      }
      break;
    default:
      break;
  }

  return PUGL_SUCCESS;
}

// /////////////////////////
// // clap_plugin_gui     //
// /////////////////////////

static const char *nativeApi() {
#ifdef _WIN32
  return CLAP_WINDOW_API_WIN32;
#else
  return CLAP_WINDOW_API_X11;
#endif
}

static bool guiIsApiSupported(const clap_plugin_t *, const char *api, bool isFloating) {
  return !isFloating && strcmp(api, nativeApi()) == 0;
}

static bool guiGetPreferredApi(const clap_plugin_t *, const char **api, bool *isFloating) {
  *api = nativeApi();
  *isFloating = false;
  return true;
}

static bool guiCreate(const clap_plugin_t *plugin, const char *api, bool isFloating) {
  auto *instance = (ProtoPlugin *)plugin->plugin_data;

  if (!guiIsApiSupported(plugin, api, isFloating) || instance->world) {
    return false;
  }

  instance->world = puglNewWorld(PUGL_MODULE, 0);
  puglSetWorldString(instance->world, PUGL_CLASS_NAME, "SintGuiPrototype");

  instance->view = puglNewView(instance->world);
  puglSetHandle(instance->view, instance);
  puglSetBackend(instance->view, puglGlBackend());
  puglSetEventFunc(instance->view, onEvent);
  puglSetSizeHint(instance->view, PUGL_DEFAULT_SIZE, WINDOW_WIDTH, WINDOW_HEIGHT);
  puglSetViewHint(instance->view, PUGL_CONTEXT_API, PUGL_OPENGL_API);
  puglSetViewHint(instance->view, PUGL_CONTEXT_VERSION_MAJOR, 3);
  puglSetViewHint(instance->view, PUGL_CONTEXT_VERSION_MINOR, 3);
  puglSetViewHint(instance->view, PUGL_CONTEXT_PROFILE, PUGL_OPENGL_CORE_PROFILE);
  puglSetViewHint(instance->view, PUGL_STENCIL_BITS, 8);
  puglSetViewHint(instance->view, PUGL_DOUBLE_BUFFER, 1);

  return true;
}

static void guiDestroy(const clap_plugin_t *plugin) {
  auto *instance = (ProtoPlugin *)plugin->plugin_data;

  if (instance->timerId != CLAP_INVALID_ID) {
    instance->hostTimer->unregister_timer(instance->host, instance->timerId);
    instance->timerId = CLAP_INVALID_ID;
  }

  if (instance->view) {
    puglFreeView(instance->view);
    instance->view = nullptr;
  }

  if (instance->world) {
    puglFreeWorld(instance->world);
    instance->world = nullptr;
  }
}

static bool guiSetScale(const clap_plugin_t *, double) {
  return false;
}

static bool guiGetSize(const clap_plugin_t *, uint32_t *width, uint32_t *height) {
  *width = WINDOW_WIDTH;
  *height = WINDOW_HEIGHT;
  return true;
}

static bool guiCanResize(const clap_plugin_t *) {
  return false;
}

static bool guiGetResizeHints(const clap_plugin_t *, clap_gui_resize_hints_t *) {
  return false;
}

static bool guiAdjustSize(const clap_plugin_t *, uint32_t *width, uint32_t *height) {
  *width = WINDOW_WIDTH;
  *height = WINDOW_HEIGHT;
  return true;
}

static bool guiSetSize(const clap_plugin_t *, uint32_t width, uint32_t height) {
  return width == WINDOW_WIDTH && height == WINDOW_HEIGHT;
}

static bool guiSetParent(const clap_plugin_t *plugin, const clap_window_t *window) {
  auto *instance = (ProtoPlugin *)plugin->plugin_data;

#ifdef _WIN32
  puglSetParent(instance->view, (PuglNativeView)window->win32);
#else
  puglSetParent(instance->view, (PuglNativeView)window->x11);
#endif

  if (puglRealize(instance->view) != PUGL_SUCCESS) {
    return false;
  }

  // Предпочитаем таймер хоста: через него идут и события pugl
  if (instance->hostTimer) {
    instance->hostTimer->register_timer(instance->host, 16, &instance->timerId);
  }

  if (instance->timerId == CLAP_INVALID_ID) {
    puglStartTimer(instance->view, 1, 1.0 / 60.0);
  }

  return true;
}

static bool guiSetTransient(const clap_plugin_t *, const clap_window_t *) {
  return false;
}

static void guiSuggestTitle(const clap_plugin_t *, const char *) {}

static bool guiShow(const clap_plugin_t *plugin) {
  auto *instance = (ProtoPlugin *)plugin->plugin_data;
  return puglShow(instance->view, PUGL_SHOW_PASSIVE) == PUGL_SUCCESS;
}

static bool guiHide(const clap_plugin_t *plugin) {
  auto *instance = (ProtoPlugin *)plugin->plugin_data;
  return puglHide(instance->view) == PUGL_SUCCESS;
}

static const clap_plugin_gui_t s_gui = {
  .is_api_supported = guiIsApiSupported,
  .get_preferred_api = guiGetPreferredApi,
  .create = guiCreate,
  .destroy = guiDestroy,
  .set_scale = guiSetScale,
  .get_size = guiGetSize,
  .can_resize = guiCanResize,
  .get_resize_hints = guiGetResizeHints,
  .adjust_size = guiAdjustSize,
  .set_size = guiSetSize,
  .set_parent = guiSetParent,
  .set_transient = guiSetTransient,
  .suggest_title = guiSuggestTitle,
  .show = guiShow,
  .hide = guiHide,
};

static void timerOnTimer(const clap_plugin_t *plugin, clap_id) {
  auto *instance = (ProtoPlugin *)plugin->plugin_data;

  if (!instance->view) {
    return;
  }

  puglObscureView(instance->view);
  puglUpdate(instance->world, 0.0);
}

static const clap_plugin_timer_support_t s_timer = {.on_timer = timerOnTimer};

// /////////////////////////
// // Порты               //
// /////////////////////////

static uint32_t audioPortsCount(const clap_plugin_t *, bool isInput) {
  return isInput ? 0 : 1;
}

static bool audioPortsGet(const clap_plugin_t *, uint32_t index, bool isInput, clap_audio_port_info_t *info) {
  if (isInput || index > 0) {
    return false;
  }

  info->id = 0;
  snprintf(info->name, sizeof(info->name), "%s", "Output");
  info->channel_count = 2;
  info->flags = CLAP_AUDIO_PORT_IS_MAIN;
  info->port_type = CLAP_PORT_STEREO;
  info->in_place_pair = CLAP_INVALID_ID;
  return true;
}

static const clap_plugin_audio_ports_t s_audioPorts = {.count = audioPortsCount, .get = audioPortsGet};

static uint32_t notePortsCount(const clap_plugin_t *, bool isInput) {
  return isInput ? 1 : 0;
}

static bool notePortsGet(const clap_plugin_t *, uint32_t index, bool isInput, clap_note_port_info_t *info) {
  if (!isInput || index > 0) {
    return false;
  }

  info->id = 0;
  snprintf(info->name, sizeof(info->name), "%s", "Notes");
  info->supported_dialects = CLAP_NOTE_DIALECT_CLAP;
  info->preferred_dialect = CLAP_NOTE_DIALECT_CLAP;
  return true;
}

static const clap_plugin_note_ports_t s_notePorts = {.count = notePortsCount, .get = notePortsGet};

// /////////////////////////
// // Плагин              //
// /////////////////////////

static const char *const s_features[] = {CLAP_PLUGIN_FEATURE_INSTRUMENT, CLAP_PLUGIN_FEATURE_SYNTHESIZER, CLAP_PLUGIN_FEATURE_STEREO, nullptr};

static const clap_plugin_descriptor_t s_descriptor = {
  .clap_version = CLAP_VERSION_INIT,
  .id = "dev.sint.gui-prototype",
  .name = "SIN-T GUI Prototype",
  .vendor = "ChillySwim",
  .url = "",
  .manual_url = "",
  .support_url = "",
  .version = "0.0.1",
  .description = "RmlUi + pugl GUI prototype, makes no sound",
  .features = s_features,
};

static bool pluginInit(const clap_plugin_t *plugin) {
  auto *instance = (ProtoPlugin *)plugin->plugin_data;
  instance->hostTimer = (const clap_host_timer_support_t *)instance->host->get_extension(instance->host, CLAP_EXT_TIMER_SUPPORT);
  return true;
}

static void pluginDestroy(const clap_plugin_t *plugin) {
  delete (ProtoPlugin *)plugin->plugin_data;
}

static bool pluginActivate(const clap_plugin_t *, double, uint32_t, uint32_t) {
  return true;
}

static void pluginDeactivate(const clap_plugin_t *) {}

static bool pluginStartProcessing(const clap_plugin_t *) {
  return true;
}

static void pluginStopProcessing(const clap_plugin_t *) {}

static void pluginReset(const clap_plugin_t *) {}

static clap_process_status pluginProcess(const clap_plugin_t *, const clap_process_t *process) {
  for (uint32_t channel = 0; channel < process->audio_outputs[0].channel_count; channel++) {
    memset(process->audio_outputs[0].data32[channel], 0, process->frames_count * sizeof(float));
  }

  return CLAP_PROCESS_SLEEP;
}

static const void *pluginGetExtension(const clap_plugin_t *, const char *id) {
  if (!strcmp(id, CLAP_EXT_GUI)) {
    return &s_gui;
  }

  if (!strcmp(id, CLAP_EXT_TIMER_SUPPORT)) {
    return &s_timer;
  }

  if (!strcmp(id, CLAP_EXT_AUDIO_PORTS)) {
    return &s_audioPorts;
  }

  if (!strcmp(id, CLAP_EXT_NOTE_PORTS)) {
    return &s_notePorts;
  }

  return nullptr;
}

static void pluginOnMainThread(const clap_plugin_t *) {}

static uint32_t factoryGetCount(const clap_plugin_factory_t *) {
  return 1;
}

static const clap_plugin_descriptor_t *factoryGetDescriptor(const clap_plugin_factory_t *, uint32_t index) {
  return index == 0 ? &s_descriptor : nullptr;
}

static const clap_plugin_t *factoryCreate(const clap_plugin_factory_t *, const clap_host_t *host, const char *id) {
  if (strcmp(id, s_descriptor.id) != 0) {
    return nullptr;
  }

  auto *instance = new ProtoPlugin{};
  instance->host = host;
  instance->timerId = CLAP_INVALID_ID;
  instance->plugin.desc = &s_descriptor;
  instance->plugin.plugin_data = instance;
  instance->plugin.init = pluginInit;
  instance->plugin.destroy = pluginDestroy;
  instance->plugin.activate = pluginActivate;
  instance->plugin.deactivate = pluginDeactivate;
  instance->plugin.start_processing = pluginStartProcessing;
  instance->plugin.stop_processing = pluginStopProcessing;
  instance->plugin.reset = pluginReset;
  instance->plugin.process = pluginProcess;
  instance->plugin.get_extension = pluginGetExtension;
  instance->plugin.on_main_thread = pluginOnMainThread;
  return &instance->plugin;
}

static const clap_plugin_factory_t s_factory = {
  .get_plugin_count = factoryGetCount,
  .get_plugin_descriptor = factoryGetDescriptor,
  .create_plugin = factoryCreate,
};

extern "C" CLAP_EXPORT const clap_plugin_entry_t clap_entry = {
  .clap_version = CLAP_VERSION_INIT,
  .init = [](const char *) -> bool { return true; },
  .deinit = []() {},
  .get_factory = [](const char *id) -> const void * {
    return strcmp(id, CLAP_PLUGIN_FACTORY_ID) == 0 ? &s_factory : nullptr;
  },
};
