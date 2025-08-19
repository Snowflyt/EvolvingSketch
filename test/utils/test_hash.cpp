#include <string>

#include <doctest/doctest.h>

#include "../../src/utils/hash.hpp"

TEST_CASE("[hash] 32-bit hash") {
  CHECK(hash32("") == hash32(""));

  std::string str = "test string";
  CHECK(hash32(str) == hash32(str));
}
