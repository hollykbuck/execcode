#include <catch2/catch_test_macros.hpp>
#include <stdexec/execution.hpp>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <functional>
#include <vector>
#include <atomic>

namespace ex = stdexec;

// ============================================================
// 1.  Minimal inline_scheduler from scratch
//     Corresponds to Chapter 34 "Minimal Implementation"
// ============================================================

class custom_inline_scheduler {
public:
  using scheduler_concept = ex::scheduler_tag;

  custom_inline_scheduler() = default;

  constexpr auto query(ex::get_forward_progress_guarantee_t) const noexcept
    -> ex::forward_progress_guarantee {
    return ex::forward_progress_guarantee::concurrent;
  }

  struct schedule_sender {
    using sender_concept = ex::sender_tag;
    using completion_signatures = ex::completion_signatures<ex::set_value_t()>;

    template <class R>
    struct op {
      using operation_state_concept = ex::operation_state_tag;
      R rcvr_;
      void start() & noexcept {
        ex::set_value(static_cast<R&&>(rcvr_));
      }
    };

    template <class R>
    static constexpr auto connect(R r) noexcept -> op<R> {
      return {static_cast<R&&>(r)};
    }
  };

  static constexpr schedule_sender schedule() noexcept {
    return {};
  }

  constexpr bool operator==(const custom_inline_scheduler&) const = default;
};

// ============================================================
// 2.  Simplified trampoline_scheduler — deferred execution
//     Corresponds to Chapter 34 "Advanced: trampoline_scheduler"
// ============================================================

namespace {
  thread_local bool g_trampoline_active = false;
  thread_local std::queue<std::function<void()>> g_trampoline_queue;
}

class custom_trampoline_scheduler {
public:
  using scheduler_concept = ex::scheduler_tag;

  custom_trampoline_scheduler() = default;

  constexpr auto query(ex::get_forward_progress_guarantee_t) const noexcept
    -> ex::forward_progress_guarantee {
    return ex::forward_progress_guarantee::concurrent;
  }

  struct schedule_sender {
    using sender_concept = ex::sender_tag;
    using completion_signatures = ex::completion_signatures<ex::set_value_t()>;

    template <class R>
    struct op {
      using operation_state_concept = ex::operation_state_tag;
      R rcvr_;

      void start() & noexcept {
        if (g_trampoline_active) {
          g_trampoline_queue.push([this] {
            ex::set_value(static_cast<R&&>(rcvr_));
          });
        } else {
          g_trampoline_active = true;
          ex::set_value(static_cast<R&&>(rcvr_));
          while (!g_trampoline_queue.empty()) {
            auto task = std::move(g_trampoline_queue.front());
            g_trampoline_queue.pop();
            task();
          }
          g_trampoline_active = false;
        }
      }
    };

    template <class R>
    auto connect(R r) const -> op<R> {
      return {static_cast<R&&>(r)};
    }
  };

  schedule_sender schedule() const noexcept {
    return {};
  }

  bool operator==(const custom_trampoline_scheduler&) const = default;
};

// ============================================================
// 3.  Custom thread_pool scheduler
//     Corresponds to Chapter 34 "Production-Grade" skeleton
// ============================================================

class custom_thread_pool {
  std::vector<std::thread> threads_;
  std::queue<std::function<void()>> queue_;
  std::mutex mtx_;
  std::condition_variable cv_;
  std::atomic<bool> done_{false};

  void worker() {
    while (true) {
      std::function<void()> task;
      {
        std::unique_lock lock(mtx_);
        cv_.wait(lock, [this] { return done_.load() || !queue_.empty(); });
        if (done_.load() && queue_.empty())
          break;
        task = std::move(queue_.front());
        queue_.pop();
      }
      task();
    }
  }

public:
  explicit custom_thread_pool(std::size_t n) {
    threads_.reserve(n);
    for (std::size_t i = 0; i < n; ++i)
      threads_.emplace_back(&custom_thread_pool::worker, this);
  }

  ~custom_thread_pool() {
    done_.store(true);
    cv_.notify_all();
    for (auto& t : threads_)
      if (t.joinable())
        t.join();
  }

  void enqueue(std::function<void()> task) {
    {
      std::lock_guard lock(mtx_);
      queue_.push(std::move(task));
    }
    cv_.notify_one();
  }

  class scheduler {
  public:
    using scheduler_concept = ex::scheduler_tag;

    explicit scheduler(custom_thread_pool* pool) noexcept : pool_(pool) {}

    auto query(ex::get_forward_progress_guarantee_t) const noexcept
      -> ex::forward_progress_guarantee {
      return ex::forward_progress_guarantee::parallel;
    }

    struct schedule_sender {
      using sender_concept = ex::sender_tag;
      using completion_signatures = ex::completion_signatures<ex::set_value_t()>;
      custom_thread_pool* pool_;

      template <class R>
      struct op {
        using operation_state_concept = ex::operation_state_tag;
        custom_thread_pool* pool_;
        R rcvr_;

        void start() & noexcept {
          pool_->enqueue([this] {
            ex::set_value(static_cast<R&&>(rcvr_));
          });
        }
      };

      template <class R>
      auto connect(R r) const -> op<R> {
        return {pool_, static_cast<R&&>(r)};
      }
    };

    schedule_sender schedule() const noexcept {
      return schedule_sender{pool_};
    }

    bool operator==(const scheduler&) const = default;

  private:
    custom_thread_pool* pool_;
  };

  scheduler get_scheduler() noexcept { return scheduler{this}; }
};

// ============================================================
// Tests — custom inline_scheduler
// ============================================================

TEST_CASE("custom inline_scheduler: executes task and returns value", "[custom_scheduler]") {
  custom_inline_scheduler sch;
  auto [v] = ex::sync_wait(
    ex::schedule(sch) | ex::then([] { return 42; })
  ).value();
  REQUIRE(v == 42);
}

TEST_CASE("custom inline_scheduler: runs on the current thread", "[custom_scheduler]") {
  custom_inline_scheduler sch;
  std::thread::id caller = std::this_thread::get_id();
  std::thread::id worker{};

  ex::sync_wait(
    ex::schedule(sch) | ex::then([&] { worker = std::this_thread::get_id(); })
  );

  REQUIRE(worker == caller);
}

TEST_CASE("custom inline_scheduler: forward_progress_guarantee is concurrent", "[custom_scheduler]") {
  custom_inline_scheduler sch;
  auto g = ex::get_forward_progress_guarantee(sch);
  REQUIRE(g == ex::forward_progress_guarantee::concurrent);
}

TEST_CASE("custom inline_scheduler: equality comparison", "[custom_scheduler]") {
  custom_inline_scheduler a, b;
  REQUIRE(a == b);
}

TEST_CASE("custom inline_scheduler: composes with when_all", "[custom_scheduler]") {
  custom_inline_scheduler sch;
  auto [a, b] = ex::sync_wait(
    ex::when_all(
      ex::schedule(sch) | ex::then([] { return 1; }),
      ex::schedule(sch) | ex::then([] { return 2; })
    )
  ).value();
  REQUIRE(a == 1);
  REQUIRE(b == 2);
}

TEST_CASE("custom inline_scheduler: composes with starts_on", "[custom_scheduler]") {
  custom_inline_scheduler sch;
  auto [v] = ex::sync_wait(
    ex::starts_on(sch, ex::just(42))
  ).value();
  REQUIRE(v == 42);
}

// ============================================================
// Tests — custom trampoline_scheduler
// ============================================================

TEST_CASE("custom trampoline_scheduler: executes task and returns value", "[custom_scheduler]") {
  custom_trampoline_scheduler sch;
  auto [v] = ex::sync_wait(
    ex::schedule(sch) | ex::then([] { return 42; })
  ).value();
  REQUIRE(v == 42);
}

TEST_CASE("custom trampoline_scheduler: defers nested schedules and drains queue", "[custom_scheduler]") {
  custom_trampoline_scheduler sch;
  std::vector<int> order;

  ex::sync_wait(
    ex::schedule(sch)
      | ex::then([&] { order.push_back(1); })
      | ex::let_value([&] {
          return ex::schedule(sch) | ex::then([&] { order.push_back(2); });
        })
      | ex::then([&] { order.push_back(3); })
  );

  REQUIRE(order.size() == 3);
  REQUIRE(order[0] == 1);
  REQUIRE(order[1] == 2);
  REQUIRE(order[2] == 3);
}

// ============================================================
// Tests — custom_thread_pool scheduler
// ============================================================

TEST_CASE("custom_thread_pool: executes task and returns value", "[custom_scheduler]") {
  custom_thread_pool pool{2};
  auto sch = pool.get_scheduler();
  auto [v] = ex::sync_wait(
    ex::schedule(sch) | ex::then([] { return 42; })
  ).value();
  REQUIRE(v == 42);
}

TEST_CASE("custom_thread_pool: runs on a different thread", "[custom_scheduler]") {
  custom_thread_pool pool{2};
  auto sch = pool.get_scheduler();
  std::thread::id caller = std::this_thread::get_id();
  std::thread::id worker{};

  ex::sync_wait(
    ex::schedule(sch) | ex::then([&] { worker = std::this_thread::get_id(); })
  );

  REQUIRE(worker != std::thread::id{});
  REQUIRE(worker != caller);
}

TEST_CASE("custom_thread_pool: forward_progress_guarantee is parallel", "[custom_scheduler]") {
  custom_thread_pool pool{2};
  auto sch = pool.get_scheduler();
  auto g = ex::get_forward_progress_guarantee(sch);
  REQUIRE(g == ex::forward_progress_guarantee::parallel);
}

TEST_CASE("custom_thread_pool: composes with when_all", "[custom_scheduler]") {
  custom_thread_pool pool{4};
  auto sch = pool.get_scheduler();

  auto [a, b] = ex::sync_wait(
    ex::when_all(
      ex::schedule(sch) | ex::then([] { return 10; }),
      ex::schedule(sch) | ex::then([] { return 32; })
    )
  ).value();

  REQUIRE(a == 10);
  REQUIRE(b == 32);
}
