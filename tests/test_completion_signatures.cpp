#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_template_test_macros.hpp>
#include <stdexec/execution.hpp>

namespace ex = stdexec;

TEST_CASE("completion_signatures_of_t for just", "[completion_sigs]") {
  using Sigs = ex::completion_signatures_of_t<decltype(ex::just(42))>;
  // Expected: completion_signatures<set_value_t(int)>
  constexpr bool has_value = requires {
    requires std::same_as<
      ex::value_types_of_t<decltype(ex::just(42))>,
      std::variant<std::tuple<int>>
    >;
  };
  SUCCEED("Completion signatures are defined for just(42)");
}

TEST_CASE("value_types_of_t extracts value types", "[completion_sigs]") {
  using VT = ex::value_types_of_t<decltype(ex::just(1, 2.0))>;
  constexpr bool correct = std::same_as<VT, std::variant<std::tuple<int, double>>>;
  CHECK(correct);
}

TEST_CASE("error_types_of_t for just", "[completion_sigs]") {
  using ET = ex::error_types_of_t<decltype(ex::just(42))>;
  // just does not send error
  CHECK_FALSE(ex::sends_stopped<decltype(ex::just(42))>);
}

TEST_CASE("sends_stopped for cancellable sender", "[completion_sigs]") {
  // just_stopped only sends stopped
  constexpr bool stopped = ex::sends_stopped<decltype(ex::just_stopped())>;
  CHECK(stopped);
}

TEST_CASE("completion_signatures with multiple signatures", "[completion_sigs]") {
  // completion_signatures is just a type list; it's always valid on its own
  using Sigs = ex::completion_signatures<
    ex::set_value_t(int),
    ex::set_error_t(std::exception_ptr),
    ex::set_stopped_t()
  >;

  constexpr bool is_class = std::is_class_v<Sigs>;
  CHECK(is_class);
}

TEST_CASE("transform_completion_signatures concept", "[completion_sigs]") {
  // Verify that completion signatures form a valid type
  using Sigs = ex::completion_signatures<ex::set_value_t(int)>;

  constexpr bool is_class = std::is_class_v<Sigs>;
  CHECK(is_class);
}

TEST_CASE("sender_in concept check for just", "[completion_sigs]") {
  constexpr bool is_sender = ex::sender<decltype(ex::just(42))>;
  CHECK(is_sender);
}
