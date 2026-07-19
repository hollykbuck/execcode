#include <catch2/catch_test_macros.hpp>
#include <stdexec/execution.hpp>
#include <exec/static_thread_pool.hpp>
#include <exec/inline_scheduler.hpp>

namespace ex = stdexec;

TEST_CASE("default_domain is default_constructible", "[domain]") {
  ex::default_domain dom{};
  (void)dom;
  SUCCEED("default_domain default-constructs");
}

TEST_CASE("get_domain on empty env returns a domain", "[domain]") {
  auto e = ex::env{};
  auto dom = ex::get_domain(e);
  (void)dom;
  SUCCEED("get_domain on env returns a domain");
}

TEST_CASE("get_domain from scheduler's get_env", "[domain]") {
  exec::static_thread_pool pool{2};
  auto sch = pool.get_scheduler();
  auto env = ex::get_env(sch);
  auto dom = ex::get_domain(env);
  (void)dom;
  SUCCEED("get_domain on scheduler env compiles");
}

TEST_CASE("get_domain from inline_scheduler", "[domain]") {
  stdexec::inline_scheduler sch{};
  auto env = ex::get_env(sch);
  auto dom = ex::get_domain(env);
  (void)dom;
  SUCCEED("get_domain on inline_scheduler env compiles");
}

TEST_CASE("get_completion_domain on schedule sender", "[domain]") {
  exec::static_thread_pool pool{2};
  auto sch = pool.get_scheduler();
  auto sndr = ex::schedule(sch);
  auto env = ex::get_env(sndr);
  auto dom = ex::get_completion_domain<ex::set_value_t>(env);
  (void)dom;
  SUCCEED("get_completion_domain<set_value_t> compiles for schedule sender");
}

TEST_CASE("indeterminate_domain is default_constructible", "[domain]") {
  ex::indeterminate_domain<> dom{};
  (void)dom;
  SUCCEED("indeterminate_domain<> default-constructs");
}

TEST_CASE("default_domain is trivially copyable", "[domain]") {
  constexpr bool is_trivial = std::is_trivially_copyable_v<ex::default_domain>;
  REQUIRE(is_trivial);
}

TEST_CASE("domain propagation via write_env pipeline", "[domain]") {
  exec::static_thread_pool pool{2};
  auto sch = pool.get_scheduler();

  auto [v] = ex::sync_wait(
    ex::write_env(
      ex::just() | ex::then([] { return 42; }),
      ex::prop{ex::get_scheduler, sch}
    ) | ex::then([](int x) { return x; })
  ).value();
  REQUIRE(v == 42);
}

TEST_CASE("get_domain in pipeline via read_env", "[domain]") {
  ex::sync_wait(
    ex::read_env(ex::get_domain) | ex::then([](auto dom) {
      (void)dom;
    })
  );
  SUCCEED("read_env with get_domain compiles");
}

TEST_CASE("transform_sender identity preserves sender", "[domain]") {
  auto sndr = ex::just(42);
  auto transformed = ex::transform_sender(std::move(sndr));
  auto [v] = ex::sync_wait(std::move(transformed)).value();
  REQUIRE(v == 42);
}


