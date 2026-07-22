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

TEST_CASE("inplace_stop_source requests stop", "[stop_token]") {
  ex::inplace_stop_source source;
  auto token = source.get_token();

  REQUIRE_FALSE(token.stop_requested());
  source.request_stop();
  REQUIRE(token.stop_requested());
}

TEST_CASE("inplace_stop_token remains valid after request_stop", "[stop_token]") {
  ex::inplace_stop_source source;
  auto token = source.get_token();

  source.request_stop();
  REQUIRE(token.stop_requested());

  source.request_stop();
  REQUIRE(token.stop_requested());
}

TEST_CASE("stop_requested via read_env", "[stop_token]") {
  bool token_never_stops = false;

  ex::sync_wait(
    ex::read_env(ex::get_stop_token)
      | ex::then([&](auto token) {
          token_never_stops = !token.stop_requested();
        })
  );

  REQUIRE(token_never_stops);
}

TEST_CASE("get_stop_token from sync_wait environment", "[stop_token]") {
  auto [token] = ex::sync_wait(ex::get_stop_token()).value();
  REQUIRE_FALSE(token.stop_requested());
}

TEST_CASE("never_stop_token never stops", "[stop_token]") {
  ex::never_stop_token token;
  REQUIRE_FALSE(token.stop_requested());
}
