// Copyright (C) 2026 hollykbuck <101749900+hollykbuck@users.noreply.github.com>
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

#include <catch2/catch_test_macros.hpp>
#include <exec/static_thread_pool.hpp>
#include <stdexec/execution.hpp>

#include <optional>
#include <thread>

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

// =============================================================================
// Scheduler switching with coroutines
//
// NOTE: co_await schedule(pool_sch) inside task<T> does NOT switch threads.
//   task<T>::await_transform wraps every co_awaited sender with affine(),
//   which inserts finally(sender, unstoppable(schedule(task_scheduler))).
//   The task_scheduler is backed by sync_wait's run_loop, which runs on the
//   caller thread — so the continuation always resumes on the caller.
//
// To switch execution context, use starts_on / continues_on / on at the
// pipeline level (wrapping the task), not co_await schedule inside.
// =============================================================================

TEST_CASE("continues_on transfers completion to pool thread", "[coroutine][scheduler]") {
  exec::static_thread_pool pool{1};
  auto pool_sch = pool.get_scheduler();

  std::thread::id main_id = std::this_thread::get_id();

  // continues_on runs the sender on the caller, then delivers its completion
  // on the pool. The then() after continues_on runs on the pool thread.
  bool on_pool = false;
  ex::sync_wait(
    ex::continues_on(ex::just(42), pool_sch)
    | ex::then([&](int) { on_pool = std::this_thread::get_id() != main_id; })
  );
  REQUIRE(on_pool);
}

TEST_CASE("continues_on with coroutine completes and transfers back", "[coroutine][scheduler]") {
  exec::static_thread_pool pool{1};
  auto pool_sch = pool.get_scheduler();

  auto coro = []() -> ex::task<int> {
    co_return 42;
  };

  // continues_on(coro(), pool_sch) runs the coroutine on the caller, then
  // delivers the completion on the pool. The completion eventually reaches
  // sync_wait's receiver on the caller thread.
  auto [v] = ex::sync_wait(
    ex::continues_on(coro(), pool_sch)
  ).value();
  REQUIRE(v == 42);
}

TEST_CASE("sender pipeline switches thread via schedule", "[coroutine][scheduler]") {
  exec::static_thread_pool pool{1};
  auto pool_sch = pool.get_scheduler();

  std::thread::id main_id = std::this_thread::get_id();
  std::thread::id work_id;

  // Plain sender pipeline (no task<T>) switches thread.
  ex::sync_wait(
    ex::schedule(pool_sch)
    | ex::then([&] { work_id = std::this_thread::get_id(); })
  );
  REQUIRE(work_id != main_id);
}
