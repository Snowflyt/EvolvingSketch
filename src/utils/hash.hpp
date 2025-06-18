#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <type_traits>

#include "hash_functions/murmur.hpp"

template <typename T>
[[nodiscard]] inline auto hash32(const T &item, const uint32_t seed = 42) -> uint32_t {
#if defined(__x86_64__) || defined(__aarch64__) || defined(_WIN64) || defined(__LP64__)
  // 64-bit hash proves to be faster on 64-bit platforms even we just need 32 bits of hash value
  if constexpr (std::is_integral_v<T> || std::is_enum_v<T>)
    return murmur_hash2_x64_64(&item, sizeof(T), seed);
  else if constexpr (std::is_same_v<T, std::string>)
    return murmur_hash2_x64_64(item.c_str(), item.size(), seed);
  else if constexpr (std::is_convertible_v<T, const char *>)
    return murmur_hash2_x64_64(static_cast<const char *>(item),
                               std::strlen(static_cast<const char *>(item)), seed);
  else
    return std::hash<T>{}(item);
#else
  if constexpr (std::is_integral_v<T> || std::is_enum_v<T>)
    return murmur_hash2_x86_32(&item, sizeof(T), seed);
  else if constexpr (std::is_same_v<T, std::string>)
    return murmur_hash2_x86_32(item.c_str(), item.size(), seed);
  else if constexpr (std::is_same_v<T, const char *>)
    return murmur_hash2_x86_32(item, std::strlen(item), seed);
  else
    return std::hash<T>{}(item);
#endif
}

template <typename T>
[[nodiscard]] inline auto hash64(const T &item, const uint64_t seed = 42) -> uint64_t {
  if constexpr (std::is_integral_v<T> || std::is_enum_v<T>)
    return murmur_hash2_x64_64(&item, sizeof(T), seed);
  else if constexpr (std::is_same_v<T, std::string>)
    return murmur_hash2_x64_64(item.c_str(), item.size(), seed);
  else if constexpr (std::is_convertible_v<T, const char *>)
    return murmur_hash2_x64_64(static_cast<const char *>(item),
                               std::strlen(static_cast<const char *>(item)), seed);
  else
    return std::hash<T>{}(item);
}

template <typename T>
[[nodiscard]] inline auto hash(const T &item, const size_t seed = 42) -> size_t {
#if defined(__x86_64__) || defined(__aarch64__) || defined(_WIN64) || defined(__LP64__)
  return hash64(item, seed);
#else
  return hash32(item, seed);
#endif
}
