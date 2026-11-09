#pragma once

#include <RmlUi/Core/Types.h>

#include <vector>

/**
 * Картинка, посчитанная кодом: RGBA с предумноженной прозрачностью, как её ждёт RmlUi.
 * Рендерер окна отдаёт такие картинки по именам sint:... из стилей
 */
struct TextureImage {
  int width = 0;
  int height = 0;
  std::vector<Rml::byte> pixels;
};
