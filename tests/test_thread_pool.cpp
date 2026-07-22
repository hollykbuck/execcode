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
#include <exec/inline_scheduler.hpp>
#include <atomic>

namespace ex = stdexec;

TEST_CASE("static_thread_pool executes work on pool threads", "[thread_pool]") {
  exec::static_thread_pool pool{4};
  auto sch = pool.get_scheduler();

  std::atomic<int> counter{0};
  constexpr int N = 20;

  for (int i = 0; i < N; ++i) {
    ex::sync_wait(
      ex::schedule(sch) | ex::then([&] { counter.fetch_add(1); })
    );
  }

  REQUIRE(counter.load() == N);
}

TEST_CASE("inline_scheduler executes synchronously", "[thread_pool][inline]") {
  stdexec::inline_scheduler sch{};

  std::thread::id caller_id = std::this_thread::get_id();
  std::thread::id work_id;

  ex::sync_wait(
    ex::schedule(sch) | ex::then([&] { work_id = std::this_thread::get_id(); })
  );

  REQUIRE(work_id == caller_id);
}

TEST_CASE("multiple schedulers can coexist", "[thread_pool]") {
  exec::static_thread_pool pool_a{2};
  exec::static_thread_pool pool_b{2};
  auto sch_a = pool_a.get_scheduler();
  auto sch_b = pool_b.get_scheduler();

  auto [va, vb] = ex::sync_wait(ex::when_all(
    ex::schedule(sch_a) | ex::then([] { return 1; }),
    ex::schedule(sch_b) | ex::then([] { return 2; })
  )).value();

  REQUIRE(va == 1);
  REQUIRE(vb == 2);
}

TEST_CASE("get_forward_progress_guarantee on static_thread_pool", "[thread_pool]") {
  exec::static_thread_pool pool{2};
  auto sch = pool.get_scheduler();

  auto guarantee = ex::get_forward_progress_guarantee(sch);
  REQUIRE(guarantee == ex::forward_progress_guarantee::parallel);
}
