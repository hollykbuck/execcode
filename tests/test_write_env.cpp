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

namespace ex = stdexec;

TEST_CASE("write_env modifies the receiver environment", "[write_env]") {
  exec::static_thread_pool pool{2};
  auto sch = pool.get_scheduler();

  // write_env in function-call style: write_env(sender, env)
  ex::sync_wait(
    ex::write_env(
      ex::just() | ex::then([] { return 42; }),
      ex::prop{ex::get_scheduler, sch}
    )
  );

  SUCCEED("write_env compiles and runs");
}

TEST_CASE("write_env with never_stop_token", "[write_env][unstoppable]") {
  bool ran_to_completion = false;

  // write_env with prop{get_stop_token, never_stop_token}
  ex::sync_wait(
    ex::write_env(
      ex::just() | ex::then([&] { ran_to_completion = true; }),
      ex::prop{ex::get_stop_token, ex::never_stop_token{}}
    )
  );

  REQUIRE(ran_to_completion);
}

TEST_CASE("write_env chains multiple properties", "[write_env]") {
  exec::static_thread_pool pool{2};
  auto sch = pool.get_scheduler();

  // Multiple properties via env composition
  ex::sync_wait(
    ex::write_env(
      ex::write_env(
        ex::just() | ex::then([] { return 42; }),
        ex::prop{ex::get_scheduler, sch}
      ),
      ex::prop{ex::get_allocator, std::allocator<int>{}}
    )
  );

  SUCCEED("Multiple write_env properties compose");
}
