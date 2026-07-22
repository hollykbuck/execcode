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
#include <thread>

namespace ex = stdexec;

TEST_CASE("sync_wait provides run_loop::scheduler via get_scheduler", "[run_loop]") {
  auto [sched] = ex::sync_wait(ex::get_scheduler()).value();
  (void)sched;
  SUCCEED("sync_wait provides run_loop::scheduler via get_scheduler");
}

TEST_CASE("run_loop scheduler can be used in let_value", "[run_loop]") {
  auto y = ex::let_value(
    ex::get_scheduler(),
    [](auto sched) {
      return ex::starts_on(sched,
        ex::then(ex::just(), [] { return 42; })
      );
    });

  auto [v] = ex::sync_wait(std::move(y)).value();
  REQUIRE(v == 42);
}

TEST_CASE("run_loop scheduler is equality_comparable", "[run_loop]") {
  ex::run_loop loop;
  auto s1 = loop.get_scheduler();
  auto s2 = loop.get_scheduler();

  REQUIRE(s1 == s2);
}
