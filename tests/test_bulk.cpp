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
#include <atomic>
#include <vector>

namespace ex = stdexec;

TEST_CASE("bulk applies operation over index range", "[bulk]") {
  // Verify that bulk calls the function for each index
  std::atomic<int> count{0};
  constexpr int N = 10;

  ex::sync_wait(
    ex::just()
      | ex::bulk(N, [&](int idx) {
          count.fetch_add(1);
          REQUIRE(idx >= 0);
          REQUIRE(idx < N);
        })
  );

  REQUIRE(count.load() == N);
}

TEST_CASE("bulk on thread pool", "[bulk][thread_pool]") {
  exec::static_thread_pool pool{4};
  auto sch = pool.get_scheduler();

  std::atomic<int> sum{0};
  constexpr int N = 100;

  ex::sync_wait(
    ex::starts_on(sch,
      ex::just()
        | ex::bulk(N, [&](int idx) {
            sum.fetch_add(idx, std::memory_order_relaxed);
          })
    )
  );

  REQUIRE(sum.load() == N * (N - 1) / 2);
}

TEST_CASE("bulk with state from upstream", "[bulk]") {
  std::vector<int> data = {1, 2, 3, 4, 5};
  std::atomic<int64_t> total{0};

  ex::sync_wait(
    ex::just(std::ref(data))
      | ex::bulk(static_cast<int>(data.size()), [&](int idx, auto& vec) {
          total.fetch_add(vec.get()[idx]);
        })
  );

  REQUIRE(total.load() == 15);
}

TEST_CASE("bulk preserves upstream value", "[bulk]") {
  // bulk transparently passes through the upstream value
  auto [v] = ex::sync_wait(
    ex::just(42)
      | ex::bulk(5, [](int, int&) {})
  ).value();

  REQUIRE(v == 42);
}
