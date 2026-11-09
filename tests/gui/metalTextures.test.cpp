#include <doctest/doctest.h>

#include <cmath>
#include <string>

#include "../../gui/metalTextures/metalTextures.h"

namespace {
  const char* const NAMES[] = { "sint:brushed", "sint:worn", "sint:scuffed" };

  /**
   * Яркость со знаком, как её задумал генератор: белый плюс, чёрный минус
   */
  float valueAt(const TextureImage& image, int x, int y) {
    const size_t index = ((size_t)y * (size_t)image.width + (size_t)x) * 4;
    const float alpha = image.pixels[index + 3] / 255.0f;

    return image.pixels[index] > 0 ? alpha : -alpha;
  }

  /**
   * Перепад между соседними столбцами через стык плитки и самый резкий перепад между соседними столбцами внутри.
   * Шов идёт через всю высоту, поэтому выделяется на фоне любой пары внутри. Одна царапина через стык —
   * обычное дело и для пар внутри, поэтому со средним по одной паре сравнивать нельзя
   */
  struct Seam {
    float across = 0.0f;
    float inside = 0.0f;
  };

  float columnStep(const TextureImage& image, int left, int right) {
    float sum = 0.0f;

    for (int y = 0; y < image.height; y++) {
      sum += std::fabs(valueAt(image, left, y) - valueAt(image, right, y));
    }

    return sum;
  }

  float rowStep(const TextureImage& image, int top, int bottom) {
    float sum = 0.0f;

    for (int x = 0; x < image.width; x++) {
      sum += std::fabs(valueAt(image, x, top) - valueAt(image, x, bottom));
    }

    return sum;
  }

  Seam measureColumns(const TextureImage& image) {
    Seam seam;

    seam.across = columnStep(image, image.width - 1, 0);

    for (int x = 0; x + 1 < image.width; x++) {
      seam.inside = std::max(seam.inside, columnStep(image, x, x + 1));
    }

    return seam;
  }

  Seam measureRows(const TextureImage& image) {
    Seam seam;

    seam.across = rowStep(image, image.height - 1, 0);

    for (int y = 0; y + 1 < image.height; y++) {
      seam.inside = std::max(seam.inside, rowStep(image, y, y + 1));
    }

    return seam;
  }
}

TEST_CASE("Текстуры металла находятся по своим именам и считаются один раз") {
  CHECK(findMetalTexture("") == nullptr);
  CHECK(findMetalTexture("sint:") == nullptr);
  CHECK(findMetalTexture("brushed") == nullptr);
  CHECK(findMetalTexture("assets/sint:brushed") == nullptr);

  for (const char* name : NAMES) {
    CAPTURE(name);

    const TextureImage* image = findMetalTexture(name);

    REQUIRE(image != nullptr);
    CHECK(findMetalTexture(std::string(name)) == image);
    CHECK(image->width > 0);
    CHECK(image->height > 0);
    CHECK(image->pixels.size() == (size_t)image->width * (size_t)image->height * 4);
  }
}

TEST_CASE("Текстура металла — белые и чёрные точки с предумноженной прозрачностью") {
  for (const char* name : NAMES) {
    CAPTURE(name);

    const TextureImage& image = *findMetalTexture(name);
    bool hasLight = false;
    bool hasDark = false;
    bool isValid = true;

    for (size_t index = 0; index < image.pixels.size(); index += 4) {
      const Rml::byte red = image.pixels[index];
      const Rml::byte alpha = image.pixels[index + 3];

      isValid = isValid && red == image.pixels[index + 1] && red == image.pixels[index + 2] && (red == 0 || red == alpha);
      hasLight = hasLight || red > 0;
      hasDark = hasDark || (red == 0 && alpha > 0);
    }

    CHECK(isValid);
    CHECK(hasLight);
    CHECK(hasDark);
  }
}

TEST_CASE("Текстура металла повторяется без шва") {
  for (const char* name : NAMES) {
    CAPTURE(name);

    const TextureImage& image = *findMetalTexture(name);
    const Seam columns = measureColumns(image);
    const Seam rows = measureRows(image);

    CHECK(columns.across <= columns.inside);
    CHECK(rows.across <= rows.inside);
  }
}

TEST_CASE("Шлифовка в среднем не меняет тон панели") {
  const TextureImage& image = *findMetalTexture("sint:brushed");
  double sum = 0.0;
  float strongest = 0.0f;

  for (int y = 0; y < image.height; y++) {
    for (int x = 0; x < image.width; x++) {
      sum += valueAt(image, x, y);
      strongest = std::max(strongest, std::fabs(valueAt(image, x, y)));
    }
  }

  CHECK(std::fabs(sum / (image.width * image.height)) < 0.005);
  // Слой тонкий: даже самая заметная точка не перекрывает тон панели
  CHECK(strongest < 0.15f);
}
