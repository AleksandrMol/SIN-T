#include <doctest/doctest.h>

#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <vector>

#include "../clap/clap.h"
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
