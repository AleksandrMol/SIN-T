#pragma once

#include <cstdint>
#include <vector>

#include "../wav/wav.h"

/**
 * Нота для офлайн-рендера. Время задано в сэмплах от начала рендера
 */
struct OfflineNote {
  int16_t key;
  uint32_t startFrame;
  uint32_t endFrame;
  double velocity = 1.0;
};

struct OfflineRenderSettings {
  uint32_t frameCount;
  uint32_t sampleRate = 48000;
  uint32_t blockSize = 256;
};

/**
 * Минимальный хост без DAW. Проходит весь путь плагина через публичный интерфейс CLAP:
 * clap_entry, фабрика, активация, process. Поэтому не зависит от устройства движка.
 * Возвращает пустой буфер, если плагин не удалось создать или он вернул ошибку
 */
StereoBuffer renderOffline(const std::vector<OfflineNote>& notes, const OfflineRenderSettings& settings);
