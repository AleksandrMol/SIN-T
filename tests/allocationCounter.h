#pragma once

#include <cstddef>

/**
 * Сколько раз программа выделила память через new. Нужен для проверки того,
 * что аудиопоток не выделяет память (D-023)
 */
size_t getAllocationCount();
