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
#include <catch2/catch_approx.hpp>
#include <stdexec/execution.hpp>
#include <exec/static_thread_pool.hpp>

namespace ex = stdexec;

TEST_CASE("just creates an immediate-value sender", "[basics][just]") {
  ex::sender auto snd = ex::just(42);
  auto [v] = ex::sync_wait(std::move(snd)).value();
  REQUIRE(v == 42);
}

TEST_CASE("just with multiple values", "[basics][just]") {
  ex::sender auto snd = ex::just(1, 2.0, 3);
  auto [a, b, c] = ex::sync_wait(std::move(snd)).value();
  REQUIRE(a == 1);
  REQUIRE(b == Catch::Approx(2.0));
  REQUIRE(c == 3);
}

TEST_CASE("just_error sends an error", "[basics][just_error]") {
  ex::sender auto snd = 
    ex::just_error(std::make_exception_ptr(std::runtime_error{"test"}))
      | ex::upon_error([](std::exception_ptr) { return -1; });
  auto opt = ex::sync_wait(std::move(snd));
  REQUIRE(opt.has_value());
  auto [v] = opt.value();
  REQUIRE(v == -1);
}

TEST_CASE("just_stopped sends a stopped signal", "[basics][just_stopped]") {
  auto opt = ex::sync_wait(
    ex::just_stopped()
      | ex::upon_stopped([] { return 0; })
  );
  REQUIRE(opt.has_value());
  auto [v] = opt.value();
  REQUIRE(v == 0);
}

TEST_CASE("schedule + then + sync_wait basic pipeline", "[basics][pipeline]") {
  exec::static_thread_pool pool{4};
  auto sch = pool.get_scheduler();

  auto [i] = ex::sync_wait(
    ex::schedule(sch)
      | ex::then([] { return 13; })
      | ex::then([](int x) { return x + 42; })
  ).value();

  REQUIRE(i == 55);
}

TEST_CASE("empty just sends void", "[basics]") {
  auto opt = ex::sync_wait(ex::just());
  REQUIRE(opt.has_value());
}

TEST_CASE("get_env returns a queryable environment for sender", "[basics][env]") {
  auto sndr = ex::just(1);
  auto env = ex::get_env(sndr);
  (void)env;
  SUCCEED("get_env is callable on just()");
}

TEST_CASE("when_all of multiple just values", "[basics][when_all]") {
  auto [x, y, z] = ex::sync_wait(
    ex::when_all(ex::just(1), ex::just(2), ex::just(3))
  ).value();
  REQUIRE(x == 1);
  REQUIRE(y == 2);
  REQUIRE(z == 3);
}
