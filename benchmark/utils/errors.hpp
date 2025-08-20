#pragma once

#include <stdexcept>
#include <string>
#include <utility>

class usage_error : public std::runtime_error {
public:
  explicit usage_error(std::string usage, std::string msg = "")
      : std::runtime_error(msg), usage_(std::move(usage)), msg_(std::move(msg)) {}

  [[nodiscard]] auto usage() const -> std::string { return usage_; }
  [[nodiscard]] auto msg() const -> std::string { return msg_; }

private:
  std::string usage_;
  std::string msg_;
};
