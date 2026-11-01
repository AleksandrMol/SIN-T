// Замер редактора на большой схеме без окна: сколько стоит каждый шаг от правки до кадра.
// Запуск: sint_editor_bench [блоков] [маршрутов] [повторов]
// Отрисовка пустая, поэтому OpenGL в цифры не входит: полный путь с окном показывает sint_editor_preview --bench.
// Только для разработки, в плагин не входит.

#include <RmlUi/Core.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "../../clap/clap.h"
#include "../../gui/editorDocument/editorDocument.h"
#include "../../gui/editorView/editorView.h"
#include "../../gui/rmlRuntime/rmlRuntime.h"
#include "../../plugin/plugin.h"
#include "../../plugin/pluginEditorBackend.h"
#include "./largePatch.h"

namespace {
  using Clock = std::chrono::steady_clock;

  class NullRenderer final : public Rml::RenderInterface {
    public:
      Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex>, Rml::Span<const int>) override {
        return 1;
      }

      void RenderGeometry(Rml::CompiledGeometryHandle, Rml::Vector2f, Rml::TextureHandle) override {}
      void ReleaseGeometry(Rml::CompiledGeometryHandle) override {}

      Rml::TextureHandle LoadTexture(Rml::Vector2i&, const Rml::String&) override {
        return 0;
      }

      Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte>, Rml::Vector2i) override {
        return 1;
      }

      void ReleaseTexture(Rml::TextureHandle) override {}
      void EnableScissorRegion(bool) override {}
      void SetScissorRegion(Rml::Rectanglei) override {}
  };

  const clap_host_t s_host = {
    .clap_version = CLAP_VERSION_INIT,
    .host_data = nullptr,
    .name = "sint editor bench",
    .vendor = "",
    .url = "",
    .version = "0",
    .get_extension = [](const clap_host_t*, const char*) -> const void* {
      return nullptr;
    },
    .request_restart = [](const clap_host_t*) {},
    .request_process = [](const clap_host_t*) {},
    .request_callback = [](const clap_host_t*) {},
  };

  double readMilliseconds(Clock::time_point start, Clock::time_point end) {
    return std::chrono::duration<double, std::milli>(end - start).count();
  }

  struct Samples {
    std::vector<double> view;
    std::vector<double> layout;
    std::vector<double> render;
    std::vector<double> total;
  };

  double median(std::vector<double> list) {
    std::ranges::sort(list);

    return list[list.size() / 2];
  }

  double maximum(const std::vector<double>& list) {
    return *std::ranges::max_element(list);
  }

  void printRow(const char* name, const Samples& samples) {
    std::printf(
      "%-12s вид %7.2f  раскладка %7.2f  отрисовка %7.2f  всего %7.2f  (худший %7.2f) мс\n",
      name, median(samples.view), median(samples.layout), median(samples.render), median(samples.total), maximum(samples.total)
    );
  }
}

int main(int argc, char** argv) {
  const size_t blocks = argc > 1 ? (size_t)std::atoi(argv[1]) : 60;
  const size_t routes = argc > 2 ? (size_t)std::atoi(argv[2]) : 100;
  const int repeats = argc > 3 ? std::atoi(argv[3]) : 40;

  const auto* factory = (const clap_plugin_factory_t*)clap_entry.get_factory(CLAP_PLUGIN_FACTORY_ID);
  const clap_plugin_t* plugin = factory->create_plugin(factory, &s_host, factory->get_plugin_descriptor(factory, 0)->id);

  if (!plugin->init(plugin)) {
    return 1;
  }

  auto* instance = (MyPluginInstance*)plugin;
  const LargePatch patch = buildLargePatch(instance, blocks, routes);

  PluginEditorBackend backend(instance);
  NullRenderer renderer;

  acquireRmlRuntime();

  Rml::Context* context = Rml::CreateContext("bench", Rml::Vector2i(1200, 780), &renderer);
  Rml::ElementDocument* document = context->LoadDocumentFromMemory(EDITOR_DOCUMENT);

  document->Show();

  auto view = std::make_unique<EditorView>(document, backend, [] {});

  view->openGraph(patch.generator);
  view->update();
  context->Update();
  context->Render();

  const auto measure = [&](const std::function<void(int)>& change) {
    Samples samples;

    for (int index = 0; index < repeats; index++) {
      change(index);

      const Clock::time_point start = Clock::now();

      view->update();

      const Clock::time_point viewDone = Clock::now();

      context->Update();

      const Clock::time_point layoutDone = Clock::now();

      context->Render();

      const Clock::time_point end = Clock::now();

      samples.view.push_back(readMilliseconds(start, viewDone));
      samples.layout.push_back(readMilliseconds(viewDone, layoutDone));
      samples.render.push_back(readMilliseconds(layoutDone, end));
      samples.total.push_back(readMilliseconds(start, end));
    }

    return samples;
  };

  std::printf(
    "Блоков на холсте %zu, соединений добавлено %zu, маршрутов %zu\n",
    patch.blockCount, patch.connectionCount, patch.routeCount
  );

  const float low = 0.2f;
  const float high = 0.8f;
  const ModuleInstance* valueModule = instance->model->findModule(patch.valueModule);
  const ParameterDescriptor* parameter = nullptr;

  for (const ParameterDescriptor& candidate : valueModule->descriptor->parameters) {
    if (candidate.key == patch.valueParameter) {
      parameter = &candidate;
    }
  }

  printRow("структура", measure([&](int index) {
    (void)my_plug_apply_command(instance, SetModuleEnabled{ patch.lastBlock, index % 2 != 0 });
  }));

  printRow("значение", measure([&](int index) {
    (void)my_plug_apply_command(instance, SetParameter{ patch.valueModule, patch.valueParameter, toReal(*parameter, index % 2 != 0 ? high : low) });
  }));

  printRow("блок", measure([&](int index) {
    (void)my_plug_apply_command(instance, SetModulePosition{ patch.lastBlock, { 40.0f + (float)index, 40.0f } });
  }));

  // Мышь над углом холста: иначе колесо досталось бы ручке или панели
  const Rml::Vector2f corner = document->GetElementById("canvas")->GetAbsoluteOffset(Rml::BoxArea::Border) + Rml::Vector2f(5.0f, 5.0f);

  context->ProcessMouseMove((int)corner.x, (int)corner.y, 0);

  printRow("панорама", measure([&](int index) {
    context->ProcessMouseWheel(Rml::Vector2f(0.0f, index % 2 != 0 ? 1.0f : -1.0f), 0);
  }));

  view.reset();
  Rml::RemoveContext("bench");
  Rml::ReleaseRenderManagers();
  releaseRmlRuntime();
  plugin->destroy(plugin);

  return 0;
}
