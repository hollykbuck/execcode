#include <catch2/catch_test_macros.hpp>
#include <stdexec/execution.hpp>
#include <exec/inline_scheduler.hpp>

namespace ex = stdexec;

TEST_CASE("operator| chains sender adaptors", "[pipe]") {
  exec::inline_scheduler sch{};

  // Pipeline style
  auto [v] = ex::sync_wait(
    ex::schedule(sch)
      | ex::then([] { return 1; })
      | ex::then([](int x) { return x + 2; })
      | ex::then([](int x) { return x * 3; })
  ).value();

  REQUIRE(v == 9);
}

TEST_CASE("pipe syntax equals function call syntax", "[pipe]") {
  exec::inline_scheduler sch{};

  auto pipe_result = ex::sync_wait(
    ex::schedule(sch) | ex::then([] { return 42; })
  );

  auto func_result = ex::sync_wait(
    ex::then(ex::schedule(sch), [] { return 42; })
  );

  REQUIRE(pipe_result == func_result);
}

TEST_CASE("closure objects can be pre-composed", "[pipe]") {
  auto add_one = ex::then([](int x) { return x + 1; });
  auto mul_two = ex::then([](int x) { return x * 2; });

  auto [v] = ex::sync_wait(
    ex::just(10) | add_one | mul_two
  ).value();

  REQUIRE(v == 22);
}

TEST_CASE("adaptor closure with upon_error can be reused", "[pipe]") {
  auto safe_divide = [](int divisor) {
    return ex::then([divisor](int x) { return x / divisor; })
         | ex::upon_error([](std::exception_ptr) { return 0; });
  };

  auto [v1] = ex::sync_wait(ex::just(10) | safe_divide(2)).value();
  REQUIRE(v1 == 5);
}

TEST_CASE("consumer wraps pipeline result", "[pipe][sync_wait]") {
  auto pipeline = ex::just(7) | ex::then([](int x) { return x * 6; });
  auto [v] = ex::sync_wait(std::move(pipeline)).value();
  REQUIRE(v == 42);
}
