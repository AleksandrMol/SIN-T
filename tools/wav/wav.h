#pragma once

#include <cstdint>
#include <optional>
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
 * Содержимое WAV-файла
 */
struct WavFile {
  StereoBuffer audio;
  uint32_t sampleRate;
};

/**
 * Записывает звук в WAV-файл: 32-битный float, два канала, порядок байт little-endian.
 * Возвращает false, если файл не удалось записать
 */
bool writeWav(const std::string& path, const StereoBuffer& audio, uint32_t sampleRate);

/**
 * Читает файл в том же формате, в каком его пишет writeWav. Другие форматы не поддержаны.
 * Возвращает пустое значение, если файл не удалось прочитать или формат другой
 */
std::optional<WavFile> readWav(const std::string& path);
