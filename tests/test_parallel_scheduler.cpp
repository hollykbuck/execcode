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

namespace ex = stdexec;

TEST_CASE("get_parallel_scheduler returns a valid scheduler", "[parallel]") {
  auto sched = ex::get_parallel_scheduler();
  static_assert(ex::scheduler<decltype(sched)>);
}

TEST_CASE("parallel_scheduler is not default constructible", "[parallel]") {
  using sched_t = decltype(ex::get_parallel_scheduler());
  STATIC_REQUIRE(!std::is_default_constructible_v<sched_t>);
  STATIC_REQUIRE(std::is_destructible_v<sched_t>);
}

TEST_CASE("parallel_scheduler is copyable and movable", "[parallel]") {
  using sched_t = decltype(ex::get_parallel_scheduler());
  STATIC_REQUIRE(std::is_copy_constructible_v<sched_t>);
  STATIC_REQUIRE(std::is_move_constructible_v<sched_t>);
}

TEST_CASE("parallel_scheduler equality", "[parallel]") {
  auto sched1 = ex::get_parallel_scheduler();
  auto sched2 = ex::get_parallel_scheduler();
  REQUIRE(sched1 == sched2);
}

TEST_CASE("schedule on parallel_scheduler runs on a different thread", "[parallel]") {
  auto sched = ex::get_parallel_scheduler();

  std::thread::id caller = std::this_thread::get_id();
  std::thread::id worker{};

  ex::sync_wait(
    ex::schedule(sched) | ex::then([&] { worker = std::this_thread::get_id(); })
  );

  REQUIRE(worker != std::thread::id{});
  REQUIRE(worker != caller);
}

TEST_CASE("parallel_scheduler forward_progress_guarantee", "[parallel]") {
  auto sched = ex::get_parallel_scheduler();
  REQUIRE(ex::get_forward_progress_guarantee(sched) == ex::forward_progress_guarantee::parallel);
}

TEST_CASE("schedule with value on parallel_scheduler", "[parallel]") {
  auto sched = ex::get_parallel_scheduler();

  auto [v] = ex::sync_wait(
    ex::schedule(sched) | ex::then([] { return 42; })
  ).value();
  REQUIRE(v == 42);
}

TEST_CASE("bulk on parallel_scheduler", "[parallel]") {
  auto sched = ex::get_parallel_scheduler();
  constexpr size_t n = 16;
  std::thread::id pool_ids[n];
  std::thread::id caller = std::this_thread::get_id();

  ex::sync_wait(
    ex::schedule(sched) | ex::bulk(ex::par, n, [&](size_t i) {
      pool_ids[i] = std::this_thread::get_id();
    })
  );

  for (auto id : pool_ids) {
    REQUIRE(id != std::thread::id{});
    REQUIRE(id != caller);
  }
}
