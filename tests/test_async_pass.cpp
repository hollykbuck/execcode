#include <catch2/catch_test_macros.hpp>
#include <stdexec/execution.hpp>
#include <exec/static_thread_pool.hpp>

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>
#include <type_traits>

namespace ex = stdexec;

// =============================================================================
// async_pass — synchronous rendezvous primitive ported from libunifex
//
// Concept: one-to-one synchronous channel connecting caller and acceptor roles.
//   - async_call(args...) →  sender completes after acceptor receives args
//   - async_accept()      →  sender completes with args after receiving them
//
// Uses a tagged pointer to encode three states:
//   0                   = idle
//   even, non-zero      = caller_node* (caller waiting)
//   odd                 = acceptor_base* | 1 (acceptor waiting)
//
// When both call and accept are ready, rendezvous occurs:
//   caller's args are passed directly to acceptor, both complete synchronously.
// =============================================================================
template <typename... Args>
class async_pass {
  struct acceptor_base {
    void (*set_value_)(acceptor_base*, Args&&...) noexcept;
    void (*set_error_)(acceptor_base*, std::exception_ptr) noexcept;
    void (*resume_)(acceptor_base*) noexcept;
  };

  struct caller_base {
    void (*invoke_)(caller_base*, acceptor_base&) noexcept;
    void (*resume_)(caller_base*) noexcept;
  };

  static constexpr uintptr_t kAcceptorTag = 1;
  std::atomic<uintptr_t> state_{0};

  static bool is_caller(uintptr_t s) noexcept { return s != 0 && !(s & kAcceptorTag); }
  static bool is_acceptor(uintptr_t s) noexcept { return (s & kAcceptorTag) != 0; }

  static acceptor_base* as_acceptor(uintptr_t s) noexcept {
    return reinterpret_cast<acceptor_base*>(s ^ kAcceptorTag);
  }

  static caller_base* as_caller(uintptr_t s) noexcept {
    return reinterpret_cast<caller_base*>(s);
  }

  acceptor_base* call_or_suspend(caller_base* c) noexcept {
    uintptr_t s = state_.load(std::memory_order_acquire);
    while (true) {
      if (is_acceptor(s)) {
        if (state_.compare_exchange_weak(s, 0, std::memory_order_acq_rel, std::memory_order_acquire))
          return as_acceptor(s);
      } else if (s == 0) {
        if (state_.compare_exchange_weak(
              s, reinterpret_cast<uintptr_t>(c),
              std::memory_order_release, std::memory_order_acquire))
          return nullptr;
      } else {
        std::terminate();
      }
    }
  }

  caller_base* accept_or_suspend(acceptor_base* a) noexcept {
    uintptr_t s = state_.load(std::memory_order_acquire);
    uintptr_t tagged = reinterpret_cast<uintptr_t>(a) | kAcceptorTag;
    while (true) {
      if (is_caller(s)) {
        if (state_.compare_exchange_weak(s, 0, std::memory_order_acq_rel, std::memory_order_acquire))
          return as_caller(s);
      } else if (s == 0) {
        if (state_.compare_exchange_weak(
              s, tagged,
              std::memory_order_release, std::memory_order_acquire))
          return nullptr;
      } else {
        std::terminate();
      }
    }
  }

  class call_sender;
  class accept_sender;

public:
  async_pass() noexcept = default;
  async_pass(const async_pass&) = delete;
  async_pass(async_pass&&) = delete;

  auto async_call(Args... args) noexcept -> call_sender {
    return call_sender{*this, std::tuple<std::decay_t<Args>...>(std::move(args)...)};
  }

  auto async_accept() noexcept -> accept_sender {
    return accept_sender{*this};
  }

private:
  // ---------------------------------------------------------------
  // call_sender
  // ---------------------------------------------------------------
  class call_sender {
  public:
    using sender_concept = ex::sender_tag;
    using completion_signatures = ex::completion_signatures<ex::set_value_t()>;

    call_sender(const call_sender&) = delete;
    call_sender(call_sender&&) = default;

    template <typename R>
    struct op : caller_base {
      async_pass& pass_;
      std::tuple<std::decay_t<Args>...> args_;
      R rcvr_;

      template <typename R2>
      explicit op(async_pass& p, std::tuple<std::decay_t<Args>...> a, R2&& r) noexcept
        : pass_(p), args_(std::move(a)), rcvr_(static_cast<R2&&>(r)) {
        this->invoke_ = [](caller_base* base, acceptor_base& acc) noexcept {
          auto& self = *static_cast<op*>(base);
          std::apply([&](auto&... as) noexcept {
            (*acc.set_value_)(&acc, std::move(as)...);
          }, self.args_);
        };
        this->resume_ = [](caller_base* base) noexcept {
          auto& self = *static_cast<op*>(base);
          ex::set_value(static_cast<R&&>(self.rcvr_));
        };
      }

      op(op&&) = delete;

      void start() & noexcept {
        if (auto* acc = pass_.call_or_suspend(this)) {
          (*this->invoke_)(this, *acc);
          (*acc->resume_)(acc);
          ex::set_value(static_cast<R&&>(rcvr_));
        }
      }

      auto get_env() const noexcept { return ex::env<>{}; }
    };

    template <typename R>
    auto connect(R r) && -> op<R> {
      return op<R>{pass_, std::move(args_), static_cast<R&&>(r)};
    }

    auto get_env() const noexcept -> ex::env<> { return {}; }

  private:
    friend async_pass;
    explicit call_sender(async_pass& p, std::tuple<std::decay_t<Args>...> a) noexcept
      : pass_(p), args_(std::move(a)) {}
    async_pass& pass_;
    std::tuple<std::decay_t<Args>...> args_;
  };

  // ---------------------------------------------------------------
  // accept_sender
  // ---------------------------------------------------------------
  class accept_sender {
  public:
    using sender_concept = ex::sender_tag;
    using completion_signatures = ex::completion_signatures<
      ex::set_value_t(std::decay_t<Args>...),
      ex::set_error_t(std::exception_ptr)>;

    accept_sender(const accept_sender&) = delete;
    accept_sender(accept_sender&&) = default;

    template <typename R>
    struct op : acceptor_base {
      async_pass& pass_;
      R rcvr_;

      template <typename R2>
      explicit op(async_pass& p, R2&& r) noexcept
        : pass_(p), rcvr_(static_cast<R2&&>(r)) {
        this->set_value_ = [](acceptor_base* base, Args&&... args) noexcept {
          auto& self = *static_cast<op*>(base);
          ex::set_value(static_cast<R&&>(self.rcvr_), std::forward<Args>(args)...);
        };
        this->set_error_ = [](acceptor_base* base, std::exception_ptr ex) noexcept {
          auto& self = *static_cast<op*>(base);
          ex::set_error(static_cast<R&&>(self.rcvr_), std::move(ex));
        };
        this->resume_ = [](acceptor_base*) noexcept {};
      }

      op(op&&) = delete;

      void start() & noexcept {
        if (auto* caller = pass_.accept_or_suspend(this)) {
          (*caller->invoke_)(caller, *this);
          (*caller->resume_)(caller);
        }
      }

      auto get_env() const noexcept { return ex::env<>{}; }
    };

    template <typename R>
    auto connect(R r) && -> op<R> {
      return op<R>{pass_, static_cast<R&&>(r)};
    }

    auto get_env() const noexcept -> ex::env<> { return {}; }

  private:
    friend async_pass;
    explicit accept_sender(async_pass& p) noexcept : pass_(p) {}
    async_pass& pass_;
  };
};

// =============================================================================
// Tests
// =============================================================================
TEST_CASE("async_pass basic rendezvous: call before accept", "[async_pass]") {
  exec::static_thread_pool pool{2};
  auto sch = pool.get_scheduler();

  async_pass<int> pass;
  std::atomic<int> received{0};

  auto caller = ex::schedule(sch) | ex::then([&] {
    ex::sync_wait(pass.async_call(42));
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(10));

  auto acceptor = ex::schedule(sch) | ex::then([&] {
    auto [v] = ex::sync_wait(pass.async_accept()).value();
    received.store(v);
  });

  ex::sync_wait(ex::when_all(std::move(caller), std::move(acceptor)));
  REQUIRE(received.load() == 42);
}

TEST_CASE("async_pass basic rendezvous: accept before call", "[async_pass]") {
  exec::static_thread_pool pool{2};
  auto sch = pool.get_scheduler();

  async_pass<std::string> pass;
  std::atomic<bool> got_value{false};
  std::string result;

  auto acceptor = ex::schedule(sch) | ex::then([&] {
    auto [s] = ex::sync_wait(pass.async_accept()).value();
    result = std::move(s);
    got_value.store(true);
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(10));

  auto caller = ex::schedule(sch) | ex::then([&] {
    ex::sync_wait(pass.async_call(std::string{"hello"}));
  });

  ex::sync_wait(ex::when_all(std::move(acceptor), std::move(caller)));
  REQUIRE(got_value.load());
  REQUIRE(result == "hello");
}

TEST_CASE("async_pass rendezvous with multiple values", "[async_pass]") {
  exec::static_thread_pool pool{2};
  auto sch = pool.get_scheduler();

  async_pass<int, double, std::string> pass;
  std::tuple<int, double, std::string> result;

  // Use std::thread to ensure concurrency
  std::thread acceptor([&] {
    auto [a, b, c] = ex::sync_wait(pass.async_accept()).value();
    result = std::make_tuple(std::move(a), std::move(b), std::move(c));
  });

  std::thread caller([&] {
    ex::sync_wait(pass.async_call(7, 3.14, std::string{"pi"}));
  });

  acceptor.join();
  caller.join();

  REQUIRE(std::get<0>(result) == 7);
  REQUIRE(std::get<1>(result) == 3.14);
  REQUIRE(std::get<2>(result) == "pi");
}

TEST_CASE("async_pass stress test", "[async_pass][stress]") {
  constexpr int N = 100;
  async_pass<int> pass;
  std::atomic<int> sum{0};

  auto acceptor_work = [&] {
    for (int i = 0; i < N; ++i) {
      auto [v] = ex::sync_wait(pass.async_accept()).value();
      sum.fetch_add(v);
    }
  };

  auto caller_work = [&] {
    for (int i = 0; i < N; ++i) {
      ex::sync_wait(pass.async_call(1));
    }
  };

  std::thread acceptor(acceptor_work);
  std::thread caller(caller_work);

  acceptor.join();
  caller.join();

  REQUIRE(sum.load() == N);
}

TEST_CASE("async_pass call sender completion signatures", "[async_pass]") {
  using Sender = decltype(std::declval<async_pass<int>>().async_call(0));
  using Sigs = ex::completion_signatures_of_t<Sender>;
  // call_sender completes with void on rendezvous
  [[maybe_unused]] auto check = Sigs{};
}

TEST_CASE("async_pass accept sender completion signatures", "[async_pass]") {
  using Sender = decltype(std::declval<async_pass<int, double>>().async_accept());
  using Sigs = ex::completion_signatures_of_t<Sender>;
  // accept_sender completes with (int, double) or error
  [[maybe_unused]] auto check = Sigs{};
}
