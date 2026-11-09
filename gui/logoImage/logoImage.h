#pragma once

#include <string_view>

#include "../textureImage/textureImage.h"

/**
 * Размер логотипа в стилях, dp. Картинка вдвое больше, чтобы буквы оставались чёткими при масштабе до 2
 */
constexpr int LOGO_WIDTH_DP = 85;
constexpr int LOGO_HEIGHT_DP = 26;

/**
 * Логотип «SIN-T» (D-095), в стилях image(sint:logo). Буквы заданы полем расстояний до края:
 * из него получаются скруглённые углы, сглаживание, фаска, обводка и тень.
 * Считается один раз на процесс. Пустой указатель — имя не наше
 */
const TextureImage* findLogoImage(std::string_view source);
