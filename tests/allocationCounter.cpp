#include "./allocationCounter.h"

#include <atomic>
#include <cstdlib>
#include <new>

// Подмена глобальных new и delete в отдельном файле: иначе компилятор встраивает их
// в вызывающий код и ошибочно предупреждает о несовпадении malloc и delete
namespace {
  std::atomic<size_t> s_allocationCount = 0;
}

size_t getAllocationCount() {
  return s_allocationCount;
}

void* operator new(size_t size) {
  s_allocationCount++;

  if (void* memory = std::malloc(size ? size : 1)) {
    return memory;
  }

  throw std::bad_alloc();
}

void operator delete(void* memory) noexcept {
  std::free(memory);
}

void operator delete(void* memory, size_t) noexcept {
  std::free(memory);
}
