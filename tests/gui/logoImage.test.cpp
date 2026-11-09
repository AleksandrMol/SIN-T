#include <doctest/doctest.h>

#include "../../gui/logoImage/logoImage.h"

namespace {
  Rml::byte alphaAt(const TextureImage& image, int x, int y) {
    return image.pixels[((size_t)y * (size_t)image.width + (size_t)x) * 4 + 3];
  }
}

TEST_CASE("Логотип находится по своему имени, считается один раз и вдвое больше размера в стилях") {
  CHECK(findLogoImage("") == nullptr);
  CHECK(findLogoImage("sint:brushed") == nullptr);
  CHECK(findLogoImage("logo") == nullptr);

  const TextureImage* logo = findLogoImage("sint:logo");

  REQUIRE(logo != nullptr);
  CHECK(findLogoImage("sint:logo") == logo);
  // При масштабе 1 картинка сжимается ровно вдвое, при 2 рисуется точка в точку
  CHECK(logo->width == LOGO_WIDTH_DP * 2);
  CHECK(logo->height == LOGO_HEIGHT_DP * 2);
  CHECK(logo->pixels.size() == (size_t)logo->width * (size_t)logo->height * 4);
}

TEST_CASE("Логотип — буквы на прозрачном поле с предумноженной прозрачностью") {
  const TextureImage& logo = *findLogoImage("sint:logo");
  bool isPremultiplied = true;

  for (size_t index = 0; index < logo.pixels.size(); index += 4) {
    const Rml::byte alpha = logo.pixels[index + 3];

    isPremultiplied = isPremultiplied && logo.pixels[index] <= alpha && logo.pixels[index + 1] <= alpha && logo.pixels[index + 2] <= alpha;
  }

  CHECK(isPremultiplied);

  // Поле над буквами пустое: ни буква, ни обводка, ни тень сюда не достают
  int topAlpha = 0;

  for (int y = 0; y < 4; y++) {
    for (int x = 0; x < logo.width; x++) {
      topAlpha += alphaAt(logo, x, y);
    }
  }

  CHECK(topAlpha == 0);

  // Посередине высоты буквы сплошные: есть полностью непрозрачные точки, и их много
  int opaque = 0;

  for (int x = 0; x < logo.width; x++) {
    opaque += alphaAt(logo, x, logo.height / 2) == 255 ? 1 : 0;
  }

  CHECK(opaque > logo.width / 4);
}
