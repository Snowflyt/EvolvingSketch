#pragma once

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <random>
#include <type_traits>

#include "../../src/utils/hash.hpp"
#include "../../src/utils/memory.hpp"
#include "../../src/utils/time.hpp"

struct AdaSketchOptions {
  std::function<float(uint32_t t)> f = nullptr;
  uint32_t tuning_interval = 0;
};

template <typename T> class AdaSketch {
public:
  explicit AdaSketch(const size_t size, const AdaSketchOptions &&options = {})
      : k_width_(std::bit_ceil(std::max(size / 4, 8UZ))),
        data_(aligned_alloc<std::remove_pointer_t<decltype(data_)>>(4 * k_width_)), k_f_(options.f),
        k_tuning_interval_(options.tuning_interval) {
    if (!data_)
      throw std::bad_alloc();

    for (size_t i = 0; i < 4 * k_width_; i++)
      data_[i] = 0;

    std::mt19937 gen{std::random_device{}()};
    for (auto &seed : seeds_)
      seed = gen();
  }

  ~AdaSketch() { cleanup(); }

  AdaSketch(const AdaSketch &other)
      : k_width_(other.k_width_),
        data_(aligned_alloc<std::remove_pointer_t<decltype(data_)>>(4 * k_width_)),
        k_f_(other.k_f_), k_tuning_interval_(other.k_tuning_interval_) {
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

  AdaSketch(AdaSketch &&other) noexcept
      : k_width_(other.k_width_), data_(other.data_), k_f_(std::move(other.k_f_)),
        k_tuning_interval_(other.k_tuning_interval_) {
    for (size_t i = 0; i < 4; i++)
      seeds_[i] = other.seeds_[i];

    other.k_width_ = 0;
    other.data_ = nullptr;
    other.k_f_ = nullptr;
    other.k_tuning_interval_ = 0;
  }

  auto operator=(const AdaSketch &other) -> AdaSketch & {
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

    k_f_ = other.k_f_;
    k_tuning_interval_ = other.k_tuning_interval_;

    return *this;
  }

  auto operator=(AdaSketch &&other) noexcept -> AdaSketch & {
    if (this == &other)
      return *this;

    cleanup();

    k_width_ = other.k_width_;
    data_ = other.data_;
    k_f_ = std::move(other.k_f_);
    k_tuning_interval_ = other.k_tuning_interval_;

    for (size_t i = 0; i < 4; i++)
      seeds_[i] = other.seeds_[i];

    other.k_width_ = 0;
    other.data_ = nullptr;
    other.k_f_ = nullptr;
    other.k_tuning_interval_ = 0;

    return *this;
  }

  void update(const T &item) {
    const auto start = get_current_time_in_seconds();

    const auto increment = k_f_(++t_);

    size_t index = hash(item) % k_width_;
    for (size_t i = 0; i < 4; i++) {
      if (i > 0)
        index = alt_index(index, seeds_[i]);
      const size_t pos = i * k_width_ + index;
      data_[pos] += increment;
    }

    if (k_tuning_interval_ && ++tuning_counter_ >= k_tuning_interval_)
      tune();

    total_update_time_seconds_ += get_current_time_in_seconds() - start;
    update_count_++;
  }

  [[nodiscard]] auto estimate(const T &item) const -> float {
    const auto start = get_current_time_in_seconds();

    auto res = std::numeric_limits<std::remove_pointer_t<decltype(data_)>>::max();
    size_t index = hash(item) % k_width_;
    for (size_t i = 0; i < 4; i++) {
      if (i > 0)
        index = alt_index(index, seeds_[i]);
      const size_t pos = i * k_width_ + index;
      res = std::min(res, data_[pos] / k_f_(t_));
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

  float *data_;
  size_t seeds_[4];

  uint32_t t_ = 0;
  std::function<float(uint32_t t)> k_f_;

  uint32_t k_tuning_interval_;
  uint32_t tuning_counter_ = 0;

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

  /**
   * @brief Periodically reset `t` to avoid overflow.
   */
  void tune() {
    const auto d = k_f_(t_);
    for (size_t i = 0; i < 4; i++)
      for (size_t j = 0; j < k_width_; j++)
        data_[i * k_width_ + j] /= d;
    t_ = 0;
    tuning_counter_ = 0;
  }
};
