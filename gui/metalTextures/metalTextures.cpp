#include "./metalTextures.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <utility>
#include <vector>

namespace {
  constexpr int SIZE = 512;

  // Сила слоёв в долях непрозрачности: 0,05 — это 13 из 255
  constexpr float BRUSHING_STRENGTH = 0.05f;
  constexpr float BLOTCH_STRENGTH = 0.035f;
  constexpr float SCRATCH_STRENGTH = 0.16f;
  constexpr float PIT_STRENGTH = 0.12f;

  constexpr int SCRATCH_COUNT = 70;
  constexpr int PIT_COUNT = 40;

  enum class LAYER : uint32_t {
    BRUSHING = 1,
    BLOTCHES = 2,
    SCRATCHES = 3,
    PITS = 4,
  };

  /**
   * Целочисленный хеш вместо std::*_distribution: у тех результат зависит от стандартной библиотеки,
   * а текстура должна быть одинаковой везде (lowbias32, Chris Wellons)
   */
  uint32_t hash(uint32_t value) {
    value ^= value >> 16;
    value *= 0x7feb352dU;
    value ^= value >> 15;
    value *= 0x846ca68bU;
    value ^= value >> 16;

    return value;
  }

  uint32_t hash(uint32_t a, uint32_t b, uint32_t c = 0) {
    return hash(a ^ hash(b ^ hash(c)));
  }

  /** 0..1 */
  float unit(uint32_t value) {
    return (float)(value >> 8) / 16777216.0f;
  }

  /** -1..1 */
  float signedUnit(uint32_t value) {
    return unit(value) * 2.0f - 1.0f;
  }

  float smooth(float t) {
    return t * t * (3.0f - 2.0f * t);
  }

  int wrap(int value, int period) {
    return ((value % period) + period) % period;
  }

  /**
   * Шум вдоль одной оси, повторяется через period узлов: края плитки сходятся
   */
  float noise1(float x, int period, uint32_t seed) {
    const float cell = std::floor(x);
    const int index = (int)cell;
    const float t = smooth(x - cell);
    const float left = signedUnit(hash((uint32_t)wrap(index, period), seed));
    const float right = signedUnit(hash((uint32_t)wrap(index + 1, period), seed));

    return left + (right - left) * t;
  }

  float noise2(float x, float y, int period, uint32_t seed) {
    const float cellX = std::floor(x);
    const float cellY = std::floor(y);
    const int indexX = (int)cellX;
    const int indexY = (int)cellY;
    const float tx = smooth(x - cellX);
    const float ty = smooth(y - cellY);
    const auto at = [period, seed](int ix, int iy) {
      return signedUnit(hash((uint32_t)wrap(ix, period), (uint32_t)wrap(iy, period), seed));
    };
    const float top = at(indexX, indexY) + (at(indexX + 1, indexY) - at(indexX, indexY)) * tx;
    const float bottom = at(indexX, indexY + 1) + (at(indexX + 1, indexY + 1) - at(indexX, indexY + 1)) * tx;

    return top + (bottom - top) * ty;
  }

  /**
   * Поле яркости со знаком: плюс светлее, минус темнее, единица — полная непрозрачность
   */
  class Field {
    public:
      Field() : values((size_t)SIZE * SIZE, 0.0f) {}

      float& at(int x, int y) {
        return this->values[(size_t)wrap(y, SIZE) * SIZE + (size_t)wrap(x, SIZE)];
      }

      /**
       * Точка с дробными координатами делится между четырьмя соседями, за краем продолжается с другой стороны
       */
      void splat(float x, float y, float amount) {
        const float cellX = std::floor(x);
        const float cellY = std::floor(y);
        const float tx = x - cellX;
        const float ty = y - cellY;

        this->at((int)cellX, (int)cellY) += amount * (1.0f - tx) * (1.0f - ty);
        this->at((int)cellX + 1, (int)cellY) += amount * tx * (1.0f - ty);
        this->at((int)cellX, (int)cellY + 1) += amount * (1.0f - tx) * ty;
        this->at((int)cellX + 1, (int)cellY + 1) += amount * tx * ty;
      }

      /**
       * Добавить другой слой точка в точку
       */
      void add(const Field& layer) {
        for (size_t index = 0; index < this->values.size(); index++) {
          this->values[index] += layer.values[index];
        }
      }

      /**
       * Шлифовка: у каждой строки свой ровный сдвиг яркости, вдоль неё тянутся длинные полосы
       */
      void addBrushing(float strength) {
        const uint32_t layer = (uint32_t)LAYER::BRUSHING;

        for (int y = 0; y < SIZE; y++) {
          const uint32_t seed = hash((uint32_t)y, layer);
          const float shift = unit(hash((uint32_t)y, layer, 1)) * 64.0f;
          const float level = signedUnit(hash((uint32_t)y, layer, 2)) * 0.35f;

          for (int x = 0; x < SIZE; x++) {
            const float position = (float)x + shift;
            const float streak = 0.6f * noise1(position / 32.0f, SIZE / 32, seed) + 0.4f * noise1(position / 8.0f, SIZE / 8, seed ^ 0x9e3779b9U);

            this->at(x, y) += (level + streak * 0.65f) * strength;
          }
        }
      }

      /**
       * Крупные пятна: участки, которых чаще касались руки и инструменты
       */
      void addBlotches(float strength) {
        const uint32_t layer = (uint32_t)LAYER::BLOTCHES;

        for (int y = 0; y < SIZE; y++) {
          for (int x = 0; x < SIZE; x++) {
            const float fx = (float)x;
            const float fy = (float)y;
            const float value = 0.6f * noise2(fx / 128.0f, fy / 128.0f, SIZE / 128, layer)
              + 0.3f * noise2(fx / 64.0f, fy / 64.0f, SIZE / 64, layer + 10)
              + 0.1f * noise2(fx / 32.0f, fy / 32.0f, SIZE / 32, layer + 20);

            this->at(x, y) += value * strength;
          }
        }
      }

      /**
       * Микроцарапины: тонкие отрезки, сужаются к концам. Большинство светлые и почти вдоль шлифовки
       */
      void addScratches(int count, float strength) {
        for (int index = 0; index < count; index++) {
          const auto random = [index](uint32_t slot) {
            return unit(hash((uint32_t)index, (uint32_t)LAYER::SCRATCHES, slot));
          };
          const bool isAlong = random(0) < 0.7f;
          const float angle = isAlong ? (random(1) - 0.5f) * 0.7f : random(1) * std::numbers::pi_v<float>;
          const float length = 10.0f + random(2) * 80.0f;
          const float sign = random(3) < 0.85f ? 1.0f : -1.0f;
          const float amount = sign * (0.6f + random(4)) * strength;
          const float startX = random(5) * SIZE;
          const float startY = random(6) * SIZE;
          const float directionX = std::cos(angle);
          const float directionY = std::sin(angle);

          for (float t = 0.0f; t <= length; t += 0.35f) {
            const float taper = std::sin(std::numbers::pi_v<float> * t / length);

            // Шаг меньше пикселя, поэтому вклад точки уменьшен, иначе линия вышла бы слишком яркой
            this->splat(startX + directionX * t, startY + directionY * t, amount * taper * 0.35f);
          }
        }
      }

      /**
       * Мелкие тёмные точки: выбоины в покрытии
       */
      void addPits(int count, float strength) {
        const uint32_t layer = (uint32_t)LAYER::PITS;

        for (int index = 0; index < count; index++) {
          const float x = unit(hash((uint32_t)index, layer, 0)) * SIZE;
          const float y = unit(hash((uint32_t)index, layer, 1)) * SIZE;

          this->splat(x, y, -strength * (0.5f + unit(hash((uint32_t)index, layer, 2))));
        }
      }

      TextureImage toImage() const {
        TextureImage image;

        image.width = SIZE;
        image.height = SIZE;
        image.pixels.resize((size_t)SIZE * SIZE * 4);

        for (size_t index = 0; index < this->values.size(); index++) {
          const float value = this->values[index];
          const auto alpha = (Rml::byte)std::lround(std::clamp(std::fabs(value), 0.0f, 1.0f) * 255.0f);
          // Предумноженный цвет: у белого каналы равны прозрачности, у чёрного нули
          const Rml::byte light = value > 0.0f ? alpha : 0;

          image.pixels[index * 4 + 0] = light;
          image.pixels[index * 4 + 1] = light;
          image.pixels[index * 4 + 2] = light;
          image.pixels[index * 4 + 3] = alpha;
        }

        return image;
      }

    private:
      std::vector<float> values;
  };

  struct MetalTextures {
    TextureImage brushed;
    TextureImage worn;
    TextureImage scuffed;
  };

  /**
   * Все три текстуры сразу: шлифовка у brushed и worn общая, пятна у worn и scuffed тоже,
   * поэтому каждый слой считается один раз. Редактор подключает все три, отдельно их не просят
   */
  MetalTextures makeTextures() {
    Field brushing;
    Field blotches;

    brushing.addBrushing(BRUSHING_STRENGTH);
    blotches.addBlotches(BLOTCH_STRENGTH);

    Field worn = brushing;

    worn.add(blotches);
    worn.addScratches(SCRATCH_COUNT, SCRATCH_STRENGTH);
    worn.addPits(PIT_COUNT, PIT_STRENGTH);

    Field scuffed = std::move(blotches);

    scuffed.addScratches(SCRATCH_COUNT / 2, SCRATCH_STRENGTH * 0.6f);
    scuffed.addPits(PIT_COUNT / 2, PIT_STRENGTH);

    return { brushing.toImage(), worn.toImage(), scuffed.toImage() };
  }
}

const TextureImage* findMetalTexture(std::string_view source) {
  if (source != "sint:brushed" && source != "sint:worn" && source != "sint:scuffed") {
    return nullptr;
  }

  static const MetalTextures textures = makeTextures();

  return source == "sint:brushed" ? &textures.brushed : source == "sint:worn" ? &textures.worn : &textures.scuffed;
}
