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
