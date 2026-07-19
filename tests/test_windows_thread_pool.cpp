#include <catch2/catch_test_macros.hpp>
#include <stdexec/execution.hpp>
#include <stdexec/stop_token.hpp>
#include <exec/windows/windows_thread_pool.hpp>
#include <atomic>

namespace ex = stdexec;

TEST_CASE("windows_thread_pool default constructs", "[win]") {
  exec::windows_thread_pool pool{};
  auto sch = pool.get_scheduler();
  REQUIRE_NOTHROW(ex::sync_wait(ex::schedule(sch)));
}

TEST_CASE("windows_thread_pool executes work", "[win]") {
  exec::windows_thread_pool pool{};
  auto sch = pool.get_scheduler();

  std::atomic<int> counter{0};
  constexpr int N = 10;

  for (int i = 0; i < N; ++i) {
    ex::sync_wait(
      ex::schedule(sch) | ex::then([&] { counter.fetch_add(1); })
    );
  }

  REQUIRE(counter.load() == N);
}

TEST_CASE("windows_thread_pool returns values", "[win]") {
  exec::windows_thread_pool pool{};
  auto sch = pool.get_scheduler();

  auto [v] = ex::sync_wait(
    ex::schedule(sch) | ex::then([] { return 42; })
  ).value();
  REQUIRE(v == 42);
}

TEST_CASE("windows_thread_pool with min/max threads", "[win]") {
  exec::windows_thread_pool pool{2, 8};
  auto sch = pool.get_scheduler();

  std::atomic<int> counter{0};
  ex::sync_wait(
    ex::schedule(sch) | ex::then([&] { counter.fetch_add(1); })
  );
  REQUIRE(counter.load() == 1);
}

TEST_CASE("windows_thread_pool composes with when_all", "[win]") {
  exec::windows_thread_pool pool{};
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

TEST_CASE("windows_thread_pool runs on pool threads", "[win]") {
  exec::windows_thread_pool pool{};
  auto sch = pool.get_scheduler();

  std::thread::id main_tid = std::this_thread::get_id();
  std::thread::id work_tid;

  ex::sync_wait(
    ex::schedule(sch) | ex::then([&] { work_tid = std::this_thread::get_id(); })
  );

  // Work should run on a different thread (pool thread)
  REQUIRE(work_tid != main_tid);
}

TEST_CASE("windows_thread_pool supports bulk", "[win]") {
  exec::windows_thread_pool pool{};
  auto sch = pool.get_scheduler();

  std::atomic<int> count{0};
  constexpr int N = 20;

  ex::sync_wait(
    ex::starts_on(sch,
      ex::just() | ex::bulk(N, [&](int) { count.fetch_add(1); })
    )
  );

  REQUIRE(count.load() == N);
}
