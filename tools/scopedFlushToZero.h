#pragma once

#if defined(__SSE__) || defined(_M_X64)
#include <xmmintrin.h>
#define SINT_HAS_FLUSH_TO_ZERO
#endif

/**
 * На время своей жизни считает субнормальные числа нулём (D-062). На них процессор работает в десятки
 * раз медленнее, а затихающий фильтр может застрять на таком значении, пока звучит голос.
 * Настройка принадлежит потоку, а поток — хосту, поэтому в конце возвращается прежнее значение.
 * Платформозависимая часть собрана здесь: на других процессорах класс ничего не делает
 */
class ScopedFlushToZero {
  public:
#ifdef SINT_HAS_FLUSH_TO_ZERO
    ScopedFlushToZero() : saved(_mm_getcsr()) {
      _mm_setcsr(this->saved | FLUSH_RESULTS_TO_ZERO | READ_INPUTS_AS_ZERO);
    }

    ~ScopedFlushToZero() {
      _mm_setcsr(this->saved);
    }
#else
    ScopedFlushToZero() {}
#endif

    ScopedFlushToZero(const ScopedFlushToZero&) = delete;
    ScopedFlushToZero& operator=(const ScopedFlushToZero&) = delete;

#ifdef SINT_HAS_FLUSH_TO_ZERO
  private:
    static constexpr unsigned int FLUSH_RESULTS_TO_ZERO = 0x8000;
    static constexpr unsigned int READ_INPUTS_AS_ZERO = 0x0040;

    unsigned int saved;
#endif
};
