#include "./pluginWindow.h"

#include <RmlUi/Core.h>
#include <RmlUi_Renderer_GL3.h>
#include <pugl/gl.h>
#include <pugl/pugl.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <optional>
#include <utility>
#include <vector>

#include "../editorBackend/editorBackend.h"
#include "../editorDocument/editorDocument.h"
#include "../editorView/editorView.h"
#include "../rmlRuntime/rmlRuntime.h"

namespace {
  constexpr uint32_t DEFAULT_WIDTH = 1200;
  constexpr uint32_t DEFAULT_HEIGHT = 780;

  constexpr uintptr_t OWN_TIMER_ID = 1;
  constexpr double OWN_TIMER_PERIOD = 1.0 / 60.0;

  // Функции OpenGL загружаются один раз на процесс, а окон может быть много
  int s_glUsers = 0;

  int toRmlModifiers(PuglMods mods) {
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

  Rml::Input::KeyIdentifier toRmlKey(uint32_t key) {
    switch (key) {
      case PUGL_KEY_BACKSPACE:
        return Rml::Input::KI_BACK;
      case PUGL_KEY_DELETE:
        return Rml::Input::KI_DELETE;
      case PUGL_KEY_ENTER:
        return Rml::Input::KI_RETURN;
      case PUGL_KEY_ESCAPE:
        return Rml::Input::KI_ESCAPE;
      case PUGL_KEY_LEFT:
        return Rml::Input::KI_LEFT;
      case PUGL_KEY_RIGHT:
        return Rml::Input::KI_RIGHT;
      case PUGL_KEY_HOME:
        return Rml::Input::KI_HOME;
      case PUGL_KEY_END:
        return Rml::Input::KI_END;
      default:
        return Rml::Input::KI_UNKNOWN;
    }
  }

  void writeFrame(const std::string& path, uint32_t width, uint32_t height) {
    std::vector<unsigned char> pixels((size_t)width * height * 3);

    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, (GLsizei)width, (GLsizei)height, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());

    FILE* file = std::fopen(path.c_str(), "wb");

    if (!file) {
      return;
    }

    std::fprintf(file, "P6 %u %u 255\n", width, height);

    for (uint32_t row = height; row > 0; row--) {
      std::fwrite(&pixels[(size_t)(row - 1) * width * 3], 3, width, file);
    }

    std::fclose(file);
  }
}

struct PluginWindow::Implementation {
  EditorBackend& backend;
  PuglWorld* world = nullptr;
  PuglView* view = nullptr;
  std::unique_ptr<RenderInterface_GL3> renderer;
  Rml::Context* context = nullptr;
  std::unique_ptr<EditorView> editor;
  uint32_t width = DEFAULT_WIDTH;
  uint32_t height = DEFAULT_HEIGHT;
  double scale = 1.0;
  bool isDirty = true;
  uint64_t frameCount = 0;
  std::optional<std::string> capturePath;

  explicit Implementation(EditorBackend& backend) : backend(backend) {}

  static PuglStatus onEvent(PuglView* view, const PuglEvent* event) {
    return ((Implementation*)puglGetHandle(view))->handle(event);
  }

  /**
   * Вызывается из PUGL_REALIZE: контекст OpenGL уже активен
   */
  void realize() {
    std::string message;

    if (s_glUsers == 0 && !RmlGL3::Initialize(&message)) {
      std::fprintf(stderr, "SIN-T: OpenGL не инициализирован: %s\n", message.c_str());
      return;
    }

    s_glUsers++;
    acquireRmlRuntime();

    this->renderer = std::make_unique<RenderInterface_GL3>();
    this->renderer->SetViewport((int)this->width, (int)this->height);

    const std::string name = "sint-" + std::to_string((uintptr_t)this);

    this->context = Rml::CreateContext(name, Rml::Vector2i((int)this->width, (int)this->height), this->renderer.get());
    this->context->SetDensityIndependentPixelRatio((float)this->scale);

    Rml::ElementDocument* document = this->context->LoadDocumentFromMemory(EDITOR_DOCUMENT);

    document->Show();

    this->editor = std::make_unique<EditorView>(document, this->backend, [this] {
      puglGrabFocus(this->view);
    });
  }

  /**
   * Контекст OpenGL ещё активен
   */
  void unrealize() {
    if (!this->context) {
      return;
    }

    this->editor.reset();
    Rml::RemoveContext(this->context->GetName());
    this->context = nullptr;

    Rml::ReleaseRenderManagers();
    this->renderer.reset();

    releaseRmlRuntime();

    if (--s_glUsers == 0) {
      RmlGL3::Shutdown();
    }
  }

  void resize(uint32_t width, uint32_t height) {
    this->width = width;
    this->height = height;

    if (this->context) {
      this->renderer->SetViewport((int)width, (int)height);
      this->context->SetDimensions(Rml::Vector2i((int)width, (int)height));
      this->isDirty = true;
    }
  }

  void expose() {
    if (!this->context) {
      return;
    }

    this->context->Update();
    // Clear до BeginFrame, как в бэкендах RmlUi: Clear оставляет цвет очистки непрозрачным,
    // а BeginFrame возвращает прозрачный. Иначе слои для box-shadow и фильтров заливаются чёрным
    this->renderer->Clear();
    this->renderer->BeginFrame();
    this->context->Render();
    this->renderer->EndFrame();
    this->isDirty = false;
    this->frameCount++;

    if (this->capturePath) {
      writeFrame(*this->capturePath, this->width, this->height);
      this->capturePath.reset();
    }
  }

  PuglStatus handle(const PuglEvent* event) {
    switch (event->type) {
      case PUGL_REALIZE:
        this->realize();
        return PUGL_SUCCESS;
      case PUGL_UNREALIZE:
        this->unrealize();
        return PUGL_SUCCESS;
      case PUGL_CONFIGURE:
        this->resize((uint32_t)event->configure.width, (uint32_t)event->configure.height);
        return PUGL_SUCCESS;
      case PUGL_EXPOSE:
        this->expose();
        return PUGL_SUCCESS;
      case PUGL_TIMER:
        this->advance();
        return PUGL_SUCCESS;
      default:
        break;
    }

    if (!this->context) {
      return PUGL_SUCCESS;
    }

    switch (event->type) {
      case PUGL_MOTION:
        this->context->ProcessMouseMove((int)event->motion.x, (int)event->motion.y, toRmlModifiers(event->motion.state));
        break;
      case PUGL_BUTTON_PRESS:
        this->context->ProcessMouseButtonDown((int)event->button.button, toRmlModifiers(event->button.state));
        break;
      case PUGL_BUTTON_RELEASE:
        this->context->ProcessMouseButtonUp((int)event->button.button, toRmlModifiers(event->button.state));
        break;
      case PUGL_SCROLL:
        this->context->ProcessMouseWheel(Rml::Vector2f((float)-event->scroll.dx, (float)-event->scroll.dy), toRmlModifiers(event->scroll.state));
        break;
      case PUGL_POINTER_OUT:
        this->context->ProcessMouseLeave();
        break;
      case PUGL_KEY_PRESS:
        this->context->ProcessKeyDown(toRmlKey(event->key.key), toRmlModifiers(event->key.state));
        break;
      case PUGL_KEY_RELEASE:
        this->context->ProcessKeyUp(toRmlKey(event->key.key), toRmlModifiers(event->key.state));
        break;
      case PUGL_TEXT:
        // Управляющие символы уже пришли как нажатие клавиши
        if (event->text.character >= 0x20 && event->text.character != 0x7F) {
          this->context->ProcessTextInput(Rml::String(event->text.string));
        }
        break;
      default:
        return PUGL_SUCCESS;
    }

    this->isDirty = true;

    return PUGL_SUCCESS;
  }

  bool isRedrawNeeded() const {
    return this->isDirty || this->capturePath.has_value() || (this->context && this->context->GetNextUpdateDelay() <= 0.0);
  }

  /**
   * Сверяет вид с моделью и просит кадр, если есть что показать
   */
  void advance() {
    if (this->editor && this->editor->update()) {
      this->isDirty = true;
    }

    if (this->isRedrawNeeded()) {
      puglObscureView(this->view);
    }
  }
};

PluginWindow::PluginWindow(EditorBackend& backend) : implementation(std::make_unique<Implementation>(backend)) {
  Implementation& self = *this->implementation;

  self.world = puglNewWorld(PUGL_MODULE, 0);
  puglSetWorldString(self.world, PUGL_CLASS_NAME, "SintEditor");

  self.view = puglNewView(self.world);
  puglSetHandle(self.view, &self);
  puglSetBackend(self.view, puglGlBackend());
  puglSetEventFunc(self.view, Implementation::onEvent);
  puglSetSizeHint(self.view, PUGL_DEFAULT_SIZE, self.width, self.height);
  puglSetViewHint(self.view, PUGL_CONTEXT_API, PUGL_OPENGL_API);
  puglSetViewHint(self.view, PUGL_CONTEXT_VERSION_MAJOR, 3);
  puglSetViewHint(self.view, PUGL_CONTEXT_VERSION_MINOR, 3);
  puglSetViewHint(self.view, PUGL_CONTEXT_PROFILE, PUGL_OPENGL_CORE_PROFILE);
  puglSetViewHint(self.view, PUGL_STENCIL_BITS, 8);
  puglSetViewHint(self.view, PUGL_DOUBLE_BUFFER, 1);
}

PluginWindow::~PluginWindow() {
  Implementation& self = *this->implementation;

  puglFreeView(self.view);
  puglFreeWorld(self.world);
}

bool PluginWindow::attach(uintptr_t parent) {
  Implementation& self = *this->implementation;

  puglSetParent(self.view, (PuglNativeView)parent);

  return puglRealize(self.view) == PUGL_SUCCESS;
}

bool PluginWindow::show() {
  return puglShow(this->implementation->view, PUGL_SHOW_PASSIVE) == PUGL_SUCCESS;
}

bool PluginWindow::hide() {
  return puglHide(this->implementation->view) == PUGL_SUCCESS;
}

uint32_t PluginWindow::getWidth() const {
  return this->implementation->width;
}

uint32_t PluginWindow::getHeight() const {
  return this->implementation->height;
}

bool PluginWindow::setSize(uint32_t width, uint32_t height) {
  Implementation& self = *this->implementation;

  self.width = width;
  self.height = height;

  // Хост может задать размер до встраивания, текущего размера у окна тогда ещё нет
  return puglSetSizeHint(self.view, self.context ? PUGL_CURRENT_SIZE : PUGL_DEFAULT_SIZE, width, height) == PUGL_SUCCESS;
}

void PluginWindow::setScale(double scale) {
  Implementation& self = *this->implementation;

  self.scale = scale;

  if (self.context) {
    self.context->SetDensityIndependentPixelRatio((float)scale);
    self.isDirty = true;
  } else {
    self.width = (uint32_t)std::lround(DEFAULT_WIDTH * scale);
    self.height = (uint32_t)std::lround(DEFAULT_HEIGHT * scale);
    puglSetSizeHint(self.view, PUGL_DEFAULT_SIZE, self.width, self.height);
  }
}

void PluginWindow::tick() {
  Implementation& self = *this->implementation;

  self.advance();
  puglUpdate(self.world, 0.0);
}

void PluginWindow::startOwnTimer() {
  puglStartTimer(this->implementation->view, OWN_TIMER_ID, OWN_TIMER_PERIOD);
}

void PluginWindow::captureFrame(std::string path) {
  this->implementation->capturePath = std::move(path);
}

uint64_t PluginWindow::getFrameCount() const {
  return this->implementation->frameCount;
}

void PluginWindow::openGraph(std::optional<uint32_t> generator) {
  if (this->implementation->editor) {
    this->implementation->editor->openGraph(generator);
  }
}
