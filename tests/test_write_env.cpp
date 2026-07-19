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
