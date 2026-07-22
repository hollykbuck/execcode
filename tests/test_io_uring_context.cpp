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
#include <exec/linux/io_uring_context.hpp>
#include <exec/when_any.hpp>
#include <stdexec/execution.hpp>

#include <atomic>
#include <chrono>
#include <thread>

namespace ex = stdexec;
using namespace std::chrono_literals;

TEST_CASE("io_uring_context is not running initially", "[io_uring]") {
  exec::io_uring_context ctx;
  CHECK_FALSE(ctx.is_running());
}

TEST_CASE("io_uring_context schedule on io thread", "[io_uring]") {
  exec::io_uring_context ctx;
  auto sch = ctx.get_scheduler();

  std::thread io_thread{[&] { ctx.run_until_stopped(); }};

  std::thread::id work_id;
  ex::sync_wait(ex::schedule(sch) | ex::then([&] {
    work_id = std::this_thread::get_id();
  }));

  CHECK(work_id == io_thread.get_id());
  ctx.request_stop();
  io_thread.join();
}

TEST_CASE("io_uring_context schedule_after via member", "[io_uring]") {
  exec::io_uring_context ctx;
  auto sch = ctx.get_scheduler();

  std::thread io_thread{[&] { ctx.run_until_stopped(); }};

  std::atomic<bool> called{false};
  ex::sync_wait(
    sch.schedule_after(10ms) | ex::then([&] { called = true; }));

  CHECK(called);
  ctx.request_stop();
  io_thread.join();
}

TEST_CASE("io_uring_context schedule_at via member", "[io_uring]") {
  exec::io_uring_context ctx;
  auto sch = ctx.get_scheduler();

  std::thread io_thread{[&] { ctx.run_until_stopped(); }};

  auto tp = std::chrono::steady_clock::now() + 10ms;

  std::atomic<bool> called{false};
  ex::sync_wait(
    sch.schedule_at(tp) | ex::then([&] { called = true; }));

  CHECK(called);
  ctx.request_stop();
  io_thread.join();
}

TEST_CASE("io_uring_context run_until_empty via when_all", "[io_uring]") {
  exec::io_uring_context ctx;
  auto sch = ctx.get_scheduler();

  std::atomic<bool> called{false};
  ex::sync_wait(ex::when_all(
    ex::schedule(sch) | ex::then([&] { called = true; }),
    ex::just() | ex::then([&] { ctx.run_until_empty(); })));

  CHECK(called);
  CHECK_FALSE(ctx.is_running());
  CHECK_FALSE(ctx.stop_requested());
}

TEST_CASE("io_uring_context run via sender with when_any", "[io_uring]") {
  exec::io_uring_context ctx;
  auto sch = ctx.get_scheduler();

  std::atomic<bool> called{false};
  ex::sync_wait(exec::when_any(
    ex::schedule(sch) | ex::then([&] { called = true; }),
    ctx.run()));

  CHECK(called);
  CHECK_FALSE(ctx.is_running());
  CHECK(ctx.stop_requested());
}

TEST_CASE("io_uring_context run via sender with until::empty", "[io_uring]") {
  exec::io_uring_context ctx;
  auto sch = ctx.get_scheduler();

  std::atomic<bool> called{false};
  ex::sync_wait(ex::when_all(
    ex::schedule(sch) | ex::then([&] { called = true; }),
    ctx.run(exec::until::empty)));

  CHECK(called);
  CHECK_FALSE(ctx.is_running());
  CHECK_FALSE(ctx.stop_requested());
}

TEST_CASE("io_uring_context request_stop stops context", "[io_uring]") {
  exec::io_uring_context ctx;
  auto sch = ctx.get_scheduler();

  std::thread io_thread{[&] { ctx.run_until_stopped(); }};
  ctx.request_stop();
  io_thread.join();

  CHECK(ctx.stop_requested());

  bool stopped = false;
  ex::sync_wait(
    ex::schedule(sch) | ex::upon_stopped([&] { stopped = true; }));
  CHECK(stopped);
}

TEST_CASE("io_uring_context reset and reuse", "[io_uring]") {
  exec::io_uring_context ctx;
  auto sch = ctx.get_scheduler();

  CHECK(ex::sync_wait(exec::when_any(ex::schedule(sch), ctx.run())));
  CHECK(ctx.stop_requested());

  CHECK_FALSE(ex::sync_wait(exec::when_any(ex::schedule(sch), ctx.run())));

  ctx.reset();
  CHECK_FALSE(ctx.stop_requested());
  CHECK(ex::sync_wait(exec::when_any(ex::schedule(sch), ctx.run())));
}

TEST_CASE("io_uring_context timed_scheduler concept satisfied", "[io_uring]") {
  exec::io_uring_context ctx;
  auto sch = ctx.get_scheduler();
  STATIC_REQUIRE(exec::timed_scheduler<decltype(sch)>);
}

TEST_CASE("io_uring_context multi-threaded schedule", "[io_uring]") {
  exec::io_uring_context ctx;
  auto sch = ctx.get_scheduler();

  std::thread io_thread{[&] { ctx.run_until_stopped(); }};

  std::atomic<int> count{0};

  std::thread t1{[&] {
    for (int i = 0; i < 10; ++i)
      ex::sync_wait(ex::schedule(sch) | ex::then([&] { count.fetch_add(1); }));
  }};
  std::thread t2{[&] {
    for (int i = 0; i < 10; ++i)
      ex::sync_wait(ex::schedule(sch) | ex::then([&] { count.fetch_add(1); }));
  }};
  t1.join();
  t2.join();

  CHECK(count.load() == 20);
  ctx.request_stop();
  io_thread.join();
}

TEST_CASE("io_uring_context now returns monotonic clock", "[io_uring]") {
  exec::io_uring_context ctx;
  auto sch = ctx.get_scheduler();
  auto start = exec::now(sch);
  std::this_thread::sleep_for(5ms);
  CHECK(start < exec::now(sch));
}
