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
