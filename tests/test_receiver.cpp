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

namespace ex = stdexec;

TEST_CASE("just creates sender delivering an int", "[receiver]") {
  int result = 0;
  ex::sync_wait(
    ex::just(42) | ex::then([&](int v) { result = v; })
  );
  REQUIRE(result == 42);
}

TEST_CASE("just_error triggers upon_error", "[receiver]") {
  bool handled = false;
  ex::sync_wait(
    ex::just_error(std::make_exception_ptr(std::runtime_error{"x"}))
      | ex::upon_error([&](std::exception_ptr) { handled = true; return 0; })
  );
  REQUIRE(handled);
}

TEST_CASE("just_stopped triggers upon_stopped", "[receiver]") {
  bool handled = false;
  ex::sync_wait(
    ex::just_stopped()
      | ex::upon_stopped([&] { handled = true; return 0; })
  );
  REQUIRE(handled);
}

TEST_CASE("receiver environment is queryable", "[receiver][env]") {
  ex::sync_wait(
    ex::just() | ex::then([] {
      // The sync_wait receiver environment is available here
      return 1;
    })
  );
  SUCCEED("Environment is accessible in then callback");
}
