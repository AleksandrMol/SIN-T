#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <initializer_list>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "../clap/clap.h"
#include "../engine/filter/filterProcessor.h"
#include "../engine/moduleCatalog/moduleCatalog.h"
#include "../engine/oscillator/oscillatorProcessor.h"
#include "../engine/preset/preset.h"
#include "../engine/synthModel/synthModel.h"
#include "../tools/offlineHost/offlineHost.h"

namespace {
  constexpr uint32_t SAMPLE_RATE = 48000;

  // Время релиза огибающей по умолчанию: 1200 мс
  constexpr uint32_t RELEASE_FRAMES = 1200 * SAMPLE_RATE / 1000;

  const clap_plugin_factory_t* getFactory() {
    return (const clap_plugin_factory_t*)clap_entry.get_factory(CLAP_PLUGIN_FACTORY_ID);
  }

  bool isSilent(const std::vector<float>& channel, uint32_t from, uint32_t to) {
    for (uint32_t i = from; i < to; i++) {
      if (channel[i] != 0.0f) {
        return false;
      }
    }

    return true;
  }

  float peak(const std::vector<float>& channel, uint32_t from, uint32_t to) {
    float result = 0.0f;

    for (uint32_t i = from; i < to; i++) {
      result = std::fmax(result, std::fabs(channel[i]));
    }

    return result;
  }

  int countRisingCrossings(const std::vector<float>& channel) {
    int crossings = 0;

    for (size_t i = 1; i < channel.size(); i++) {
      if (channel[i - 1] < 0.0f && channel[i] >= 0.0f) {
        crossings++;
      }
    }

    return crossings;
  }

  /**
   * Пресет: синус заданной частоты идёт через фильтр на выход
   */
  std::string buildPreset(float frequency, float cutoff = 20000.0f, bool isFilterEnabled = true) {
    ModuleRegistry registry;
    REQUIRE(addAllModules(registry).has_value());

    SynthModel model(&registry);
    const ModuleId osc = **model.apply(AddModule{ "oscillator" });
    const ModuleId filter = **model.apply(AddModule{ "filter" });

    for (const Command& command : std::initializer_list<Command>{
      SetParameter{ osc, "wave", 1.0f },
      SetParameter{ osc, "frequency", frequency },
      SetParameter{ osc, "level", 0.5f },
      SetParameter{ filter, "cutoff", cutoff },
      SetModuleEnabled{ filter, isFilterEnabled },
      Connect{ { osc, "out", filter, "in" } },
      Connect{ { filter, "out", model.getOutputId(), "in" } },
    }) {
      REQUIRE(model.apply(command).has_value());
    }

    return saveModel(model);
  }

  /**
   * Поток чтения, отдающий данные мелкими порциями: CLAP этого не запрещает
   */
  struct ChunkedReader {
    std::string text;
    size_t chunkSize;
    bool shouldFail = false;
    size_t offset = 0;
    clap_istream_t stream = { .ctx = this, .read = ChunkedReader::read };

    static int64_t read(const clap_istream_t* source, void* buffer, uint64_t size) {
      auto* reader = (ChunkedReader*)source->ctx;

      if (reader->shouldFail) {
        return -1;
      }

      const size_t count = std::min({ (size_t)size, reader->chunkSize, reader->text.size() - reader->offset });

      std::memcpy(buffer, reader->text.data() + reader->offset, count);
      reader->offset += count;

      return (int64_t)count;
    }
  };

  /**
   * Поток записи, принимающий данные мелкими порциями
   */
  struct ChunkedWriter {
    size_t chunkSize;
    bool shouldFail = false;
    std::string text = {};
    clap_ostream_t stream = { .ctx = this, .write = ChunkedWriter::write };

    static int64_t write(const clap_ostream_t* target, const void* buffer, uint64_t size) {
      auto* writer = (ChunkedWriter*)target->ctx;

      if (writer->shouldFail) {
        return -1;
      }

      const size_t count = std::min((size_t)size, writer->chunkSize);

      writer->text.append((const char*)buffer, count);

      return (int64_t)count;
    }
  };

  /**
   * Инициализированный плагин без рендера, для проверки состояния
   */
  struct PluginFixture {
    clap_host_t host{};
    const clap_plugin_t* plugin = nullptr;
    const clap_plugin_state_t* state = nullptr;

    PluginFixture() {
      const clap_plugin_factory_t* factory = getFactory();

      this->host.clap_version = CLAP_VERSION;
      this->plugin = factory->create_plugin(factory, &this->host, factory->get_plugin_descriptor(factory, 0)->id);

      REQUIRE(this->plugin != nullptr);
      REQUIRE(this->plugin->init(this->plugin));

      this->state = (const clap_plugin_state_t*)this->plugin->get_extension(this->plugin, CLAP_EXT_STATE);

      REQUIRE(this->state != nullptr);
    }

    ~PluginFixture() {
      this->plugin->destroy(this->plugin);
    }

    bool load(const std::string& text, size_t chunkSize = 4096) {
      ChunkedReader reader = { .text = text, .chunkSize = chunkSize };

      return this->state->load(this->plugin, &reader.stream);
    }

    std::string save() {
      ChunkedWriter writer = { .chunkSize = 4096 };

      REQUIRE(this->state->save(this->plugin, &writer.stream));

      return writer.text;
    }
  };
}

TEST_CASE("Дескриптор содержит постоянный ID и теги инструмента") {
  const clap_plugin_factory_t* factory = getFactory();
  REQUIRE(factory != nullptr);
  REQUIRE(factory->get_plugin_count(factory) == 1);

  const clap_plugin_descriptor_t* descriptor = factory->get_plugin_descriptor(factory, 0);
  REQUIRE(descriptor != nullptr);

  // Менять нельзя: DAW находит плагин в проектах по этой строке (D-022)
  CHECK(std::string(descriptor->id) == "com.chillyswim.sin-t");

  std::vector<std::string> features;
  for (const char* const* feature = descriptor->features; *feature; feature++) {
    features.emplace_back(*feature);
  }

  CHECK(features == std::vector<std::string>{
    CLAP_PLUGIN_FEATURE_INSTRUMENT,
    CLAP_PLUGIN_FEATURE_SYNTHESIZER,
    CLAP_PLUGIN_FEATURE_STEREO,
  });
}

TEST_CASE("Порты: один стерео-выход и один нотный вход") {
  const clap_plugin_factory_t* factory = getFactory();
  clap_host_t host{};
  host.clap_version = CLAP_VERSION;

  const clap_plugin_t* plugin = factory->create_plugin(factory, &host, factory->get_plugin_descriptor(factory, 0)->id);
  REQUIRE(plugin != nullptr);

  const auto* audioPorts = (const clap_plugin_audio_ports_t*)plugin->get_extension(plugin, CLAP_EXT_AUDIO_PORTS);
  const auto* notePorts = (const clap_plugin_note_ports_t*)plugin->get_extension(plugin, CLAP_EXT_NOTE_PORTS);
  REQUIRE(audioPorts != nullptr);
  REQUIRE(notePorts != nullptr);

  CHECK(audioPorts->count(plugin, true) == 0);
  CHECK(audioPorts->count(plugin, false) == 1);
  CHECK(notePorts->count(plugin, true) == 1);
  CHECK(notePorts->count(plugin, false) == 0);

  clap_audio_port_info_t audioInfo{};
  CHECK_FALSE(audioPorts->get(plugin, 0, true, &audioInfo));
  CHECK_FALSE(audioPorts->get(plugin, 1, false, &audioInfo));
  REQUIRE(audioPorts->get(plugin, 0, false, &audioInfo));
  CHECK(audioInfo.channel_count == 2);
  CHECK(std::string(audioInfo.port_type) == CLAP_PORT_STEREO);

  clap_note_port_info_t noteInfo{};
  CHECK_FALSE(notePorts->get(plugin, 0, false, &noteInfo));
  CHECK_FALSE(notePorts->get(plugin, 1, true, &noteInfo));
  CHECK(notePorts->get(plugin, 0, true, &noteInfo));

  plugin->destroy(plugin);
}

TEST_CASE("Нота звучит с точностью до сэмпла и затихает после релиза") {
  const uint32_t startFrame = 1000;
  const uint32_t endFrame = 24000;
  const uint32_t silenceFrame = endFrame + RELEASE_FRAMES + 10;

  const StereoBuffer audio = renderOffline(
    { { .key = 60, .startFrame = startFrame, .endFrame = endFrame } },
    { .frameCount = silenceFrame + 2000, .sampleRate = SAMPLE_RATE }
  );

  REQUIRE(audio.left.size() == silenceFrame + 2000);

  // Пила начинается с середины скачка, поэтому самый первый сэмпл ноты равен нулю
  CHECK(isSilent(audio.left, 0, startFrame + 1));
  CHECK(audio.left[startFrame + 1] != 0.0f);
  CHECK(peak(audio.left, startFrame, endFrame) > 0.01f);
  CHECK(peak(audio.left, startFrame, endFrame) < 1.0f);
  CHECK(peak(audio.left, endFrame, endFrame + RELEASE_FRAMES) > 0.0f);
  CHECK(isSilent(audio.left, silenceFrame, (uint32_t)audio.left.size()));
  CHECK(audio.left == audio.right);
}

TEST_CASE("Рендер одинаков при повторе и при любом размере блока хоста") {
  const std::vector<OfflineNote> notes = {
    { .key = 60, .startFrame = 100, .endFrame = 20000 },
    { .key = 64, .startFrame = 5003, .endFrame = 15000 },
  };
  const uint32_t frameCount = 30000;

  const StereoBuffer reference = renderOffline(notes, { .frameCount = frameCount, .blockSize = 256 });
  REQUIRE(reference.left.size() == frameCount);

  CHECK(renderOffline(notes, { .frameCount = frameCount, .blockSize = 256 }).left == reference.left);

  for (const uint32_t blockSize : { 1u, 37u, 64u, 1024u, 8192u }) {
    CAPTURE(blockSize);
    CHECK(renderOffline(notes, { .frameCount = frameCount, .blockSize = blockSize }).left == reference.left);
  }
}

TEST_CASE("Загруженный граф звучит через CLAP (D-032)") {
  const StereoBuffer audio = renderOffline({}, {
    .frameCount = SAMPLE_RATE,
    .sampleRate = SAMPLE_RATE,
    .preset = buildPreset(1000.0f),
  });

  REQUIRE(audio.left.size() == SAMPLE_RATE);

  CHECK(std::abs(countRisingCrossings(audio.left) - 1000) <= 1);
  CHECK(peak(audio.left, 0, SAMPLE_RATE) > 0.3f);
  CHECK(audio.left == audio.right);
}

TEST_CASE("Отключённый модуль из пресета пропускает сигнал через CLAP (D-040)") {
  const StereoBuffer filtered = renderOffline({}, {
    .frameCount = 4800,
    .sampleRate = SAMPLE_RATE,
    .preset = buildPreset(5000.0f, 20.0f),
  });
  const StereoBuffer bypassed = renderOffline({}, {
    .frameCount = 4800,
    .sampleRate = SAMPLE_RATE,
    .preset = buildPreset(5000.0f, 20.0f, false),
  });

  CHECK(peak(filtered.left, 0, 4800) < 0.05f);
  CHECK(peak(bypassed.left, 0, 4800) > 0.3f);
}

TEST_CASE("Частота дискретизации хоста доходит до модулей графа") {
  const uint32_t sampleRate = 96000;

  const StereoBuffer audio = renderOffline({}, {
    .frameCount = sampleRate,
    .sampleRate = sampleRate,
    .preset = buildPreset(1000.0f),
  });

  REQUIRE(audio.left.size() == sampleRate);

  CHECK(std::abs(countRisingCrossings(audio.left) - 1000) <= 1);
}

TEST_CASE("Звук графа через CLAP не зависит от размера блока") {
  const std::string preset = buildPreset(1234.0f);
  const uint32_t frameCount = 9000;

  const StereoBuffer reference = renderOffline({}, { .frameCount = frameCount, .blockSize = 256, .preset = preset });
  REQUIRE(reference.left.size() == frameCount);

  for (const uint32_t blockSize : { 1u, 37u, 1024u }) {
    CAPTURE(blockSize);
    CHECK(renderOffline({}, { .frameCount = frameCount, .blockSize = blockSize, .preset = preset }).left == reference.left);
  }
}

TEST_CASE("Без загруженного графа плагин играет ноты по-старому") {
  // Стартовое состояние совпадает с пустой моделью, поэтому движок не переключается
  PluginFixture fixture;
  const std::string initial = fixture.save();

  const OfflineRenderSettings settings = { .frameCount = 4800, .preset = initial };
  const StereoBuffer audio = renderOffline({ { .key = 60, .startFrame = 0, .endFrame = 4800 } }, settings);

  REQUIRE(audio.left.size() == 4800);
  CHECK(peak(audio.left, 0, 4800) > 0.01f);
}

TEST_CASE("Плагин отвергает неверный пресет и остаётся в прежнем состоянии") {
  PluginFixture fixture;
  const std::string initial = fixture.save();

  CHECK_FALSE(fixture.load("not a preset"));
  CHECK_FALSE(fixture.load(""));
  CHECK_FALSE(fixture.load("{\"version\": 999}"));
  CHECK(fixture.save() == initial);

  CHECK(renderOffline({}, { .frameCount = 100, .preset = "not a preset" }).left.empty());
}

TEST_CASE("Состояние сохраняется и восстанавливается через потоки CLAP") {
  const std::string preset = buildPreset(440.0f);

  PluginFixture source;

  // Потоки могут работать мелкими порциями
  REQUIRE(source.load(preset, 3));
  CHECK(source.save() == preset);

  ChunkedWriter writer = { .chunkSize = 5 };
  REQUIRE(source.state->save(source.plugin, &writer.stream));
  CHECK(writer.text == preset);

  PluginFixture restored;
  REQUIRE(restored.load(writer.text));
  CHECK(restored.save() == preset);
}

TEST_CASE("Ошибка потока отклоняет сохранение и загрузку") {
  PluginFixture fixture;

  ChunkedReader reader = { .text = buildPreset(440.0f), .chunkSize = 4096, .shouldFail = true };
  CHECK_FALSE(fixture.state->load(fixture.plugin, &reader.stream));

  ChunkedWriter writer = { .chunkSize = 4096, .shouldFail = true };
  CHECK_FALSE(fixture.state->save(fixture.plugin, &writer.stream));
}

TEST_CASE("Работающий плагин не принимает новый граф, но принимает своё же состояние") {
  PluginFixture fixture;
  const std::string preset = buildPreset(440.0f);

  REQUIRE(fixture.load(preset));
  REQUIRE(fixture.plugin->activate(fixture.plugin, SAMPLE_RATE, 1, 256));

  CHECK(fixture.load(preset));
  CHECK_FALSE(fixture.load(buildPreset(880.0f)));
  CHECK(fixture.save() == preset);

  fixture.plugin->deactivate(fixture.plugin);

  CHECK(fixture.load(buildPreset(880.0f)));
  CHECK(fixture.save() == buildPreset(880.0f));
}

TEST_CASE("Пример пресета из examples/ загружается и звучит") {
  std::ifstream file(SINT_EXAMPLES_DIR "/two-saws.sintpreset", std::ios::binary);
  std::ostringstream content;

  REQUIRE(file.good());
  content << file.rdbuf();

  const StereoBuffer audio = renderOffline({}, { .frameCount = 4800, .preset = content.str() });

  REQUIRE(audio.left.size() == 4800);
  CHECK(peak(audio.left, 0, 4800) > 0.01f);
}
