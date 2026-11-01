#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <string>
#include <vector>

#include "../offlineHost/offlineHost.h"

/**
 * Наибольшая разница сэмпла с эталоном, которая считается совпадением (D-072, D-085): -100 дБ.
 * Рендер немного меняется от флагов компилятора и процессора, замер дал не больше 1,2e-7
 */
constexpr float GOLDEN_TOLERANCE = 1e-5f;

/**
 * Один сценарий эталона: что и как рендерится
 */
struct GoldenScenario {
  std::string name;
  std::string presetPath; // Относительно корня проекта. Пусто — пресет по умолчанию
  std::vector<OfflineNote> notes;
  uint32_t frameCount;
  uint32_t sampleRate = 48000;
  double tempo = 0.0;
};

/**
 * Где сильнее всего звук расходится с эталоном
 */
struct GoldenDifference {
  float amount;
  size_t frame;
};

const std::vector<GoldenScenario>& getGoldenScenarios();

std::filesystem::path getGoldenPath(const GoldenScenario& scenario, const std::filesystem::path& sourceDirectory);

/**
 * Рендерит сценарий через тот же офлайн-хост, что и sint_render
 */
std::expected<WavFile, std::string> renderGolden(const GoldenScenario& scenario, const std::filesystem::path& sourceDirectory);

/**
 * Наибольшая разница по обоим каналам. Разная длина, разная частота дискретизации
 * и нечисловой сэмпл дают бесконечность
 */
GoldenDifference compareGolden(const WavFile& expected, const WavFile& actual);

/**
 * Звук годится в эталоны: все сэмплы конечны и это не тишина
 */
bool isAudible(const StereoBuffer& audio);
