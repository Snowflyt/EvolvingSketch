#pragma once

#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstring>

#ifdef _WIN32
#include <malloc.h>
#else
#include <cstdlib>
#endif

template <typename T> [[nodiscard]] inline auto aligned_alloc(size_t size) -> T * {
  void *ptr = nullptr;
#ifdef _WIN32
  ptr = _aligned_malloc(size * sizeof(T), 64);
  if (!ptr)
    return nullptr;
#else
  if (posix_memalign(&ptr, 64, size * sizeof(T)) != 0)
    return nullptr;
#endif
  return reinterpret_cast<T *>(ptr);
}

template <typename U>
[[nodiscard]] inline auto aligned_alloc_atomic(size_t size) -> std::atomic<U> * {
  void *ptr = nullptr;
#ifdef _WIN32
  ptr = _aligned_malloc(size * sizeof(std::atomic<U>), 64);
  if (!ptr)
    return nullptr;
#else
  if (posix_memalign(&ptr, 64, size * sizeof(std::atomic<U>)) != 0)
    return nullptr;
#endif
  return reinterpret_cast<std::atomic<U> *>(ptr);
}

template <typename T> inline void aligned_free(T *ptr) {
#ifdef _WIN32
  _aligned_free(ptr);
#else
  free(ptr);
#endif
}

template <typename U> inline void aligned_free_atomic(std::atomic<U> *ptr) {
#ifdef _WIN32
  _aligned_free(ptr);
#else
  free(ptr);
#endif
}
