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
#include <stdexec/execution.hpp>
#include <exec/static_thread_pool.hpp>
#include <exec/async_scope.hpp>
#include <atomic>

namespace ex = stdexec;

TEST_CASE("spawn via async_scope", "[spawn]") {
  exec::static_thread_pool ctx{2};
  auto sch = ctx.get_scheduler();
  exec::async_scope scope;

  std::atomic<int> counter{0};

  scope.spawn(
    ex::schedule(sch) | ex::then([&] { counter.fetch_add(1); })
  );

  ex::sync_wait(scope.on_empty());
  REQUIRE(counter.load() == 1);
}

TEST_CASE("spawn_future and collect results", "[spawn]") {
  exec::static_thread_pool ctx{2};
  auto sch = ctx.get_scheduler();
  exec::async_scope scope;

  auto f = scope.spawn_future(
    ex::schedule(sch) | ex::then([] { return 42; })
  );

  auto [v] = ex::sync_wait(std::move(f)).value();
  REQUIRE(v == 42);
  ex::sync_wait(scope.on_empty());
}

TEST_CASE("scope ensures structured concurrency", "[spawn]") {
  exec::static_thread_pool ctx{1};
  auto sch = ctx.get_scheduler();
  exec::async_scope scope;

  std::atomic<int> counter{0};

  for (int i = 0; i < 5; ++i) {
    scope.spawn(
      ex::schedule(sch) | ex::then([&] { counter.fetch_add(1); })
    );
  }

  ex::sync_wait(scope.on_empty());
  REQUIRE(counter.load() == 5);
}

TEST_CASE("spawn discards set_stopped silently", "[spawn]") {
  exec::async_scope scope;

  scope.spawn(ex::just_stopped());
  ex::sync_wait(scope.on_empty());
  SUCCEED("spawn did not crash on set_stopped (silently discarded)");
}

TEST_CASE("spawn_future propagates errors (does not discard them)", "[spawn]") {
  exec::static_thread_pool ctx{2};
  auto sch = ctx.get_scheduler();
  exec::async_scope scope;

  auto f = scope.spawn_future(
    ex::schedule(sch) | ex::then([]() -> int { throw std::runtime_error{"boom"}; })
  );

  // The error from the throwing lambda must be propagated through spawn_future
  bool error_caught = false;
  auto [v] = ex::sync_wait(
    std::move(f) | ex::upon_error([&](std::exception_ptr) { error_caught = true; return -1; })
  ).value();
  REQUIRE(error_caught);
  REQUIRE(v == -1);

  ex::sync_wait(scope.on_empty());
}
