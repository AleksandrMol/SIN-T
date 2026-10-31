#pragma once

#include <cstdint>
#include <string>
#include <vector>

/**
 * Стерео-звук в памяти: два канала одинаковой длины
 */
struct StereoBuffer {
  std::vector<float> left;
  std::vector<float> right;
};

/**
 * Записывает звук в WAV-файл: 32-битный float, два канала, порядок байт little-endian.
 * Возвращает false, если файл не удалось записать
 */
bool writeWav(const std::string& path, const StereoBuffer& audio, uint32_t sampleRate);
