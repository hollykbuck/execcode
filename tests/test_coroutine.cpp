#include <catch2/catch_test_macros.hpp>
#include <stdexec/execution.hpp>
#include <optional>

namespace ex = stdexec;

// A coroutine that awaits two senders and adds their int results
template <ex::sender S1, ex::sender S2>
auto async_add(S1 s1, S2 s2) -> ex::task<int> {
  int a = co_await static_cast<S1&&>(s1);
  int b = co_await static_cast<S2&&>(s2);
  co_return a + b;
}

TEST_CASE("task coroutine can await senders", "[coroutine]") {
  auto [v] = ex::sync_wait(async_add(ex::just(40), ex::just(2))).value();
  REQUIRE(v == 42);
}

TEST_CASE("task is itself a sender usable with sync_wait", "[coroutine]") {
  auto [v] = ex::sync_wait(async_add(ex::just(20), ex::just(22))).value();
  REQUIRE(v == 42);
}

TEST_CASE("stopped_as_optional in coroutine", "[coroutine][stopped]") {
  // Use a sender that might be cancelled
  auto f = []() -> ex::task<std::optional<int>> {
    co_return co_await ex::stopped_as_optional(ex::just(42));
  };

  auto [opt] = ex::sync_wait(f()).value();
  REQUIRE(opt.has_value());
  REQUIRE(*opt == 42);
}
