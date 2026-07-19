#include <catch2/catch_test_macros.hpp>
#include <stdexec/execution.hpp>
#include <exec/static_thread_pool.hpp>
#include <exec/inline_scheduler.hpp>

namespace ex = stdexec;

// ============================================================
// Implementing then from scratch: custom sender + receiver + operation_state
// Corresponds to Chapter 32 of the tutorial
// ============================================================

template <class R, class F>
struct _then_receiver {
  using receiver_concept = ex::receiver_tag;

  R rcvr_;
  F fn_;

  template <class... Args>
  void set_value(Args&&... args) && noexcept {
    auto& rcvr = rcvr_;
    auto& fn = fn_;
    if constexpr (std::same_as<void, std::invoke_result_t<F, Args...>>) {
      STDEXEC_TRY {
        std::invoke(fn, std::forward<Args>(args)...);
        ex::set_value(std::move(rcvr));
      }
      STDEXEC_CATCH_ALL {
        ex::set_error(std::move(rcvr), std::current_exception());
      }
    } else {
      STDEXEC_TRY {
        ex::set_value(std::move(rcvr),
          std::invoke(fn, std::forward<Args>(args)...));
      }
      STDEXEC_CATCH_ALL {
        ex::set_error(std::move(rcvr), std::current_exception());
      }
    }
  }

  void set_error(auto&& e) && noexcept {
    ex::set_error(std::move(rcvr_), std::forward<decltype(e)>(e));
  }

  void set_stopped() && noexcept {
    ex::set_stopped(std::move(rcvr_));
  }

  auto get_env() const noexcept {
    return ex::get_env(rcvr_);
  }
};

template <ex::sender S, class F>
struct then_sender {
  using sender_concept = ex::sender_tag;

  S s_;
  F f_;

  template <class... Args>
  using _set_value_t =
    ex::completion_signatures<ex::set_value_t(std::invoke_result_t<F, Args...>)>;

  template <class... Env>
  using _completions_t = ex::__transform_completion_signatures_t<
    ex::completion_signatures_of_t<S, Env...>,
    ex::completion_signatures<ex::set_error_t(std::exception_ptr)>,
    _set_value_t>;

  template <class, class... Env>
  static consteval auto get_completion_signatures() -> _completions_t<Env...> {
    return {};
  }

  template <ex::receiver R>
    requires ex::sender_to<S, _then_receiver<R, F>>
  auto connect(R r) && {
    return ex::connect(std::move(s_),
      _then_receiver<R, F>{std::move(r), std::move(f_)});
  }
};

// ============================================================
// Tests
// ============================================================

TEST_CASE("impl then: transforms int values", "[impl_then]") {
  auto [v] = ex::sync_wait(
    then_sender{ex::just(40), [](int x) { return x + 2; }}
  ).value();
  REQUIRE(v == 42);
}

TEST_CASE("impl then: multiple value args", "[impl_then]") {
  auto [v] = ex::sync_wait(
    then_sender{ex::just(3, 4), [](int a, int b) { return a + b; }}
  ).value();
  REQUIRE(v == 7);
}

TEST_CASE("impl then: chains multiple operations", "[impl_then]") {
  auto s1 = then_sender{ex::just(5), [](int x) { return x * 2; }};
  auto s2 = then_sender{std::move(s1), [](int x) { return x + 1; }};
  auto [v] = ex::sync_wait(std::move(s2)).value();
  REQUIRE(v == 11);
}

TEST_CASE("impl then: void function", "[impl_then]") {
  int side_effect = 0;
  ex::sync_wait(
    then_sender{ex::just(), [&] { side_effect = 99; }}
  );
  REQUIRE(side_effect == 99);
}

TEST_CASE("impl then: passes through error", "[impl_then]") {
  bool error_handled = false;
  auto [v] = ex::sync_wait(
    ex::upon_error(
      then_sender{
        ex::just_error(std::make_exception_ptr(std::runtime_error{"test"})),
        [](int x) { return x + 1; }
      },
      [&](std::exception_ptr) { error_handled = true; return -1; })
  ).value();
  REQUIRE(error_handled);
  REQUIRE(v == -1);
}

TEST_CASE("impl then: passes through stopped", "[impl_then]") {
  bool stopped_handled = false;
  auto [v] = ex::sync_wait(
    ex::upon_stopped(
      then_sender{
        ex::just_stopped(),
        [](int x) { return x + 1; }
      },
      [&] { stopped_handled = true; return 0; })
  ).value();
  REQUIRE(stopped_handled);
  REQUIRE(v == 0);
}

TEST_CASE("impl then: with thread pool", "[impl_then]") {
  exec::static_thread_pool pool{2};
  auto sch = pool.get_scheduler();
  auto [v] = ex::sync_wait(
    then_sender{
      ex::schedule(sch) | ex::then([] { return 7; }),
      [](int x) { return x * 6; }
    }
  ).value();
  REQUIRE(v == 42);
}

TEST_CASE("impl then: string transform", "[impl_then]") {
  auto [v] = ex::sync_wait(
    then_sender{ex::just(42), [](int x) { return "value: " + std::to_string(x); }}
  ).value();
  REQUIRE(v == "value: 42");
}

TEST_CASE("impl then: on scheduler via starts_on", "[impl_then]") {
  exec::inline_scheduler sch{};
  auto [v] = ex::sync_wait(
    ex::starts_on(sch, then_sender{ex::just(36), [](int x) { return x + 6; }})
  ).value();
  REQUIRE(v == 42);
}
