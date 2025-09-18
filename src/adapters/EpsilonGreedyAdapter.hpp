#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <functional>
#include <iterator>
#include <random>
#include <utility>
#include <variant>
#include <vector>

#include "adapter.hpp"

inline constexpr double EPSILON = 0.1; // Exploration rate

class EpsilonGreedyAdapter : public Adapter<double, double> {
public:
  explicit EpsilonGreedyAdapter(
      double min_param, double max_param, size_t num_arms, double epsilon = EPSILON,
      std::variant<double, std::function<double(const size_t n)>> step =
          [](const size_t n) { return 1.0 / static_cast<double>(n); })
      : k_num_arms_(num_arms), k_epsilon_(epsilon),
        k_is_step_constant_(std::holds_alternative<double>(step)),
        k_step_constant_(k_is_step_constant_ ? std::get<double>(step) : 0.0),
        k_step_function_(k_is_step_constant_
                             ? nullptr
                             : std::move(std::get<std::function<double(const size_t n)>>(step))),
        int_dist_{0, k_num_arms_ - 1} {
    arms_.resize(k_num_arms_);
    estimates_.resize(k_num_arms_, 0.0);
    updated_counts_.resize(k_num_arms_, 0);

    // // Initialize arms (Linear space distribution)
    // for (size_t i = 0; i < num_arms_; ++i)
    //   arms_[i] = min_param + (max_param - min_param) * static_cast<double>(i) /
    //                              static_cast<double>(num_arms_ - 1);

    // Initialize arms (Logarithmic space distribution)
    const double log_min = std::log(min_param);
    const double log_max = std::log(max_param);
    for (size_t i = 0; i < k_num_arms_; ++i)
      arms_[i] = std::exp(log_min + (log_max - log_min) * static_cast<double>(i) /
                                        static_cast<double>(k_num_arms_ - 1));
  }

protected:
  auto disturb_param(const double & /*param*/) -> double override {
    std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<size_t> dist(0, k_num_arms_ - 1);
    current_arm_ = dist(rng);
    return arms_[current_arm_];
  }

  auto adapt(const double &obj, const double & /*last_obj*/, const double & /*param*/,
             const double & /*last_param*/) -> double override {
    const double reward = obj;

    // Incremental update
    const double step =
        k_is_step_constant_ ? k_step_constant_ : k_step_function_(++updated_counts_[current_arm_]);
    estimates_[current_arm_] += step * (reward - estimates_[current_arm_]);

    // ε-greedy selection
    if (real_dist_(rng_) < k_epsilon_)
      // Explore: random arm
      current_arm_ = int_dist_(rng_);
    else
      // Exploit: best arm so far
      current_arm_ = get_best_arm();

    return arms_[current_arm_];
  }

private:
  size_t k_num_arms_;
  double k_epsilon_;
  bool k_is_step_constant_;
  double k_step_constant_;
  std::function<double(const size_t n)> k_step_function_;

  std::vector<double> arms_;
  std::vector<double> estimates_;
  std::vector<size_t> updated_counts_;

  size_t current_arm_ = 0;

  mutable std::mt19937 rng_{std::random_device{}()};
  mutable std::uniform_real_distribution<double> real_dist_{0.0, 1.0};
  mutable std::uniform_int_distribution<size_t> int_dist_;

  [[nodiscard]] auto get_best_arm() const -> size_t {
    return std::distance(estimates_.begin(), std::ranges::max_element(estimates_));
  }
};
