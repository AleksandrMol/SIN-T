#include "./goldenScenarios.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "../../engine/presetLibrary/presetLibrary.h"

namespace {
  constexpr float AUDIBLE_PEAK = 0.01f;
  constexpr float NO_MATCH = std::numeric_limits<float>::infinity();

  void addChannelDifference(const std::vector<float>& expected, const std::vector<float>& actual, GoldenDifference& worst) {
    for (size_t frame = 0; frame < expected.size(); frame++) {
      const float difference = std::abs(expected[frame] - actual[frame]);

      // NaN проиграл бы любое сравнение и остался незамеченным
      if (std::isnan(difference)) {
        worst = { NO_MATCH, frame };
        return;
      }

      if (difference > worst.amount) {
        worst = { difference, frame };
      }
    }
  }
}

const std::vector<GoldenScenario>& getGoldenScenarios() {
  static const std::vector<GoldenScenario> scenarios = {
    {
      .name = "two-saws",
      .presetPath = "examples/two-saws.sintpreset",
      .notes = {
        { .key = 45, .startFrame = 0, .endFrame = 36000 },
        { .key = 57, .startFrame = 0, .endFrame = 36000 },
      },
      .frameCount = 48000,
    },
    {
      .name = "modulation-demo",
      .presetPath = "examples/modulation-demo.sintpreset",
      .notes = { { .key = 45, .startFrame = 0, .endFrame = 36000 } },
      .frameCount = 48000,
      .tempo = 120.0,
    },
    {
      .name = "default-chord",
      .presetPath = "",
      .notes = {
        { .key = 60, .startFrame = 0, .endFrame = 36000, .velocity = 1.0 },
        { .key = 64, .startFrame = 0, .endFrame = 36000, .velocity = 0.7 },
        { .key = 67, .startFrame = 0, .endFrame = 36000, .velocity = 0.4 },
      },
      .frameCount = 48000,
    },
    {
      // Нот больше полифонии, времена не кратны сетке 32, другая частота дискретизации
      .name = "full-chain",
      .presetPath = "tests/golden/full-chain.sintpreset",
      .notes = {
        { .key = 48, .startFrame = 7, .endFrame = 20011, .velocity = 1.0 },
        { .key = 55, .startFrame = 5003, .endFrame = 26017, .velocity = 0.8 },
        { .key = 60, .startFrame = 9001, .endFrame = 29033, .velocity = 0.6 },
        { .key = 64, .startFrame = 13007, .endFrame = 32003, .velocity = 0.9 },
        { .key = 72, .startFrame = 17011, .endFrame = 33001, .velocity = 0.5 },
      },
      .frameCount = 44100,
      .sampleRate = 44100,
    },
  };

  return scenarios;
}

std::filesystem::path getGoldenPath(const GoldenScenario& scenario, const std::filesystem::path& sourceDirectory) {
  return sourceDirectory / "tests" / "golden" / (scenario.name + ".wav");
}

std::expected<WavFile, std::string> renderGolden(const GoldenScenario& scenario, const std::filesystem::path& sourceDirectory) {
  std::string preset;

  if (!scenario.presetPath.empty()) {
    auto text = readTextFile(sourceDirectory / scenario.presetPath);

    if (!text) {
      return std::unexpected(text.error());
    }

    preset = std::move(*text);
  }

  StereoBuffer audio = renderOffline(scenario.notes, {
    .frameCount = scenario.frameCount,
    .sampleRate = scenario.sampleRate,
    .preset = preset,
    .tempo = scenario.tempo,
  });

  if (audio.left.empty()) {
    return std::unexpected("рендер не удался: плагин вернул ошибку или отверг пресет");
  }

  return WavFile{ .audio = std::move(audio), .sampleRate = scenario.sampleRate };
}

GoldenDifference compareGolden(const WavFile& expected, const WavFile& actual) {
  if (expected.sampleRate != actual.sampleRate
    || expected.audio.left.size() != actual.audio.left.size()
    || expected.audio.right.size() != actual.audio.right.size()) {
    return { NO_MATCH, 0 };
  }

  GoldenDifference worst = { 0.0f, 0 };

  addChannelDifference(expected.audio.left, actual.audio.left, worst);

  if (worst.amount != NO_MATCH) {
    addChannelDifference(expected.audio.right, actual.audio.right, worst);
  }

  return worst;
}

bool isAudible(const StereoBuffer& audio) {
  float peak = 0.0f;

  for (const std::vector<float>* channel : { &audio.left, &audio.right }) {
    for (const float sample : *channel) {
      if (!std::isfinite(sample)) {
        return false;
      }

      peak = std::max(peak, std::abs(sample));
    }
  }

  return peak >= AUDIBLE_PEAK;
}
