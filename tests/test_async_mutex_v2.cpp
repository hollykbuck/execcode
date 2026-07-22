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
#include <stdexec/execution.hpp>
#include <exec/static_thread_pool.hpp>

#include <atomic>
#include <deque>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

namespace ex = stdexec;

namespace {

// =============================================================================
// async_mutex (v2) — ported from libunifex v2
//
// New features compared to v1:
//   1. Cancellation support — stop token can cancel a lock-waiting operation
//   2. Dekker synchronization — seq_cst fence prevents lost wakeups
//
// Algorithm details in the libunifex v2 source:
//   include/unifex/v2/async_mutex.hpp
//   source/async_mutex_v2.cpp
// =============================================================================
class async_mutex_v2 {
  struct waiter_base {
    waiter_base* next_ = nullptr;
    void (*resume_)(waiter_base*) noexcept;
  };

  std::mutex mutex_;
  std::deque<waiter_base*> queue_;
  std::atomic<bool> locked_{false};

  class lock_sender;

public:
  async_mutex_v2() noexcept = default;
  async_mutex_v2(const async_mutex_v2&) = delete;
  async_mutex_v2(async_mutex_v2&&) = delete;

  bool try_lock() noexcept {
    return !locked_.exchange(true, std::memory_order_acquire);
  }

  void unlock() noexcept {
    process_queue();
  }

  auto async_lock() noexcept -> lock_sender {
    return lock_sender{*this};
  }

private:
  void process_queue() noexcept {
    while (true) {
      waiter_base* w = nullptr;
      {
        std::lock_guard lk(mutex_);
        if (!queue_.empty()) {
          w = queue_.front();
          queue_.pop_front();
        }
      }
      if (w) {
        w->resume_(w);
        return;
      }

      locked_.store(false, std::memory_order_release);

      std::atomic_thread_fence(std::memory_order_seq_cst);

      {
        std::lock_guard lk(mutex_);
        if (queue_.empty()) {
          return;
        }
      }

      if (locked_.exchange(true, std::memory_order_acq_rel)) {
        return;
      }
    }
  }

  bool try_remove(waiter_base* w) noexcept {
    std::lock_guard lk(mutex_);
    for (auto it = queue_.begin(); it != queue_.end(); ++it) {
      if (*it == w) {
        queue_.erase(it);
        return true;
      }
    }
    return false;
  }

  void push_back(waiter_base* w) noexcept {
    std::lock_guard lk(mutex_);
    queue_.push_back(w);
  }

  // ---- lock_sender ----
  class lock_sender {
  public:
    using sender_concept = ex::sender_tag;

    lock_sender(const lock_sender&) = delete;
    lock_sender(lock_sender&&) = default;

    template <typename R>
    struct op : waiter_base {
      using stop_token_t = ex::stop_token_of_t<ex::env_of_t<R>>;

      // stop_handler: called by stop_callback with no args
      struct stop_handler {
        op* self_;
        void operator()() noexcept { self_->stop(); }
      };

      using cb_t = ex::stop_callback_for_t<stop_token_t, stop_handler>;

      async_mutex_v2& mutex_;
      R rcvr_;
      std::atomic<bool> completed_{false};
      std::optional<cb_t> cb_;
      bool started_ = false;
      bool cancelled_ = false;

      template <typename R2>
      explicit op(async_mutex_v2& m, R2&& r) noexcept
        : mutex_(m)
        , rcvr_(static_cast<R2&&>(r)) {
        this->resume_ = [](waiter_base* self) noexcept {
          auto& self_op = *static_cast<op*>(self);
          self_op.cb_.reset();
          if (!self_op.completed_.exchange(true, std::memory_order_acq_rel)) {
            if (self_op.cancelled_)
              ex::set_stopped(static_cast<R&&>(self_op.rcvr_));
            else
              ex::set_value(static_cast<R&&>(self_op.rcvr_));
          } else {
            // Cancellation already won → release lock (process_queue gave us the lock)
            self_op.mutex_.unlock();
          }
        };
      }

      op(op&&) = delete;

      void start() & noexcept {
        auto token = ex::get_stop_token(ex::get_env(rcvr_));

        // StopsEarly
        if (token.stop_requested()) {
          ex::set_stopped(static_cast<R&&>(rcvr_));
          return;
        }

        started_ = true;

        // Register stop callback
        if (token.stop_possible()) {
          cb_.emplace(token, stop_handler{this});
        }

        // Fast path: try_lock succeeded
        if (mutex_.try_lock()) {
          cb_.reset();
          if (!completed_.exchange(true, std::memory_order_acq_rel)) {
            ex::set_value(static_cast<R&&>(rcvr_));
          }
          return;
        }

        // Enqueue
        mutex_.push_back(this);

        // Dekker fence
        std::atomic_thread_fence(std::memory_order_seq_cst);

        // Try to acquire the lock again
        if (!mutex_.locked_.exchange(true, std::memory_order_acq_rel)) {
          mutex_.process_queue();
        }
      }

      void stop() noexcept {
        cancelled_ = true;
        if (mutex_.try_remove(this)) {
          cb_.reset();
          if (!completed_.exchange(true, std::memory_order_acq_rel)) {
            ex::set_stopped(static_cast<R&&>(rcvr_));
          }
        }
      }
    };

    template <typename R>
    auto connect(R r) && -> op<R> {
      return op<R>{mutex_, static_cast<R&&>(r)};
    }

    template <typename Self, typename Env>
    friend auto tag_invoke(ex::get_completion_signatures_t, Self&&, Env&&) noexcept
      -> ex::completion_signatures<ex::set_value_t(), ex::set_stopped_t()>;

    auto get_env() const noexcept -> ex::env<> {
      return {};
    }

  private:
    friend async_mutex_v2;
    explicit lock_sender(async_mutex_v2& m) noexcept : mutex_(m) {}
    async_mutex_v2& mutex_;
  };
};

} // anonymous namespace

// =============================================================================
// Receiver helper types
// =============================================================================
struct stoppable_env {
  ex::inplace_stop_token token_;
  auto query(ex::get_stop_token_t) const noexcept { return token_; }
};

struct stoppable_receiver {
  using receiver_concept = ex::receiver_tag;
  std::atomic<bool>& done_;
  std::atomic<bool>& stopped_;
  ex::inplace_stop_token token_;

  void set_value() noexcept { done_.store(true); }
  void set_error(std::exception_ptr) noexcept {}
  void set_stopped() noexcept { stopped_.store(true); done_.store(true); }
  auto get_env() const noexcept { return stoppable_env{token_}; }
};

struct unstoppable_receiver {
  using receiver_concept = ex::receiver_tag;
  std::atomic<bool>& done_;
  bool& ran_;

  void set_value() noexcept { ran_ = true; done_.store(true); }
  void set_error(std::exception_ptr) noexcept {}
  void set_stopped() noexcept {}
  auto get_env() const noexcept { return ex::env<>{}; }
};

// =============================================================================
// Basic functionality tests
// =============================================================================
TEST_CASE("async_mutex_v2 try_lock", "[async_mutex_v2]") {
  async_mutex_v2 mtx;
  REQUIRE(mtx.try_lock());
  REQUIRE_FALSE(mtx.try_lock());
  mtx.unlock();
  REQUIRE(mtx.try_lock());
  mtx.unlock();
}

TEST_CASE("async_mutex_v2 async_lock no contention", "[async_mutex_v2]") {
  async_mutex_v2 mtx;
  ex::sync_wait(mtx.async_lock());
  mtx.unlock();
}

TEST_CASE("async_mutex_v2 async_lock blocks until unlock",
          "[async_mutex_v2]") {
  async_mutex_v2 mtx;
  REQUIRE(mtx.try_lock());

  std::atomic<bool> acquired{false};
  std::thread t([&] {
    ex::sync_wait(ex::then(mtx.async_lock(), [&] { acquired.store(true); }));
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  REQUIRE_FALSE(acquired.load());

  mtx.unlock();
  t.join();
  REQUIRE(acquired.load());
}

// =============================================================================
// Two threads incrementing shared state (via static_thread_pool)
// =============================================================================
TEST_CASE("async_mutex_v2 two threads increment", "[async_mutex_v2][thread_pool]") {
  exec::static_thread_pool pool{2};
  auto sch = pool.get_scheduler();

  async_mutex_v2 mtx;
  int shared = 0;
  constexpr int N = 10000;

  auto worker = [&](int) -> ex::task<void> {
    for (int i = 0; i < N; ++i) {
      co_await mtx.async_lock();
      co_await ex::schedule(sch);
      ++shared;
      mtx.unlock();
    }
  };

  ex::sync_wait(ex::when_all(worker(1), worker(2)));
  REQUIRE(shared == 2 * N);
}

// =============================================================================
// Cancellation test: cancel after async_lock
// =============================================================================
TEST_CASE("async_mutex_v2 cancel awaiting lock via stop_source",
          "[async_mutex_v2][cancel]") {
  async_mutex_v2 mtx;
  ex::inplace_stop_source stop_src;
  std::atomic<bool> done{false};
  std::atomic<bool> stopped{false};

  mtx.try_lock(); // hold the lock

  auto op = ex::connect(
    mtx.async_lock(),
    stoppable_receiver{done, stopped, stop_src.get_token()}
  );

  ex::start(op);

  // Operation should be enqueued and waiting
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  REQUIRE_FALSE(done.load());

  // Request stop → cancel the enqueued waiter
  stop_src.request_stop();
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  REQUIRE(done.load());
  REQUIRE(stopped.load());

  mtx.unlock();
}

TEST_CASE("async_mutex_v2 cancel before start (StopsEarly)",
          "[async_mutex_v2][cancel]") {
  async_mutex_v2 mtx;
  ex::inplace_stop_source stop_src;
  std::atomic<bool> done{false};
  std::atomic<bool> stopped{false};

  stop_src.request_stop(); // stop first
  mtx.try_lock();

  auto op = ex::connect(
    mtx.async_lock(),
    stoppable_receiver{done, stopped, stop_src.get_token()}
  );

  // StopsEarly should detect stop is already requested and immediately set_stopped
  ex::start(op);

  REQUIRE(done.load());
  REQUIRE(stopped.load());
  mtx.unlock();
}

TEST_CASE("async_mutex_v2 cancel one waiter, next gets lock",
          "[async_mutex_v2][cancel]") {
  async_mutex_v2 mtx;
  ex::inplace_stop_source stop_src;
  std::atomic<bool> w1_done{false};
  std::atomic<bool> w1_stopped{false};
  std::atomic<bool> w2_done{false};

  mtx.try_lock(); // holder

  // waiter 1: stoppable
  auto op1 = ex::connect(
    mtx.async_lock(),
    stoppable_receiver{w1_done, w1_stopped, stop_src.get_token()}
  );
  ex::start(op1);

  // waiter 2: unstoppable
  bool w2_ran = false;
  auto op2 = ex::connect(
    mtx.async_lock(),
    unstoppable_receiver{w2_done, w2_ran}
  );
  ex::start(op2);

  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  REQUIRE_FALSE(w1_done.load());
  REQUIRE_FALSE(w2_done.load());

  // Cancel waiter1 → waiter2 should acquire the lock
  stop_src.request_stop();
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  REQUIRE(w1_done.load());
  REQUIRE(w1_stopped.load());
  REQUIRE_FALSE(w2_done.load()); // waiter2 is still waiting

  mtx.unlock();
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  REQUIRE(w2_done.load());
  REQUIRE(w2_ran);
}

// =============================================================================
// Stress test
// =============================================================================
TEST_CASE("async_mutex_v2 stress", "[async_mutex_v2][stress]") {
  async_mutex_v2 mtx;
  std::atomic<int> counter{0};
  constexpr int N = 5000;

  std::thread t1([&] {
    for (int i = 0; i < N; ++i) {
      ex::sync_wait(mtx.async_lock());
      ++counter;
      mtx.unlock();
    }
  });

  std::thread t2([&] {
    for (int i = 0; i < N; ++i) {
      ex::sync_wait(mtx.async_lock());
      ++counter;
      mtx.unlock();
    }
  });

  t1.join();
  t2.join();
  REQUIRE(counter.load() == 2 * N);
}

// =============================================================================
// Low-level connect/start test
// =============================================================================
TEST_CASE("async_mutex_v2 manual connect/start", "[async_mutex_v2]") {
  struct my_receiver {
    using receiver_concept = ex::receiver_tag;
    std::atomic<bool>& done_;
    void set_value() noexcept { done_.store(true); }
    void set_error(std::exception_ptr) noexcept {}
    void set_stopped() noexcept { done_.store(true); }
    auto get_env() const noexcept { return ex::env<>{}; }
  };

  async_mutex_v2 mtx;
  std::atomic<bool> done{false};
  mtx.try_lock();

  auto op = ex::connect(std::move(mtx.async_lock()), my_receiver{done});
  ex::start(op);

  REQUIRE_FALSE(done.load());
  mtx.unlock();
  REQUIRE(done.load());
}
