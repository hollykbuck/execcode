#include <catch2/catch_test_macros.hpp>
#include <stdexec/execution.hpp>

#include <optional>

namespace ex = stdexec;

// =============================================================================
// 07-coroutines.md — Basic coroutine usage
// =============================================================================

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
  auto f = []() -> ex::task<std::optional<int>> {
    co_return co_await ex::stopped_as_optional(ex::just(42));
  };

  auto [opt] = ex::sync_wait(f()).value();
  REQUIRE(opt.has_value());
  REQUIRE(*opt == 42);
}

// =============================================================================
// get_stop_token() — querying the stop token inside a coroutine
// 07-coroutines.md line 82
// =============================================================================

TEST_CASE("get_stop_token inside coroutine", "[coroutine][stop_token]") {
  auto f = []() -> ex::task<std::optional<ex::inplace_stop_token>> {
    co_return co_await ex::stopped_as_optional(ex::get_stop_token());
  };

  auto [opt] = ex::sync_wait(f()).value();
  REQUIRE(opt.has_value());
}

// =============================================================================
// Mixed pipeline — coroutine embedded in a sender pipeline via let_value
// 07-coroutines.md line 103-114
// =============================================================================

auto pipeline(int x) -> ex::task<int> {
  int y = co_await ex::just(x + 1);
  co_return y;
}

TEST_CASE("coroutine embedded in pipeline via let_value", "[coroutine][pipeline]") {
  auto [v] = ex::sync_wait(
    ex::just(42)
    | ex::then([](int x) { return x + 1; })
    | ex::let_value([](int x) { return pipeline(x); })
  ).value();
  REQUIRE(v == 44);
}

TEST_CASE("multiple coroutines in when_all", "[coroutine][when_all]") {
  auto [a, b] = ex::sync_wait(
    ex::when_all(async_add(ex::just(10), ex::just(20)),
                 async_add(ex::just(30), ex::just(40)))
  ).value();
  REQUIRE(a == 30);
  REQUIRE(b == 70);
}

// =============================================================================
// task<T, Environment> — customizing the task's allocator and scheduler type
// 07c-task-internals.md line 53-63
// =============================================================================

struct custom_task_env {
  using allocator_type = std::allocator<std::byte>;
};

auto custom_task() -> ex::task<int, custom_task_env> {
  co_return 42;
}

TEST_CASE("task with custom Environment", "[coroutine][task_env]") {
  auto [v] = ex::sync_wait(custom_task()).value();
  REQUIRE(v == 42);
}

// =============================================================================
// affine adaptor syntax
// 07d-affine.md line 23-26
// =============================================================================

TEST_CASE("affine adaptor syntax", "[coroutine][affine]") {
  auto [v] = ex::sync_wait(
    ex::just(42) | ex::affine() | ex::then([](int x) { return x; })
  ).value();
  REQUIRE(v == 42);
}
