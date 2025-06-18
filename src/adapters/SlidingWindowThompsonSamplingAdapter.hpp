#pragma once

#include <deque>
#include <random>
#include <vector>

#include "adapter.hpp"

inline constexpr double MIN_PARAM = 0.1;
inline constexpr double MAX_PARAM = 1000.0;
inline constexpr size_t NUM_ARMS = 100;
inline constexpr double REWARD_SCALING = 5.0;
inline constexpr size_t WINDOW_SIZE = 500;

class SlidingWindowThompsonSamplingAdapter : public Adapter<double, double> {
private:
  class ArmHistory {
  public:
    explicit ArmHistory(const size_t window_size) : k_window_size_(window_size) {}

    void add_reward(const double reward) {
      rewards_.push_back(reward);
      if (rewards_.size() > k_window_size_)
        rewards_.pop_front();
    }

    // Beta distribution parameters for Thompson Sampling
    [[nodiscard]] auto get_beta_alpha() const -> double {
      if (rewards_.empty())
        return 1.0;

      // Convert rewards to successes for Beta distribution
      double sum_rewards = 0.0;
      for (const double reward : rewards_)
        sum_rewards += reward;

      return 1.0 + sum_rewards; // Prior alpha = 1
    }

    [[nodiscard]] auto get_beta_beta() const -> double {
      if (rewards_.empty())
        return 1.0;

      // Convert rewards to failures for Beta distribution
      double sum_failures = 0.0;
      for (const double reward : rewards_)
        sum_failures += (1.0 - reward); // Assuming reward in [0,1]

      return 1.0 + sum_failures; // Prior beta = 1
    }

    [[nodiscard]] auto get_mean() const -> double {
      if (rewards_.empty())
        return 0.0;
      const double alpha = get_beta_alpha();
      const double beta = get_beta_beta();
      return alpha / (alpha + beta);
    }

    [[nodiscard]] auto count() const -> size_t { return rewards_.size(); }

    void clear() { rewards_.clear(); }

  private:
    std::deque<double> rewards_;
    size_t k_window_size_;
  };

public:
  explicit SlidingWindowThompsonSamplingAdapter(const double min_param = MIN_PARAM,
                                                const double max_param = MAX_PARAM,
                                                const size_t num_arms = NUM_ARMS,
                                                const double reward_scaling = REWARD_SCALING,
                                                const size_t window_size = WINDOW_SIZE)
      : k_num_arms_(num_arms), k_reward_scaling_(reward_scaling) {

    arms_.resize(k_num_arms_);
    arm_histories_.resize(k_num_arms_, ArmHistory(window_size));

    // // Initialize arms (Linear space distribution)
    // for (size_t i = 0; i < num_arms; ++i)
    //   arms_[i] = min_param + (max_param - min_param) * static_cast<double>(i) /
    //                              static_cast<double>(num_arms - 1);

    // Initialize arms (Logarithmic space distribution)
    const double log_min = std::log(min_param);
    const double log_max = std::log(max_param);
    for (size_t i = 0; i < num_arms; ++i)
      arms_[i] = std::exp(log_min + (log_max - log_min) * static_cast<double>(i) /
                                        static_cast<double>(num_arms - 1));
  }

protected:
  auto disturb_param(const double & /*param*/) -> double override {
    // Initial random selection
    current_arm_ = std::uniform_int_distribution<size_t>(0, k_num_arms_ - 1)(rng_);
    total_pulls_ = 0;
    return arms_[current_arm_];
  }

  auto adapt(const double &obj, const double & /*last_obj*/, const double & /*param*/,
             const double & /*last_param*/) -> double override {

    total_pulls_++;

    double reward = obj;

    // Scale reward to increase sensitivity
    reward = std::pow(reward, 1.0 / k_reward_scaling_);

    // Update current arm's history
    arm_histories_[current_arm_].add_reward(reward);

    // Thompson Sampling: sample from each arm's posterior
    current_arm_ = sample_thompson_arm();

    return arms_[current_arm_];
  }

private:
  size_t k_num_arms_;
  double k_reward_scaling_;

  std::vector<double> arms_;
  std::vector<ArmHistory> arm_histories_;

  size_t current_arm_ = 0;
  size_t total_pulls_ = 0;

  std::mt19937 rng_ = std::mt19937(std::random_device{}());

  [[nodiscard]] auto sample_thompson_arm() -> size_t {
    size_t best_arm = 0;
    double best_sample = -1.0;

    for (size_t i = 0; i < k_num_arms_; ++i) {
      const auto &history = arm_histories_[i];

      // Get Beta distribution parameters
      const double alpha = history.get_beta_alpha();
      const double beta = history.get_beta_beta();

      // Sample from Beta(alpha, beta)
      const double sample = sample_beta(alpha, beta);

      if (sample > best_sample) {
        best_sample = sample;
        best_arm = i;
      }
    }

    return best_arm;
  }

  [[nodiscard]] auto sample_beta(const double alpha, const double beta) -> double {
    // Use Gamma distribution to sample from Beta
    // Beta(α,β) = Gamma(α,1) / (Gamma(α,1) + Gamma(β,1))

    std::gamma_distribution<double> gamma_alpha(alpha, 1.0);
    std::gamma_distribution<double> gamma_beta(beta, 1.0);

    const double x = gamma_alpha(rng_);
    const double y = gamma_beta(rng_);

    if (x + y == 0.0)
      return 0.5; // Fallback for numerical issues

    return x / (x + y);
  }
};
