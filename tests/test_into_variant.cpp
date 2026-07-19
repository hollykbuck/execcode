#include <catch2/catch_test_macros.hpp>
#include <stdexec/execution.hpp>
#include <optional>

namespace ex = stdexec;

// These tests use stopped_as_optional with senders that have
// exactly one value completion (the constraint for this stdexec version).

TEST_CASE("stopped_as_optional preserves set_value", "[into_variant][stopped]") {
  // just(42) has exactly one value completion: set_value_t(int)
  auto sndr = ex::stopped_as_optional(ex::just(42));
  auto opt = ex::sync_wait(std::move(sndr));

  REQUIRE(opt.has_value());
  auto [inner] = opt.value();
  REQUIRE(inner.has_value());
  REQUIRE(*inner == 42);
}

TEST_CASE("upon_stopped converts stop to value", "[into_variant][stopped]") {
  bool stopped_handled = false;

  auto [v] = ex::sync_wait(
    ex::just_stopped()
      | ex::upon_stopped([&] { stopped_handled = true; return 0; })
  ).value();

  REQUIRE(stopped_handled);
  REQUIRE(v == 0);
}

TEST_CASE("upon_error recovers from error", "[into_variant][stopped]") {
  bool error_handled = false;

  auto [v] = ex::sync_wait(
    ex::just_error(std::make_exception_ptr(std::runtime_error{"err"}))
      | ex::upon_error([&](std::exception_ptr) { error_handled = true; return -1; })
  ).value();

  REQUIRE(error_handled);
  REQUIRE(v == -1);
}

TEST_CASE("into_variant wraps value in variant of tuple", "[into_variant]") {
  auto sndr = ex::into_variant(ex::just(42));
  auto [v] = ex::sync_wait(std::move(sndr)).value();

  auto [val] = std::get<0>(v);
  REQUIRE(val == 42);
}

TEST_CASE("into_variant with empty value", "[into_variant]") {
  auto sndr = ex::into_variant(ex::just());
  auto [v] = ex::sync_wait(std::move(sndr)).value();

  SUCCEED("into_variant works with empty set_value");
}
