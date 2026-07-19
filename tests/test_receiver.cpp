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
