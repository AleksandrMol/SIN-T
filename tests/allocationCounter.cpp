#include "./allocationCounter.h"

#include <atomic>
#include <cstdlib>
#include <new>

// Подмена глобальных new и delete в отдельном файле: иначе компилятор встраивает их
// в вызывающий код и ошибочно предупреждает о несовпадении malloc и delete
namespace {
  std::atomic<size_t> s_allocationCount = 0;
  thread_local size_t s_threadMemoryOperations = 0;
}

size_t getAllocationCount() {
  return s_allocationCount;
}

size_t getThreadMemoryOperations() {
  return s_threadMemoryOperations;
}

void* operator new(size_t size) {
  s_allocationCount++;
  s_threadMemoryOperations++;

  if (void* memory = std::malloc(size ? size : 1)) {
    return memory;
  }

  throw std::bad_alloc();
}

// Этот вариант берут стандартные алгоритмы для временных буферов, например std::stable_sort
void* operator new(size_t size, const std::nothrow_t&) noexcept {
  s_allocationCount++;
  s_threadMemoryOperations++;

  return std::malloc(size ? size : 1);
}

void operator delete(void* memory) noexcept {
  s_threadMemoryOperations += memory ? 1 : 0;
  std::free(memory);
}

void operator delete(void* memory, size_t) noexcept {
  s_threadMemoryOperations += memory ? 1 : 0;
  std::free(memory);
}
