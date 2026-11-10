#pragma once

#include <RmlUi/Core.h>

// Акцент редактора: значение ручки, аудиопровод, волна на экране. В стилях это #3d8de8
inline const Rml::Colourb ACCENT_COLOR(61, 141, 232);

// Модуляция: дуга и полоска размаха, провод модуляции. В стилях это #f0923a
inline const Rml::Colourb MODULATION_COLOR(240, 146, 58);

/**
 * Цвет между двумя, amount 0..1 от первого ко второму. Каналы смешиваются вместе с прозрачностью
 */
Rml::Colourb mixColor(Rml::Colourb from, Rml::Colourb to, float amount);

Rml::Colourb withAlpha(Rml::Colourb color, Rml::byte alpha);
