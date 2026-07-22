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
// async_manual_reset_event — ported from libunifex v2
//
// Differences from async_mutex:
//   - set() wakes ALL waiters (not just one)
//   - stays signalled after triggering, new waiters complete immediately
//   - reset() manually clears the signalled state
//   - supports cancellation: stop token can cancel waiting before set()
//
// State: signalled_ (atomic bool)
//   true  → signalled, new async_wait completes immediately
//   false → not signalled, waiters are enqueued
//
// set():
//   lock → signalled = true → dequeue all → unlock → resume each
//
// reset():
//   signalled = false
//
// async_wait():
//   start() → StopsEarly → register stop callback
//     → try_enqueue (returns false if signalled)
//       → if signalled: immediately set_value
//       → if enqueued: wait for set()
//
//   stop() → try_remove: success → set_stopped;
//                           failure → cancelled_ = true (checked by resume_)
//
//   resume_: completed_ coordination + cancelled_ decides
//     set_value or set_stopped
// =============================================================================
class async_manual_reset_event {
  struct waiter_base {
    waiter_base* next_ = nullptr;
    void (*resume_)(waiter_base*) noexcept;
  };

  std::mutex mutex_;
  std::deque<waiter_base*> waiters_;
  std::atomic<bool> signalled_{false};

  class wait_sender;

public:
  async_manual_reset_event() noexcept = default;

  explicit async_manual_reset_event(bool initially_signalled) noexcept
    : signalled_(initially_signalled) {}

  bool ready() const noexcept {
    return signalled_.load(std::memory_order_acquire);
  }

  void set() noexcept {
    std::deque<waiter_base*> to_resume;
    {
      std::lock_guard lk(mutex_);
      signalled_.store(true, std::memory_order_release);
      to_resume.swap(waiters_);
    }
    for (auto* w : to_resume) {
      w->resume_(w);
    }
  }

  void reset() noexcept {
    signalled_.store(false, std::memory_order_release);
  }

  auto async_wait() noexcept -> wait_sender {
    return wait_sender{*this};
  }

private:
  // Try to enqueue. Returns true = enqueued, false = already signalled
  bool try_enqueue(waiter_base* w) noexcept {
    std::lock_guard lk(mutex_);
    if (signalled_.load(std::memory_order_relaxed)) {
      return false;
    }
    waiters_.push_back(w);
    return true;
  }

  // Remove from queue. Returns true = removed successfully
  bool try_remove(waiter_base* w) noexcept {
    std::lock_guard lk(mutex_);
    for (auto it = waiters_.begin(); it != waiters_.end(); ++it) {
      if (*it == w) {
        waiters_.erase(it);
        return true;
      }
    }
    return false;
  }

    // Forward declaration for stop_handler
  template <typename R>
  struct amre_op;

  // Generic stop_handler, not nested inside op
  template <typename Op>
  struct amre_stop_handler {
    Op* self_;
    void operator()() noexcept { self_->stop(); }
  };

  // ---- wait_sender ----
  class wait_sender {
  public:
    using sender_concept = ex::sender_tag;

    wait_sender(const wait_sender&) = delete;
    wait_sender(wait_sender&&) = default;

    template <typename R>
    struct op : waiter_base {
      using stop_token_t = ex::stop_token_of_t<ex::env_of_t<R>>;

      using cb_t = ex::stop_callback_for_t<
        stop_token_t, amre_stop_handler<op>>;

      async_manual_reset_event& evt_;
      R rcvr_;
      std::atomic<bool> completed_{false};
      std::optional<cb_t> cb_;
      bool cancelled_ = false;

      template <typename R2>
      explicit op(async_manual_reset_event& e, R2&& r) noexcept
        : evt_(e)
        , rcvr_(static_cast<R2&&>(r)) {
        this->resume_ = [](waiter_base* self) noexcept {
          auto& self_op = *static_cast<op*>(self);
          self_op.cb_.reset();
          if (!self_op.completed_.exchange(true, std::memory_order_acq_rel)) {
            if (self_op.cancelled_)
              ex::set_stopped(static_cast<R&&>(self_op.rcvr_));
            else
              ex::set_value(static_cast<R&&>(self_op.rcvr_));
          }
        };
      }

      op(op&&) = delete;

      void start() & noexcept {
        auto token = ex::get_stop_token(ex::get_env(rcvr_));

        if (token.stop_requested()) {
          ex::set_stopped(static_cast<R&&>(rcvr_));
          return;
        }

        if (token.stop_possible()) {
          cb_.emplace(token, amre_stop_handler<op>{this});
          if (cancelled_) {
            if (!completed_.exchange(true, std::memory_order_acq_rel)) {
              ex::set_stopped(static_cast<R&&>(rcvr_));
            }
            return;
          }
        }

        if (!evt_.try_enqueue(this)) {
          cb_.reset();
          if (!completed_.exchange(true, std::memory_order_acq_rel)) {
            ex::set_value(static_cast<R&&>(rcvr_));
          }
          return;
        }
      }

      void stop() noexcept {
        if (evt_.try_remove(this)) {
          cb_.reset();
          if (!completed_.exchange(true, std::memory_order_acq_rel)) {
            ex::set_stopped(static_cast<R&&>(rcvr_));
          }
          return;
        }
        cancelled_ = true;
      }
    };

    template <typename R>
    auto connect(R r) && -> op<R> {
      return op<R>{evt_, static_cast<R&&>(r)};
    }

    template <typename Self, typename Env>
    friend auto tag_invoke(ex::get_completion_signatures_t, Self&&, Env&&) noexcept
      -> ex::completion_signatures<ex::set_value_t(), ex::set_stopped_t()>;

    auto get_env() const noexcept -> ex::env<> {
      return {};
    }

  private:
    friend async_manual_reset_event;
    explicit wait_sender(async_manual_reset_event& e) noexcept : evt_(e) {}
    async_manual_reset_event& evt_;
  };
};

// =============================================================================
// Helper types
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

// =============================================================================
// Basic functionality tests
// =============================================================================
TEST_CASE("amre default is not ready", "[amre]") {
  async_manual_reset_event evt;
  REQUIRE_FALSE(evt.ready());
}

TEST_CASE("amre initially signalled is ready", "[amre]") {
  async_manual_reset_event evt{true};
  REQUIRE(evt.ready());
}

TEST_CASE("amre set makes event ready", "[amre]") {
  async_manual_reset_event evt;
  evt.set();
  REQUIRE(evt.ready());
}

TEST_CASE("amre reset makes event not ready", "[amre]") {
  async_manual_reset_event evt{true};
  REQUIRE(evt.ready());
  evt.reset();
  REQUIRE_FALSE(evt.ready());
}

TEST_CASE("amre set on already signalled is no-op", "[amre]") {
  async_manual_reset_event evt{true};
  evt.set(); // second set
  REQUIRE(evt.ready());
}

TEST_CASE("amre async_wait on ready event completes immediately",
          "[amre]") {
  async_manual_reset_event evt{true};
  ex::sync_wait(evt.async_wait());
}

TEST_CASE("amre async_wait on unready blocks until set", "[amre]") {
  async_manual_reset_event evt;
  std::atomic<bool> completed{false};

  std::thread t([&] {
    ex::sync_wait(evt.async_wait());
    completed.store(true);
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  REQUIRE_FALSE(completed.load());

  evt.set();
  t.join();
  REQUIRE(completed.load());
}

TEST_CASE("amre set/reset/set cycle", "[amre]") {
  async_manual_reset_event evt;

  evt.set();
  REQUIRE(evt.ready());

  evt.reset();
  REQUIRE_FALSE(evt.ready());

  // Now async_wait should block
  std::atomic<bool> completed{false};
  std::thread t([&] {
    ex::sync_wait(evt.async_wait());
    completed.store(true);
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  REQUIRE_FALSE(completed.load());

  evt.set();
  t.join();
  REQUIRE(completed.load());
}

// =============================================================================
// Multiple waiters: set() wakes all
// =============================================================================
TEST_CASE("amre multiple waiters all complete on set", "[amre]") {
  async_manual_reset_event evt;
  std::atomic<int> count{0};
  std::atomic<int> started{0};
  constexpr int N = 5;

  std::vector<std::thread> threads;
  for (int i = 0; i < N; ++i) {
    threads.emplace_back([&] {
      started.fetch_add(1);
      ex::sync_wait(evt.async_wait());
      count.fetch_add(1);
    });
  }

  while (started.load() < N)
    std::this_thread::yield();
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  REQUIRE(count.load() == 0);

  evt.set();
  for (auto& t : threads) t.join();
  REQUIRE(count.load() == N);
}

// =============================================================================
// Cancellation tests
// =============================================================================
TEST_CASE("amre cancel while waiting", "[amre][cancel]") {
  async_manual_reset_event evt;
  ex::inplace_stop_source stop_src;
  std::atomic<bool> done{false};
  std::atomic<bool> stopped{false};

  auto op = ex::connect(
    evt.async_wait(),
    stoppable_receiver{done, stopped, stop_src.get_token()}
  );
  ex::start(op);

  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  REQUIRE_FALSE(done.load());

  stop_src.request_stop();
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  REQUIRE(done.load());
  REQUIRE(stopped.load());

  // Should not be woken after set
  evt.set(); // should be no-op for the cancelled waiter
}

TEST_CASE("amre cancel before start (StopsEarly)", "[amre][cancel]") {
  async_manual_reset_event evt;
  ex::inplace_stop_source stop_src;
  std::atomic<bool> done{false};
  std::atomic<bool> stopped{false};

  stop_src.request_stop();

  auto op = ex::connect(
    evt.async_wait(),
    stoppable_receiver{done, stopped, stop_src.get_token()}
  );
  ex::start(op);

  REQUIRE(done.load());
  REQUIRE(stopped.load());
}

TEST_CASE("amre cancel one among multiple waiters", "[amre][cancel]") {
  async_manual_reset_event evt;
  ex::inplace_stop_source stop_src;
  std::atomic<bool> w1_done{false};
  std::atomic<bool> w1_stopped{false};
  std::atomic<bool> w2_done{false};
  std::atomic<bool> w2_stopped{false};

  // waiter 1: stoppable
  auto op1 = ex::connect(
    evt.async_wait(),
    stoppable_receiver{w1_done, w1_stopped, stop_src.get_token()}
  );
  ex::start(op1);

  // waiter 2: stoppable with different source
  ex::inplace_stop_source stop_src2;
  auto op2 = ex::connect(
    evt.async_wait(),
    stoppable_receiver{w2_done, w2_stopped, stop_src2.get_token()}
  );
  ex::start(op2);

  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  REQUIRE_FALSE(w1_done.load());
  REQUIRE_FALSE(w2_done.load());

  // Cancel waiter1
  stop_src.request_stop();
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  REQUIRE(w1_done.load());
  REQUIRE(w1_stopped.load());

  // waiter2 is still waiting
  REQUIRE_FALSE(w2_done.load());

  // set → waiter2 should complete
  evt.set();
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  REQUIRE(w2_done.load());
  REQUIRE_FALSE(w2_stopped.load());
}

TEST_CASE("amre cancel then set is no-op for cancelled", "[amre][cancel]") {
  async_manual_reset_event evt;
  ex::inplace_stop_source stop_src;
  std::atomic<bool> done{false};
  std::atomic<bool> stopped{false};

  auto op = ex::connect(
    evt.async_wait(),
    stoppable_receiver{done, stopped, stop_src.get_token()}
  );
  ex::start(op);

  stop_src.request_stop();
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  REQUIRE(done.load());
  REQUIRE(stopped.load());

  // When set arrives, waiter is already cancelled → should not complete again
  done.store(false);
  evt.set();
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  // waiter should not complete again
  REQUIRE_FALSE(done.load());
}

// =============================================================================
// Stress test
// =============================================================================
TEST_CASE("amre multiple threads set and wait", "[amre][stress]") {
  async_manual_reset_event evt;
  std::atomic<int> count{0};

  std::thread waiter_threads[5];
  for (int i = 0; i < 5; ++i) {
    waiter_threads[i] = std::thread([&] {
      ex::sync_wait(evt.async_wait());
      count.fetch_add(1);
    });
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  evt.set();

  for (auto& t : waiter_threads) t.join();
  REQUIRE(count.load() == 5);
}

} // anonymous namespace
