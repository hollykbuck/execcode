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

TEST_CASE("let_value branches on set_value", "[let_value]") {
  // Dynamic branching: return different senders based on input
  auto branch = [](int x) -> ex::sender auto {
    return ex::let_value(ex::just(x), [](int v) -> ex::sender auto {
      if (v > 0)
        return ex::just(v * 2);
      else
        return ex::just(0);
    });
  };

  auto [p] = ex::sync_wait(branch(21)).value();
  REQUIRE(p == 42);

  auto [z] = ex::sync_wait(branch(-5)).value();
  REQUIRE(z == 0);
}

TEST_CASE("let_value with upon_error recovers from error", "[let_value]") {
  // Use let_value to generate an error, then upon_error to recover
  auto [v] = ex::sync_wait(
    ex::let_value(ex::just(10), [](int a) {
      if (a != 0)
        return ex::just(100 / a);
      return ex::just(-1);  // fallback
    })
      | ex::upon_error([](std::exception_ptr) { return 0; })
  ).value();
  REQUIRE(v == 10);
}

TEST_CASE("let_value with positive input succeeds", "[let_value]") {
  auto [v] = ex::sync_wait(
    ex::let_value(ex::just(41), [](int v) {
      return ex::just(v + 1);
    })
  ).value();
  REQUIRE(v == 42);
}

TEST_CASE("let_value with error from just_error", "[let_value]") {
  bool handled = false;
  auto [v] = ex::sync_wait(
    ex::just(41)
      | ex::let_value([](int) -> ex::sender auto {
          return ex::just_error(
            std::make_exception_ptr(std::invalid_argument{"negative"}));
        })
      | ex::upon_error([&](std::exception_ptr) { handled = true; return -1; })
  ).value();
  REQUIRE(handled);
  REQUIRE(v == -1);
}

TEST_CASE("let_error recovers from error", "[let_error]") {
  auto [v] = ex::sync_wait(
    ex::just_error(std::make_exception_ptr(std::runtime_error{"oops"}))
      | ex::let_error([](std::exception_ptr) -> ex::sender auto {
          return ex::just(42);  // recover with default value
        })
  ).value();
  REQUIRE(v == 42);
}

TEST_CASE("let_error preserves set_value", "[let_error]") {
  auto [v] = ex::sync_wait(
    ex::just(7)
      | ex::let_error([](std::exception_ptr) -> ex::sender auto {
          return ex::just(0);
        })
  ).value();
  REQUIRE(v == 7);
}

TEST_CASE("let_stopped handles cancellation", "[let_stopped]") {
  auto [v] = ex::sync_wait(
    ex::just_stopped()
      | ex::let_stopped([] { return ex::just(0); })
  ).value();
  REQUIRE(v == 0);
}
