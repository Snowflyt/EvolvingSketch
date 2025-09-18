#pragma once

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <random>
#include <type_traits>

#include "../../src/adapters/adapter.hpp"
#include "../../src/utils/hash.hpp"
#include "../../src/utils/memory.hpp"
#include "../../src/utils/time.hpp"

template <typename F>
  requires std::is_invocable_r_v<float, F, uint32_t, double>
struct EvolvingSketchOptimOptions {
  double initial_alpha = 1.0;
  F f;
  Adapter<double, double> *adapter = nullptr;
  uint32_t adapt_interval = 0;
};

/**
 * @brief A variant of Evolving Sketch designed specially for hit rate (or similar) optimization
 * that may have better performance than regular Evolving Sketch.
 *
 * Note that this version only performs better performance than regular Evolving Sketch when
 * adaptation is enabled (i.e., adapter is not nullptr).
 */
template <typename T, typename F, typename SumType = size_t>
  requires std::is_invocable_r_v<float, F, uint32_t, double>
class EvolvingSketchOptim {
private:
  // Safe threshold for pruning to avoid float overflow
  // This is the max safe threshold where +1 would not be omitted
  static constexpr float PRUNE_THRESHOLD = 16777215.0F;

public:
  // NOLINTNEXTLINE
  SumType sum = 0;

  explicit EvolvingSketchOptim(const size_t size, const EvolvingSketchOptimOptions<F> &options)
      : k_width_(std::bit_ceil(std::max(size / 4, 8UZ))),
        data_(aligned_alloc<std::remove_pointer_t<decltype(data_)>>(4 * k_width_)), k_f_(options.f),
        k_adapter_(options.adapter), alpha_(options.initial_alpha),
        k_adapt_interval_(options.adapt_interval) {
    if (!data_)
      throw std::bad_alloc();

    for (size_t i = 0; i < 4 * k_width_; i++)
      data_[i] = 0;

    std::mt19937 gen{std::random_device{}()};
    for (auto &seed : seeds_)
      seed = gen();
  }

  ~EvolvingSketchOptim() { cleanup(); }

  EvolvingSketchOptim(const EvolvingSketchOptim &other)
      : k_width_(other.k_width_),
        data_(aligned_alloc<std::remove_pointer_t<decltype(data_)>>(4 * k_width_)),
        k_f_(other.k_f_), k_adapter_(other.k_adapter_), alpha_(other.alpha_),
        k_adapt_interval_(other.k_adapt_interval_) {
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

  EvolvingSketchOptim(EvolvingSketchOptim &&other) noexcept
      : k_width_(other.k_width_), data_(other.data_), k_f_(std::move(other.k_f_)),
        k_adapter_(other.k_adapter_), alpha_(other.alpha_),
        k_adapt_interval_(other.k_adapt_interval_) {
    for (size_t i = 0; i < 4; i++)
      seeds_[i] = other.seeds_[i];

    other.k_width_ = 0;
    other.data_ = nullptr;
    other.k_adapter_ = nullptr;
    other.alpha_ = 0.0;
    other.k_adapt_interval_ = 0;
    other.adapt_counter_ = 0;
  }

  auto operator=(const EvolvingSketchOptim &other) -> EvolvingSketchOptim & {
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
    k_adapter_ = other.k_adapter_;
    alpha_ = other.alpha_;
    k_adapt_interval_ = other.k_adapt_interval_;
    adapt_counter_ = other.adapt_counter_;

    return *this;
  }

  auto operator=(EvolvingSketchOptim &&other) noexcept -> EvolvingSketchOptim & {
    if (this == &other)
      return *this;

    cleanup();

    k_width_ = other.k_width_;
    data_ = other.data_;
    k_f_ = std::move(other.k_f_);
    k_adapter_ = other.k_adapter_;
    alpha_ = other.alpha_;
    k_adapt_interval_ = other.k_adapt_interval_;
    adapt_counter_ = other.adapt_counter_;

    for (size_t i = 0; i < 4; i++)
      seeds_[i] = other.seeds_[i];

    other.k_width_ = 0;
    other.data_ = nullptr;
    other.k_adapter_ = nullptr;
    other.alpha_ = 0.0;
    other.k_adapt_interval_ = 0;
    other.adapt_counter_ = 0;

    return *this;
  }

  void update(const T &item) {
    const auto start = get_current_time_in_seconds();

  retry_update:
    const auto increment = k_f_(++t_, alpha_);

    // For rollback if overflow detected
    size_t counter_positions[4];
    float original_counters[4];

    // Increment counters
    bool overflow_detected = false;
    size_t index = hash(item) % k_width_;
    size_t i;
    for (i = 0; i < 4; i++) {
      if (i > 0)
        index = alt_index(index, seeds_[i]);
      const size_t pos = i * k_width_ + index;
      auto &v = data_[pos];
      if (v > PRUNE_THRESHOLD - increment) {
        overflow_detected = true;
        break;
      }
      counter_positions[i] = pos;
      original_counters[i] = v;
      v += increment;
    }

    // If overflow detected, rollback written counters
    if (overflow_detected) {
      for (size_t j = 0; j < i; j++)
        data_[counter_positions[j]] = original_counters[j];
      t_--;
      prune();
      // NOLINTNEXTLINE(cppcoreguidelines-avoid-goto)
      goto retry_update;
    }

    if (k_adapt_interval_ && ++adapt_counter_ >= k_adapt_interval_)
      adapt();

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
      res = std::min(res, data_[pos] / k_f_(t_, alpha_));
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
  double alpha_;
  F k_f_;

  uint32_t k_adapt_interval_;
  uint32_t adapt_counter_ = 0;

  Adapter<double, double> *k_adapter_;

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
   * @brief Periodically reset 't' and prune counters to avoid overflow.
   */
  void prune() {
    const auto d = k_f_(t_, alpha_);
    for (size_t i = 0; i < 4; i++)
      for (size_t j = 0; j < k_width_; j++)
        data_[i * k_width_ + j] /= d;
    t_ = 0;
  }

  /**
   * @brief Periodically adapt alpha.
   */
  void adapt() {
    prune();
    const double normalized_sum = static_cast<double>(sum) / static_cast<double>(k_adapt_interval_);
    sum = 0; // Reset for the next interval
    alpha_ = (*k_adapter_)(normalized_sum, alpha_);
    adapt_counter_ = 0;
  }
};
