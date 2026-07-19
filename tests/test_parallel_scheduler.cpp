#include <catch2/catch_test_macros.hpp>
#include <stdexec/execution.hpp>

namespace ex = stdexec;

// NOTE: parallel_scheduler requires a separate compiled library.
// The conan package does not include it, so runtime use is not possible.
// These tests verify compile-time properties only.

TEST_CASE("parallel_scheduler type is defined", "[parallel]") {
  constexpr bool is_class = std::is_class_v<ex::parallel_scheduler>;
  REQUIRE(is_class);
}

TEST_CASE("get_parallel_scheduler is a valid function name", "[parallel]") {
  // Verify the function declaration exists (compile-time only)
  constexpr bool is_fn = std::is_function_v<std::remove_pointer_t<decltype(ex::get_parallel_scheduler)>>;
  REQUIRE(is_fn);
}
