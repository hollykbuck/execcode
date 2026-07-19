#include <catch2/catch_test_macros.hpp>
#include <stdexec/execution.hpp>
#include <exec/static_thread_pool.hpp>

#include <atomic>
#include <cstdint>
#include <thread>

namespace ex = stdexec;

namespace {

// =============================================================================
// async_mutex — ported from libunifex v1 algorithm
//
// Core idea: encode lock state and waiter stack into a tagged pointer:
//   - bit 0: 1 = inactive (unlocked), 0 = active (locked)
//   - bits 1+: pointer to waiter stack top (only valid when active)
//
// unlock has two phases:
//   1) First try to resume the next waiter from the local pending queue
//   2) Otherwise atomically: try to mark inactive; if there are waiters,
//      dequeue all to the pending queue, then resume the first one
// =============================================================================
class async_mutex {
  struct waiter_base {
    waiter_base* next_ = nullptr;
    void (*resume_)(waiter_base*) noexcept;
  };

  static constexpr uintptr_t INACTIVE = 1;

  std::atomic<uintptr_t> state_{INACTIVE};

  waiter_base* pending_head_ = nullptr;
  waiter_base* pending_tail_ = nullptr;

  class lock_sender;

public:
  async_mutex() noexcept = default;
  async_mutex(const async_mutex&) = delete;
  async_mutex(async_mutex&&) = delete;

  ~async_mutex() {
    // All waiters must have been processed
  }

  bool try_lock() noexcept {
    uintptr_t expected = INACTIVE;
    return state_.compare_exchange_strong(
      expected, 0, std::memory_order_acquire, std::memory_order_relaxed);
  }

  void unlock() noexcept {
    if (pending_head_) {
      waiter_base* w = pending_head_;
      pending_head_ = w->next_;
      if (!pending_head_)
        pending_tail_ = nullptr;
      w->resume_(w);
      return;
    }

    waiter_base* list = try_mark_inactive_or_dequeue_all();
    if (!list)
      return;

    // Reverse stack → FIFO pending queue
    waiter_base* rev = nullptr;
    while (list) {
      waiter_base* next = list->next_;
      list->next_ = rev;
      rev = list;
      list = next;
    }
    pending_head_ = rev;

    waiter_base* tail = rev;
    while (tail && tail->next_)
      tail = tail->next_;
    pending_tail_ = tail;

    waiter_base* w = pending_head_;
    pending_head_ = w->next_;
    if (!pending_head_)
      pending_tail_ = nullptr;
    w->resume_(w);
  }

  auto async_lock() noexcept -> lock_sender {
    return lock_sender{*this};
  }

private:
  // Atomically: if queue empty → mark INACTIVE; otherwise dequeue all → return stack top
  waiter_base* try_mark_inactive_or_dequeue_all() noexcept {
    uintptr_t old = state_.load(std::memory_order_relaxed);
    do {
      if (old == 0) {
        if (state_.compare_exchange_weak(
              old, INACTIVE, std::memory_order_release, std::memory_order_relaxed))
          return nullptr;
      } else {
        waiter_base* list = reinterpret_cast<waiter_base*>(old & ~uintptr_t(1));
        if (state_.compare_exchange_weak(
              old, 0, std::memory_order_acquire, std::memory_order_relaxed))
          return list;
      }
    } while (true);
  }

  // Enqueue or mark active. Returns true=enqueued (waiting); false=got lock
  bool try_enqueue(waiter_base* w) noexcept {
    uintptr_t old = state_.load(std::memory_order_relaxed);
    do {
      uintptr_t new_val;
      if (old & INACTIVE) {
        new_val = 0;
      } else {
        w->next_ = reinterpret_cast<waiter_base*>(old & ~uintptr_t(1));
        new_val = reinterpret_cast<uintptr_t>(w);
      }
      if (state_.compare_exchange_weak(
            old, new_val, std::memory_order_acq_rel, std::memory_order_relaxed))
        return !(old & INACTIVE);
    } while (true);
  }

  // ---- lock_sender ----
  class lock_sender {
  public:
    using sender_concept = ex::sender_tag;

    lock_sender(const lock_sender&) = delete;
    lock_sender(lock_sender&&) = default;

    template <typename R>
    struct op : waiter_base {
      async_mutex& mutex_;
      R rcvr_;

      template <typename R2>
      explicit op(async_mutex& m, R2&& r) noexcept
        : mutex_(m), rcvr_(static_cast<R2&&>(r)) {
        this->resume_ = [](waiter_base* self) noexcept {
          auto& self_op = *static_cast<op*>(self);
          ex::set_value(static_cast<R&&>(self_op.rcvr_));
        };
      }

      op(op&&) = delete;

      void start() & noexcept {
        if (!mutex_.try_enqueue(this)) {
          ex::set_value(static_cast<R&&>(rcvr_));
        }
      }
    };

    template <typename R>
    auto connect(R r) && -> op<R> {
      return op<R>{mutex_, static_cast<R&&>(r)};
    }

    // get_completion_signatures customized via tag_invoke
    template <typename Self, typename Env>
    friend auto tag_invoke(ex::get_completion_signatures_t, Self&&, Env&&) noexcept
      -> ex::completion_signatures<ex::set_value_t()>;

    auto get_env() const noexcept -> ex::env<> {
      return {};
    }

  private:
    friend async_mutex;
    explicit lock_sender(async_mutex& m) noexcept : mutex_(m) {}
    async_mutex& mutex_;
  };
};

} // anonymous namespace

// =============================================================================
// Basic functionality tests
// =============================================================================
TEST_CASE("async_mutex try_lock succeeds when unlocked", "[async_mutex]") {
  async_mutex mtx;
  REQUIRE(mtx.try_lock());
  mtx.unlock();
}

TEST_CASE("async_mutex try_lock fails when locked", "[async_mutex]") {
  async_mutex mtx;
  REQUIRE(mtx.try_lock());
  REQUIRE_FALSE(mtx.try_lock());
  mtx.unlock();
}

TEST_CASE("async_mutex try_lock succeeds after unlock", "[async_mutex]") {
  async_mutex mtx;
  REQUIRE(mtx.try_lock());
  mtx.unlock();
  REQUIRE(mtx.try_lock());
  mtx.unlock();
}

TEST_CASE("async_mutex async_lock no contention", "[async_mutex]") {
  async_mutex mtx;
  ex::sync_wait(mtx.async_lock());
  mtx.unlock();
}

TEST_CASE("async_mutex async_lock with try_lock blocks", "[async_mutex]") {
  async_mutex mtx;
  REQUIRE(mtx.try_lock());

  std::atomic<bool> acquired{false};
  std::thread t([&] {
    ex::sync_wait(
      ex::then(mtx.async_lock(), [&] { acquired.store(true); })
    );
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  REQUIRE_FALSE(acquired.load());

  mtx.unlock();
  t.join();
  REQUIRE(acquired.load());
}

// =============================================================================
// Classic two-thread shared state increment (using static_thread_pool)
// =============================================================================
TEST_CASE("async_mutex two threads increment via static_thread_pool",
          "[async_mutex][thread_pool]") {
  exec::static_thread_pool pool{2};
  auto sch = pool.get_scheduler();

  async_mutex mtx;
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
// Low-level test: manual connect/start receiver
// =============================================================================
TEST_CASE("async_mutex manual connect/start", "[async_mutex]") {
  struct my_receiver {
    using receiver_concept = ex::receiver_tag;
    std::atomic<bool>& done_;
    void set_value() noexcept { done_.store(true); }
    void set_error(std::exception_ptr) noexcept {}
    void set_stopped() noexcept {}
    auto get_env() const noexcept { return ex::env<>{}; }
  };

  async_mutex mtx;
  std::atomic<bool> done{false};

  mtx.try_lock();  // lock first so async_lock enqueues

  auto op = ex::connect(std::move(mtx.async_lock()), my_receiver{done});
  ex::start(op);

  REQUIRE_FALSE(done.load());
  mtx.unlock();
  REQUIRE(done.load());
}

// =============================================================================
// Stress test: multi-threaded contention
// =============================================================================
TEST_CASE("async_mutex stress test with raw threads", "[async_mutex][stress]") {
  async_mutex mtx;
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
