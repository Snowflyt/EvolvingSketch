#include <catch2/catch_test_macros.hpp>

#include "../../src/utils/hash.hpp"

TEST_CASE("32-bit hash", "[hash]") {
  REQUIRE(hash32("") == hash32(""));

  std::string str = "test string";
  REQUIRE(hash32(str) == hash32(str));
}
