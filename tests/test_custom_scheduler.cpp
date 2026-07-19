#include <catch2/catch_test_macros.hpp>
#include <stdexec/execution.hpp>
#include <exec/static_thread_pool.hpp>
#include <exec/inline_scheduler.hpp>

namespace ex = stdexec;

TEST_CASE("static_thread_pool scheduler is a valid scheduler", "[custom_scheduler]") {
  exec::static_thread_pool pool{2};
  auto sch = pool.get_scheduler();

  auto [v] = ex::sync_wait(
    ex::schedule(sch) | ex::then([] { return 42; })
  ).value();
  REQUIRE(v == 42);
}

TEST_CASE("inline_scheduler runs on current thread", "[custom_scheduler]") {
  exec::inline_scheduler sch{};

  std::thread::id caller = std::this_thread::get_id();
  std::thread::id worker;

  ex::sync_wait(
    ex::schedule(sch) | ex::then([&] { worker = std::this_thread::get_id(); })
  );

  REQUIRE(worker == caller);
}

TEST_CASE("scheduler forward_progress_guarantee is queryable", "[custom_scheduler]") {
  exec::static_thread_pool pool{2};
  auto sch = pool.get_scheduler();

  auto g = ex::get_forward_progress_guarantee(sch);
  REQUIRE(g == ex::forward_progress_guarantee::parallel);
}

TEST_CASE("multiple schedulers compose with when_all", "[custom_scheduler]") {
  exec::static_thread_pool pool{4};
  auto sch = pool.get_scheduler();

  auto [a, b] = ex::sync_wait(
    ex::when_all(
      ex::schedule(sch) | ex::then([] { return 1; }),
      ex::schedule(sch) | ex::then([] { return 2; })
    )
  ).value();

  REQUIRE(a == 1);
  REQUIRE(b == 2);
}
