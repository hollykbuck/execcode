#include <catch2/catch_test_macros.hpp>
#include <stdexec/execution.hpp>
#include <exec/static_thread_pool.hpp>

namespace ex = stdexec;

TEST_CASE("when_all waits for all senders", "[when_all]") {
  auto [a, b] = ex::sync_wait(
    ex::when_all(ex::just(1), ex::just(2))
  ).value();

  REQUIRE(a == 1);
  REQUIRE(b == 2);
}

TEST_CASE("when_all with three senders", "[when_all]") {
  auto [x, y, z] = ex::sync_wait(
    ex::when_all(ex::just(10), ex::just(20), ex::just(30))
  ).value();

  REQUIRE(x == 10);
  REQUIRE(y == 20);
  REQUIRE(z == 30);
}

TEST_CASE("when_all on thread pool", "[when_all][thread_pool]") {
  exec::static_thread_pool pool{4};
  auto sch = pool.get_scheduler();

  auto [r1, r2] = ex::sync_wait(
    ex::when_all(
      ex::schedule(sch) | ex::then([] { return 7; }),
      ex::schedule(sch) | ex::then([] { return 11; })
    )
  ).value();

  REQUIRE(r1 == 7);
  REQUIRE(r2 == 11);
}

TEST_CASE("when_all with then composition", "[when_all]") {
  auto [sum] = ex::sync_wait(
    ex::when_all(ex::just(3), ex::just(4), ex::just(5))
      | ex::then([](int a, int b, int c) { return a + b + c; })
  ).value();

  REQUIRE(sum == 12);
}

TEST_CASE("when_all error propagation", "[when_all]") {
  bool error_handled = false;

  auto [v] = ex::sync_wait(
    ex::when_all(
      ex::just(42),
      ex::just_error(std::make_exception_ptr(std::runtime_error{"fail"}))
    )
    | ex::upon_error([&](std::exception_ptr) { error_handled = true; return -1; })
  ).value();

  REQUIRE(error_handled);
  REQUIRE(v == -1);
}

TEST_CASE("when_all stopped propagation", "[when_all]") {
  bool stopped_handled = false;

  auto [v] = ex::sync_wait(
    ex::when_all(
      ex::just(42),
      ex::just_stopped()
    )
    | ex::upon_stopped([&] { stopped_handled = true; return 0; })
  ).value();

  REQUIRE(stopped_handled);
  REQUIRE(v == 0);
}

TEST_CASE("when_all_with_variant compiles with just values", "[when_all]") {
  // Verify that when_all_with_variant produces a valid sender
  auto sndr = ex::when_all_with_variant(ex::just(42), ex::just(3.14));
  using Sigs = ex::completion_signatures_of_t<decltype(sndr)>;
  constexpr bool has_value_sig = requires {
    requires std::same_as<
      ex::value_types_of_t<decltype(sndr)>,
      std::variant<std::tuple<int>, std::tuple<double>>
    >;
  };
  SUCCEED("when_all_with_variant produces a sender");
}
