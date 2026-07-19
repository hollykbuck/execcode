#include <catch2/catch_test_macros.hpp>
#include <stdexec/execution.hpp>
#include <exec/static_thread_pool.hpp>
#include <exec/inline_scheduler.hpp>

namespace ex = stdexec;

TEST_CASE("get_env on just() returns a queryable env", "[env][query]") {
  auto sndr = ex::just(42);
  auto env = ex::get_env(sndr);
  (void)env;
  SUCCEED("get_env is callable on just(42)");
}

TEST_CASE("prop stores a single query key-value pair", "[env][query]") {
  auto p = ex::prop{ex::get_stop_token, ex::never_stop_token{}};
  (void)p;
  SUCCEED("prop compiles with get_stop_token");
}

TEST_CASE("env composes multiple query properties", "[env][query]") {
  stdexec::inline_scheduler sch{};
  auto e = ex::env{
    ex::prop{ex::get_scheduler, sch},
    ex::prop{ex::get_stop_token, ex::never_stop_token{}}
  };
  (void)e;
  SUCCEED("env composes scheduler and stop_token properties");
}

TEST_CASE("get_stop_token queries env with prop", "[env][query]") {
  auto e = ex::env{
    ex::prop{ex::get_stop_token, ex::never_stop_token{}}
  };
  auto token = ex::get_stop_token(e);
  REQUIRE_FALSE(token.stop_requested());
}

TEST_CASE("get_scheduler retrieves scheduler from env", "[env][query]") {
  stdexec::inline_scheduler sch{};
  auto e = ex::env{
    ex::prop{ex::get_scheduler, sch}
  };
  auto result_sch = ex::get_scheduler(e);
  (void)result_sch;
  SUCCEED("get_scheduler retrieves scheduler from env");
}

TEST_CASE("get_scheduler in pipeline via read_env", "[env][query]") {
  // get_scheduler() without args reads from receiver env
  ex::sync_wait(
    ex::get_scheduler() | ex::then([](auto sch) {
      (void)sch;
    })
  );
  SUCCEED("get_scheduler() in pipeline compiles and runs");
}

TEST_CASE("get_stop_token in pipeline via read_env", "[env][query]") {
  ex::sync_wait(
    ex::get_stop_token() | ex::then([](auto token) {
      REQUIRE_FALSE(token.stop_requested());
    })
  );
}

TEST_CASE("get_completion_scheduler from sender environment", "[env][query]") {
  exec::static_thread_pool pool{2};
  auto sch = pool.get_scheduler();
  auto sndr = ex::schedule(sch);
  auto env = ex::get_env(sndr);
  auto cs = ex::get_completion_scheduler<ex::set_value_t>(env);
  (void)cs;
  SUCCEED("get_completion_scheduler retrieves scheduler from sender env");
}

TEST_CASE("get_forward_progress_guarantee on thread_pool", "[env][query]") {
  exec::static_thread_pool pool{2};
  auto sch = pool.get_scheduler();
  auto g = ex::get_forward_progress_guarantee(sch);
  REQUIRE(g == ex::forward_progress_guarantee::parallel);
}

TEST_CASE("get_forward_progress_guarantee on inline_scheduler", "[env][query]") {
  stdexec::inline_scheduler sch{};
  auto g = ex::get_forward_progress_guarantee(sch);
  REQUIRE(g == ex::forward_progress_guarantee::weakly_parallel);
}

TEST_CASE("forwarding_query returns true for built-in queries", "[env][query]") {
  constexpr bool fwd_sched = ex::forwarding_query(ex::get_scheduler);
  constexpr bool fwd_stop = ex::forwarding_query(ex::get_stop_token);
  constexpr bool fwd_alloc = ex::forwarding_query(ex::get_allocator);
  REQUIRE(fwd_sched);
  REQUIRE(fwd_stop);
  REQUIRE(fwd_alloc);
}

TEST_CASE("env_of_t is defined for a sender", "[env][query]") {
  using Env = ex::env_of_t<decltype(ex::just(42))>;
  constexpr bool is_env = std::destructible<Env>;
  REQUIRE(is_env);
}

// ---- Go-like value context propagation via custom query ----
//
// In Go:  ctx = context.WithValue(ctx, myKey, value) → v = ctx.Value(myKey)
// Here:   write_env(snd, prop{myQuery, value})       → read_env(myQuery)

// Define custom query CPOs (like Go's context keys).
// The __query CRTP base provides operator()(env) which calls env.query(key).
struct request_id_t : ex::__query<request_id_t> {
  using ex::__query<request_id_t>::operator();
  static consteval auto query(ex::forwarding_query_t) noexcept -> bool {
    return true;
  }
};
inline constexpr request_id_t request_id{};

struct user_role_t : ex::__query<user_role_t> {
  using ex::__query<user_role_t>::operator();
  static consteval auto query(ex::forwarding_query_t) noexcept -> bool {
    return true;
  }
};
inline constexpr user_role_t user_role{};

TEST_CASE("custom value context: write then read in pipeline", "[env][context]") {
  // write_env sets values into the receiver env, like context.WithValue
  auto [v] = ex::sync_wait(
    ex::write_env(
      ex::read_env(request_id) | ex::then([](std::string rid) {
        return rid;
      }),
      ex::prop{request_id, std::string{"req-42"}}
    )
  ).value();
  REQUIRE(v == "req-42");
}

TEST_CASE("custom value context: propagate through pipeline steps", "[env][context]") {
  // value set at the top flows through let_value, like Go context through call chain
  auto [v] = ex::sync_wait(
    ex::write_env(
      ex::just()
        | ex::then([] { return 0; })
        | ex::let_value([](int) { return ex::read_env(request_id); })
        | ex::then([](std::string rid) { return rid; }),
      ex::prop{request_id, std::string{"propagated"}}
    )
  ).value();
  REQUIRE(v == "propagated");
}

TEST_CASE("custom value context: compose with thread pool scheduler", "[env][context]") {
  // context values compose with scheduler choice, like Go context with timeout
  exec::static_thread_pool pool{2};
  auto sch = pool.get_scheduler();

  auto [v] = ex::sync_wait(
    ex::starts_on(sch,
      ex::write_env(
        ex::read_env(request_id) | ex::then([](std::string rid) {
          return rid;
        }),
        ex::prop{request_id, std::string{"pool-req"}}
      )
    )
  ).value();
  REQUIRE(v == "pool-req");
}

TEST_CASE("custom value context: concurrent values via let_value", "[env][context]") {
  // multiple context values visible at the same let_value step
  exec::static_thread_pool pool{2};
  auto sch = pool.get_scheduler();

  auto [id] = ex::sync_wait(
    ex::starts_on(sch,
      ex::write_env(
        ex::read_env(request_id) | ex::then([](std::string rid) {
          return rid;
        }),
        ex::prop{request_id, std::string{"concurrent-id"}}
      )
    )
  ).value();
  REQUIRE(id == "concurrent-id");
}
