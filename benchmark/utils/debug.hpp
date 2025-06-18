#pragma once

#include <format>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include <magic_enum/magic_enum.hpp>

/**
 * @brief Format a value for display in diagnostic messages.
 *
 * @param value The value to format.
 * @return A string representation of the value.
 */
template <typename T> constexpr auto show(T &&value) -> std::string {
  if constexpr (std::is_enum_v<T>)
    return magic_enum::enum_name(std::forward<T>(value));
  else if constexpr (std::is_same_v<T, std::string> || std::is_same_v<T, std::string_view> ||
                     std::is_same_v<T, const char *>)
    return std::format("\"{}\"", std::forward<T>(value));
  else
    return std::format("{}", std::forward<T>(value));
}
