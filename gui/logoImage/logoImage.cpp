#include "./logoImage.h"

#include <algorithm>
#include <cmath>
#include <iterator>

namespace {
  constexpr int SCALE = 2;
  constexpr int WIDTH = LOGO_WIDTH_DP * SCALE;
  constexpr int HEIGHT = LOGO_HEIGHT_DP * SCALE;

  // Высота буквы в пикселях картинки, остальное в долях высоты буквы
  constexpr float LETTER_HEIGHT = 36.0f;
  constexpr float STROKE = 0.24f;
  constexpr float RADIUS = 0.14f;
  constexpr float GAP = 0.16f;
  constexpr float S_WIDTH = 1.0f;
  constexpr float N_WIDTH = 0.95f;
  constexpr float DASH_WIDTH = 0.42f;
  constexpr float T_WIDTH = 1.0f;
  constexpr float LETTERS_WIDTH = S_WIDTH + STROKE + N_WIDTH + DASH_WIDTH + T_WIDTH + 4.0f * GAP;

  // В пикселях картинки
  constexpr float BEVEL = 3.6f;
  constexpr float OUTLINE = 1.6f;
  constexpr float SHADOW_BLUR = 3.0f;
  constexpr float SHADOW_OFFSET_X = 1.5f;
  constexpr float SHADOW_OFFSET_Y = 2.5f;
  constexpr float SHADOW_ALPHA = 0.55f;
  constexpr float OUTLINE_ALPHA = 0.9f;

  struct Point {
    float x = 0.0f;
    float y = 0.0f;
  };

  struct Color {
    float red = 0.0f;
    float green = 0.0f;
    float blue = 0.0f;
  };

  struct Stop {
    float position;
    Color color;
  };

  // Холодный хром: светлый верх, тёмная линия горизонта посередине, светлый низ
  constexpr Stop CHROME[] = {
    { 0.00f, { 232.0f, 241.0f, 252.0f } },
    { 0.40f, { 150.0f, 184.0f, 220.0f } },
    { 0.50f, { 52.0f, 86.0f, 128.0f } },
    { 0.62f, { 88.0f, 130.0f, 180.0f } },
    { 1.00f, { 178.0f, 206.0f, 236.0f } },
  };

  constexpr Color HIGHLIGHT = { 255.0f, 255.0f, 255.0f };
  constexpr Color DEPTH = { 10.0f, 20.0f, 35.0f };
  constexpr Color OUTLINE_COLOR = { 4.0f, 7.0f, 11.0f };

  // Направление на свет: сверху слева, как у панелей и ручек
  const Point LIGHT = [] {
    const float length = std::hypot(1.0f, 1.2f);

    return Point{ -1.0f / length, -1.2f / length };
  }();

  Color mix(Color from, Color to, float amount) {
    return {
      from.red + (to.red - from.red) * amount,
      from.green + (to.green - from.green) * amount,
      from.blue + (to.blue - from.blue) * amount,
    };
  }

  Color chromeAt(float position) {
    const float clamped = std::clamp(position, 0.0f, 1.0f);

    for (size_t index = 1; index < std::size(CHROME); index++) {
      if (clamped <= CHROME[index].position) {
        const Stop& from = CHROME[index - 1];
        const Stop& to = CHROME[index];

        return mix(from.color, to.color, (clamped - from.position) / (to.position - from.position));
      }
    }

    return CHROME[std::size(CHROME) - 1].color;
  }

  /**
   * Расстояние до края прямоугольника со скруглёнными углами: минус внутри
   */
  float roundedBox(Point point, float left, float top, float right, float bottom, float radius) {
    const float halfWidth = (right - left) * 0.5f;
    const float halfHeight = (bottom - top) * 0.5f;
    const float corner = std::min({ radius, halfWidth, halfHeight });
    const float qx = std::fabs(point.x - (left + right) * 0.5f) - halfWidth + corner;
    const float qy = std::fabs(point.y - (top + bottom) * 0.5f) - halfHeight + corner;

    return std::hypot(std::max(qx, 0.0f), std::max(qy, 0.0f)) + std::min(std::max(qx, qy), 0.0f) - corner;
  }

  /**
   * Брусок вдоль отрезка с прямыми концами
   */
  float slantedBar(Point point, Point from, Point to, float halfWidth) {
    const float dx = to.x - from.x;
    const float dy = to.y - from.y;
    const float length = std::hypot(dx, dy);
    const float ux = dx / length;
    const float uy = dy / length;
    const float px = point.x - (from.x + to.x) * 0.5f;
    const float py = point.y - (from.y + to.y) * 0.5f;
    const float qx = std::fabs(px * ux + py * uy) - length * 0.5f;
    const float qy = std::fabs(-px * uy + py * ux) - halfWidth;

    return std::hypot(std::max(qx, 0.0f), std::max(qy, 0.0f)) + std::min(std::max(qx, qy), 0.0f);
  }

  /**
   * Расстояние до края букв «SIN-T» в долях высоты буквы. Буквы собраны из брусков, их объединение — минимум
   */
  float distanceToLetters(Point point) {
    constexpr float HALF = STROKE * 0.5f;
    float distance = 1e9f;
    float x = 0.0f;
    const auto add = [&distance](float value) {
      distance = std::min(distance, value);
    };

    add(roundedBox(point, x, 0.0f, x + S_WIDTH, STROKE, RADIUS));
    add(roundedBox(point, x, 0.0f, x + STROKE, 0.5f + HALF, RADIUS));
    add(roundedBox(point, x, 0.5f - HALF, x + S_WIDTH, 0.5f + HALF, RADIUS));
    add(roundedBox(point, x + S_WIDTH - STROKE, 0.5f - HALF, x + S_WIDTH, 1.0f, RADIUS));
    add(roundedBox(point, x, 1.0f - STROKE, x + S_WIDTH, 1.0f, RADIUS));
    x += S_WIDTH + GAP;

    add(roundedBox(point, x, 0.0f, x + STROKE, 1.0f, RADIUS));
    x += STROKE + GAP;

    // Диагональ N обрезана рамкой буквы, иначе её концы торчали бы над стойками
    add(roundedBox(point, x, 0.0f, x + STROKE, 1.0f, RADIUS));
    add(roundedBox(point, x + N_WIDTH - STROKE, 0.0f, x + N_WIDTH, 1.0f, RADIUS));
    add(std::max(slantedBar(point, { x + HALF, 0.0f }, { x + N_WIDTH - HALF, 1.0f }, HALF * 1.1f), roundedBox(point, x, 0.0f, x + N_WIDTH, 1.0f, 0.0f)));
    x += N_WIDTH + GAP;

    add(roundedBox(point, x, 0.5f - HALF * 0.9f, x + DASH_WIDTH, 0.5f + HALF * 0.9f, RADIUS * 0.6f));
    x += DASH_WIDTH + GAP;

    add(roundedBox(point, x, 0.0f, x + T_WIDTH, STROKE, RADIUS));
    add(roundedBox(point, x + T_WIDTH * 0.5f - HALF, 0.0f, x + T_WIDTH * 0.5f + HALF, 1.0f, RADIUS));

    return distance;
  }

  /**
   * Расстояние в пикселях картинки до края букв от центра пикселя
   */
  float distanceAt(float x, float y) {
    const float originX = ((float)WIDTH - LETTERS_WIDTH * LETTER_HEIGHT) * 0.5f;
    const float originY = ((float)HEIGHT - LETTER_HEIGHT) * 0.5f;

    return distanceToLetters({ (x - originX) / LETTER_HEIGHT, (y - originY) / LETTER_HEIGHT }) * LETTER_HEIGHT;
  }

  float coverage(float distance) {
    return std::clamp(0.5f - distance, 0.0f, 1.0f);
  }

  struct Pixel {
    Color color;
    float alpha = 0.0f;

    /**
     * Слой поверх: обычное наложение с предумноженной прозрачностью
     */
    void over(Color layer, float layerAlpha) {
      this->color = {
        layer.red * layerAlpha + this->color.red * (1.0f - layerAlpha),
        layer.green * layerAlpha + this->color.green * (1.0f - layerAlpha),
        layer.blue * layerAlpha + this->color.blue * (1.0f - layerAlpha),
      };
      this->alpha = layerAlpha + this->alpha * (1.0f - layerAlpha);
    }
  };

  Pixel shade(float x, float y) {
    const float originY = ((float)HEIGHT - LETTER_HEIGHT) * 0.5f;
    const float distance = distanceAt(x, y);
    const float shadowDistance = distanceAt(x - SHADOW_OFFSET_X, y - SHADOW_OFFSET_Y) - OUTLINE;
    Pixel pixel;

    pixel.over({}, SHADOW_ALPHA * std::clamp((SHADOW_BLUR - shadowDistance) / (2.0f * SHADOW_BLUR), 0.0f, 1.0f));
    pixel.over(OUTLINE_COLOR, OUTLINE_ALPHA * coverage(distance - OUTLINE));

    const float fill = coverage(distance);

    if (fill <= 0.0f) {
      return pixel;
    }

    Color color = chromeAt((y - originY) / LETTER_HEIGHT);
    const float edge = std::clamp(1.0f + distance / BEVEL, 0.0f, 1.0f);

    // Фаска: край, повёрнутый к свету, светлее, противоположный темнее. Нормаль — наружу, по росту расстояния
    if (edge > 0.0f) {
      const float nx = distanceAt(x + 0.5f, y) - distanceAt(x - 0.5f, y);
      const float ny = distanceAt(x, y + 0.5f) - distanceAt(x, y - 0.5f);
      const float length = std::hypot(nx, ny);

      if (length > 0.0f) {
        const float lit = (nx * LIGHT.x + ny * LIGHT.y) / length;

        color = lit > 0.0f ? mix(color, HIGHLIGHT, edge * lit * 0.75f) : mix(color, DEPTH, edge * -lit * 0.7f);
      }
    }

    pixel.over(color, fill);

    return pixel;
  }

  TextureImage makeLogo() {
    TextureImage image;

    image.width = WIDTH;
    image.height = HEIGHT;
    image.pixels.resize((size_t)WIDTH * HEIGHT * 4);

    for (int y = 0; y < HEIGHT; y++) {
      for (int x = 0; x < WIDTH; x++) {
        const Pixel pixel = shade((float)x + 0.5f, (float)y + 0.5f);
        const size_t index = ((size_t)y * WIDTH + (size_t)x) * 4;
        // Цвет уже умножен на прозрачность при наложении слоёв
        const auto toByte = [](float value) {
          return (Rml::byte)std::lround(std::clamp(value, 0.0f, 255.0f));
        };

        image.pixels[index + 0] = toByte(pixel.color.red);
        image.pixels[index + 1] = toByte(pixel.color.green);
        image.pixels[index + 2] = toByte(pixel.color.blue);
        image.pixels[index + 3] = toByte(pixel.alpha * 255.0f);
      }
    }

    return image;
  }
}

const TextureImage* findLogoImage(std::string_view source) {
  if (source != "sint:logo") {
    return nullptr;
  }

  static const TextureImage logo = makeLogo();

  return &logo;
}
