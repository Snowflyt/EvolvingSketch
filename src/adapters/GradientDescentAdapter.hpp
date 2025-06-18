#pragma once

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "adapter.hpp"

inline constexpr double LEARNING_RATE = 0.01;
inline constexpr double MAX_GRAD = 10;
inline constexpr double RHO = 0.5; // Decay rate for moving average
inline constexpr double EPSILON = 1e-8;
inline constexpr double MIN_ALPHA = 0;

class GradientDescentAdapter : public Adapter<double, double> {
public:
  explicit GradientDescentAdapter(const double lr = LEARNING_RATE, const double max_grad = MAX_GRAD,
                                  const double rho = RHO, const double epsilon = EPSILON,
                                  const double min_param = MIN_ALPHA)
      : k_lr_(lr), k_max_grad_(max_grad), k_rho_(rho), k_epsilon_(epsilon),
        k_min_param_(min_param) {}

protected:
  auto disturb_param(const double &param) -> double override {
    const int sign = (std::rand() % 2) * 2 - 1;
    return param * (1.0 + sign * 1e-6);
  }

  auto adapt(const double &obj, const double &last_obj, const double &param,
             const double &last_param) -> double override {
    constexpr double EPS = 1e-6;

    // Compute gradient
    double grad = (obj - last_obj) / ((param - last_param) + EPS);
    grad = clip_gradient(grad);

    // RMSprop: moving average of squared gradients
    v_ = k_rho_ * v_ + (1.0 - k_rho_) * grad * grad;

    // Gradient descent with adaptive learning rate
    const double adaptive_lr = k_lr_ / (std::sqrt(v_) + k_epsilon_);
    const double new_param = param - adaptive_lr * grad;

    return std::max(new_param, k_min_param_);
  }

private:
  double k_lr_, k_max_grad_, k_rho_, k_epsilon_, k_min_param_;
  double v_ = 0.0; // Moving average of squared gradients

  [[nodiscard]] auto clip_gradient(const double &grad) const -> double {
    return std::clamp(grad, -k_max_grad_, k_max_grad_);
  }
};
