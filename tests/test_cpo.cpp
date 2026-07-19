#include <catch2/catch_test_macros.hpp>
#include <stdexec/execution.hpp>
#include <exec/inline_scheduler.hpp>

namespace ex = stdexec;

// ---- Custom CPO using member function pattern ----

struct my_cpo_t {
  template <class T>
    requires requires(T&& t) { std::forward<T>(t).my_cpo(); }
  constexpr auto operator()(T&& t) const
    -> decltype(std::forward<T>(t).my_cpo()) {
    return std::forward<T>(t).my_cpo();
  }
};
inline constexpr my_cpo_t my_cpo{};

struct my_cpo_type {
  int my_cpo() const { return 42; }
};

TEST_CASE("custom CPO via member function is callable", "[cpo]") {
  my_cpo_type obj{};
  int result = my_cpo(obj);
  REQUIRE(result == 42);
}

// ---- Custom scheduler with member schedule() ----

struct my_int_sender {
  using sender_concept = ex::sender_tag;
  using completion_signatures = ex::completion_signatures<ex::set_value_t(int)>;

  template <class R>
  struct op {
    R r_;
    void start() & noexcept {
      ex::set_value(std::move(r_), 42);
    }
  };

  template <class R>
  auto connect(R r) const -> op<R> {
    return op<R>{std::move(r)};
  }
};

struct my_scheduler {
  auto schedule() const noexcept -> my_int_sender {
    return {};
  }
  bool operator==(const my_scheduler&) const = default;
};

TEST_CASE("custom scheduler with member schedule() is callable", "[cpo][scheduler]") {
  my_scheduler sch{};
  auto snd = ex::schedule(sch);
  auto [v] = ex::sync_wait(std::move(snd)).value();
  REQUIRE(v == 42);
}

// ---- sender_adaptor_closure ----

struct mul_adaptor : ex::sender_adaptor_closure<mul_adaptor> {
  int factor_;
  explicit mul_adaptor(int f) : factor_(f) {}

  template <ex::sender Sndr>
  auto operator()(Sndr&& sndr) const {
    return ex::then(std::forward<Sndr>(sndr), [f = factor_](int x) { return x * f; });
  }
};

TEST_CASE("custom sender_adaptor_closure is pipeable", "[cpo][adaptor]") {
  auto [v] = ex::sync_wait(
    ex::just(21) | mul_adaptor{2}
  ).value();
  REQUIRE(v == 42);
}

TEST_CASE("custom adaptor composes with then adaptor", "[cpo][adaptor]") {
  auto [v] = ex::sync_wait(
    ex::just(20)
      | mul_adaptor{2}
      | ex::then([](int x) { return x + 1; })
  ).value();
  REQUIRE(v == 41);
}

// ---- forwarding_query on custom query ----

struct my_fwd_query_t {
  static consteval auto query(ex::forwarding_query_t) noexcept -> bool {
    return true;
  }
};
inline constexpr my_fwd_query_t my_fwd_query{};

TEST_CASE("forwarding_query recognizes custom forwarding query", "[cpo]") {
  constexpr bool is_fwd = ex::forwarding_query(my_fwd_query);
  REQUIRE(is_fwd);
}

// ---- Non-forwarding query ----

struct my_non_fwd_query_t {};
inline constexpr my_non_fwd_query_t my_non_fwd_query{};

TEST_CASE("forwarding_query returns false for non-forwarding query", "[cpo]") {
  constexpr bool is_fwd = ex::forwarding_query(my_non_fwd_query);
  REQUIRE_FALSE(is_fwd);
}

// ---- adapter closure for void-returning pipelines ----

struct write_adaptor : ex::sender_adaptor_closure<write_adaptor> {
  int value_;
  explicit write_adaptor(int v) : value_(v) {}

  template <ex::sender Sndr>
  auto operator()(Sndr&& sndr) const {
    return ex::then(std::forward<Sndr>(sndr), [v = value_](int) { return v; });
  }
};

TEST_CASE("custom adaptor replaces value in pipeline", "[cpo][adaptor]") {
  auto [v] = ex::sync_wait(
    ex::just(0) | write_adaptor{42}
  ).value();
  REQUIRE(v == 42);
}
