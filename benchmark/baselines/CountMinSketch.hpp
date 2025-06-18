#pragma once

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <random>
#include <type_traits>

#include "../../src/utils/hash.hpp"
#include "../../src/utils/memory.hpp"
#include "../../src/utils/time.hpp"

template <typename T> class CountMinSketch {
public:
  explicit CountMinSketch(const size_t size)
      : k_width_(std::bit_ceil(std::max(size / 4, 8UZ))),
        data_(aligned_alloc<std::remove_pointer_t<decltype(data_)>>(4 * k_width_)) {
    if (!data_)
      throw std::bad_alloc();

    for (size_t i = 0; i < 4 * k_width_; i++)
      data_[i] = 0;

    std::mt19937 gen{std::random_device{}()};
    for (auto &seed : seeds_)
      seed = gen();
  }

  ~CountMinSketch() { cleanup(); }

  CountMinSketch(const CountMinSketch &other)
      : k_width_(other.k_width_),
        data_(aligned_alloc<std::remove_pointer_t<decltype(data_)>>(4 * k_width_)) {
    if (!data_)
      throw std::bad_alloc();

    for (size_t i = 0; i < 4; i++)
      for (size_t j = 0; j < k_width_; j++) {
        const size_t pos = i * k_width_ + j;
        data_[pos] = other.data_[pos];
      }

    for (size_t i = 0; i < 4; i++)
      seeds_[i] = other.seeds_[i];
  }

  CountMinSketch(CountMinSketch &&other) noexcept : k_width_(other.k_width_), data_(other.data_) {
    for (size_t i = 0; i < 4; i++)
      seeds_[i] = other.seeds_[i];

    other.k_width_ = 0;
    other.data_ = nullptr;
  }

  auto operator=(const CountMinSketch &other) -> CountMinSketch & {
    if (this == &other)
      return *this;

    cleanup();

    k_width_ = other.k_width_;

    data_ = aligned_alloc<std::remove_pointer_t<decltype(data_)>>(4 * k_width_);
    if (!data_)
      throw std::bad_alloc();

    for (size_t i = 0; i < 4; i++)
      for (size_t j = 0; j < k_width_; j++) {
        const size_t pos = i * k_width_ + j;
        data_[pos] = other.data_[pos];
      }

    for (size_t i = 0; i < 4; i++)
      seeds_[i] = other.seeds_[i];

    return *this;
  }

  auto operator=(CountMinSketch &&other) noexcept -> CountMinSketch & {
    if (this == &other)
      return *this;

    cleanup();

    k_width_ = other.k_width_;
    data_ = other.data_;

    for (size_t i = 0; i < 4; i++)
      seeds_[i] = other.seeds_[i];

    other.k_width_ = 0;
    other.data_ = nullptr;

    return *this;
  }

  void update(const T &item) {
    const auto start = get_current_time_in_seconds();

    size_t index = hash(item) % k_width_;
    for (size_t i = 0; i < 4; i++) {
      if (i > 0)
        index = alt_index(index, seeds_[i]);
      const size_t pos = i * k_width_ + index;
      data_[pos]++;
    }

    total_update_time_seconds_ += get_current_time_in_seconds() - start;
    update_count_++;
  }

  [[nodiscard]] auto estimate(const T &item) const -> uint32_t {
    const auto start = get_current_time_in_seconds();

    auto res = std::numeric_limits<std::remove_pointer_t<decltype(data_)>>::max();
    size_t index = hash(item) % k_width_;
    for (size_t i = 0; i < 4; i++) {
      if (i > 0)
        index = alt_index(index, seeds_[i]);
      const size_t pos = i * k_width_ + index;
      res = std::min(res, data_[pos]);
    }

    total_estimate_time_seconds_ += get_current_time_in_seconds() - start;
    estimate_count_++;

    return res;
  }

  /* Benchmark start */
  [[nodiscard]] auto update_time_avg_seconds() const -> double {
    return total_update_time_seconds_ / update_count_;
  }
  [[nodiscard]] auto estimate_time_avg_seconds() const -> double {
    return total_estimate_time_seconds_ / estimate_count_;
  }
  /* Benchmark end */

private:
  size_t k_width_;

  uint32_t *data_;
  size_t seeds_[4];

  /* Benchmark start */
  mutable size_t update_count_ = 0;
  mutable double total_update_time_seconds_ = 0.0;
  mutable size_t estimate_count_ = 0;
  mutable double total_estimate_time_seconds_ = 0.0;
  /* Benchmark end */

  void cleanup() {
    if (data_) {
      aligned_free(data_);
      data_ = nullptr;
    }
  }

  [[nodiscard]] auto alt_index(const size_t index, const size_t seed) const -> size_t {
    // A quick and dirty way to generate an alternative index
    // 0x5bd1e995 is the hash constant from MurmurHash2
    return (index ^ (seed * 0x5bd1e995)) % k_width_;
  }
};
