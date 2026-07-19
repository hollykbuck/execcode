#include <catch2/catch_test_macros.hpp>
#include <stdexec/execution.hpp>
#include <exec/static_thread_pool.hpp>
#include <exec/inline_scheduler.hpp>
#include <thread>
#include <atomic>

namespace ex = stdexec;

TEST_CASE("starts_on launches work on a specific scheduler", "[starts_on]") {
  exec::static_thread_pool pool{2};
  auto sch = pool.get_scheduler();

  std::atomic<std::thread::id> work_thread{};
  std::thread::id main_thread = std::this_thread::get_id();

  ex::sync_wait(
    ex::starts_on(sch,
      ex::just() | ex::then([&] { work_thread.store(std::this_thread::get_id()); })
    )
  );

  // The work should execute on a pool thread, not the main thread
  REQUIRE(work_thread.load() != main_thread);
}

TEST_CASE("starts_on with inline scheduler runs synchronously", "[starts_on]") {
  exec::inline_scheduler sch{};

  std::thread::id caller = std::this_thread::get_id();
  std::thread::id worker;

  ex::sync_wait(
    ex::starts_on(sch,
      ex::just() | ex::then([&] { worker = std::this_thread::get_id(); })
    )
  );

  REQUIRE(worker == caller);
}

TEST_CASE("continues_on switches execution context", "[continues_on]") {
  exec::static_thread_pool pool_a{1};
  exec::static_thread_pool pool_b{1};
  auto sch_a = pool_a.get_scheduler();
  auto sch_b = pool_b.get_scheduler();

  std::atomic<std::thread::id> stage1{};
  std::atomic<std::thread::id> stage2{};

  ex::sync_wait(
    ex::starts_on(sch_a,
      ex::just()
        | ex::then([&] { stage1.store(std::this_thread::get_id()); })
        | ex::continues_on(sch_b)
        | ex::then([&] { stage2.store(std::this_thread::get_id()); })
    )
  );

  REQUIRE(stage1.load() != stage2.load());
}

TEST_CASE("continues_on with same scheduler", "[continues_on]") {
  exec::static_thread_pool pool{2};
  auto sch = pool.get_scheduler();

  std::atomic<int> count{0};

  ex::sync_wait(
    ex::starts_on(sch,
      ex::just()
        | ex::then([&] { count.fetch_add(1); })
        | ex::continues_on(sch)
        | ex::then([&] { count.fetch_add(1); })
    )
  );

  REQUIRE(count.load() == 2);
}

TEST_CASE("on starts and continues on the same scheduler", "[on]") {
  exec::static_thread_pool pool{2};
  auto sch = pool.get_scheduler();

  std::atomic<int> counter{0};

  ex::sync_wait(
    ex::on(sch,
      ex::just()
        | ex::then([&] { counter.fetch_add(1); })
        | ex::then([&] { counter.fetch_add(1); })
    )
  );

  REQUIRE(counter.load() == 2);
}
