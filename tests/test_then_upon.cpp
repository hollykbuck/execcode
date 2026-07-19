#include <catch2/catch_test_macros.hpp>
#include <stdexec/execution.hpp>

namespace ex = stdexec;

TEST_CASE("then transforms set_value", "[then]") {
  auto [v] = ex::sync_wait(
    ex::just(40) | ex::then([](int x) { return x + 2; })
  ).value();
  REQUIRE(v == 42);
}

TEST_CASE("then with void function", "[then]") {
  int side_effect = 0;
  auto opt = ex::sync_wait(
    ex::just() | ex::then([&] { side_effect = 99; })
  );
  REQUIRE(opt.has_value());
  REQUIRE(side_effect == 99);
}

TEST_CASE("then on when_all result", "[then][when_all]") {
  auto [v] = ex::sync_wait(
    ex::when_all(ex::just(3), ex::just(4))
      | ex::then([](int a, int b) { return a + b; })
  ).value();
  REQUIRE(v == 7);
}

TEST_CASE("upon_error catches set_error and converts to value", "[upon_error]") {
  auto [v] = ex::sync_wait(
    ex::just_error(std::make_exception_ptr(std::runtime_error{"fail"}))
      | ex::upon_error([](std::exception_ptr) { return -1; })
  ).value();
  REQUIRE(v == -1);
}

TEST_CASE("upon_error preserves set_value", "[upon_error]") {
  auto [v] = ex::sync_wait(
    ex::just(42)
      | ex::upon_error([](std::exception_ptr) { return -1; })
  ).value();
  REQUIRE(v == 42);
}

TEST_CASE("upon_stopped converts cancellation to value", "[upon_stopped]") {
  auto [v] = ex::sync_wait(
    ex::just_stopped()
      | ex::upon_stopped([] { return 0; })
  ).value();
  REQUIRE(v == 0);
}

TEST_CASE("upon_stopped preserves set_value", "[upon_stopped]") {
  auto [v] = ex::sync_wait(
    ex::just(42)
      | ex::upon_stopped([] { return 0; })
  ).value();
  REQUIRE(v == 42);
}

TEST_CASE("chained then, upon_error, upon_stopped normalizes signals", "[then]") {
  auto pipeline = [](auto sndr) {
    return std::move(sndr)
      | ex::then([](int x) { return x + 1; })
      | ex::upon_error([](std::exception_ptr) { return -1; })
      | ex::upon_stopped([] { return -2; });
  };

  auto [v1] = ex::sync_wait(pipeline(ex::just(41))).value();
  REQUIRE(v1 == 42);
}

TEST_CASE("multiple then operations compose linearly", "[then]") {
  auto [v] = ex::sync_wait(
    ex::just(1)
      | ex::then([](int x) { return x + 10; })
      | ex::then([](int x) { return x * 2; })
      | ex::then([](int x) { return x - 5; })
  ).value();
  REQUIRE(v == 17);
}
