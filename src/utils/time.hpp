#pragma once

#include <chrono>

inline auto get_current_time_in_seconds() -> double {
  const auto now = std::chrono::high_resolution_clock::now();
  const auto duration = now.time_since_epoch();
  return std::chrono::duration_cast<std::chrono::duration<double>>(duration).count();
}
