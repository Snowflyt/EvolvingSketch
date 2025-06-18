#pragma once

#include <stdexcept>
#include <string>

class usage_error : public std::runtime_error {
public:
  explicit usage_error(const std::string &usage) : std::runtime_error(usage) {}
};
