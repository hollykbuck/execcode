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
#include <type_traits>
#include <string_view>
#include <tuple>

// ============================================================
// P2300 structured-binding tag extraction — the core idea
//
// A sender is conceptually a product-type<Tag, Data, Child...>,
// i.e. a tuple-like value where the FIRST element is always the
// algorithm tag (then_t, schedule_t, when_all_t, etc.).
//
//   auto&& [tag, data, ...children] = sndr;
//   tag_of_t<Sndr> == decltype(auto(tag))
//
// The tag type then dispatches implementation via impls-for<Tag>.
// ============================================================

// Algorithm tags (like stdexec::then_t, etc.)
struct tag_then {};
struct tag_just {};

// tag_of_t: the type of the first tuple element
template <class Sndr>
using tag_of_t = std::tuple_element_t<0, std::decay_t<Sndr>>;

// impls-for: dispatch implementation per tag
template <class Tag>
struct impls_for {
  static constexpr std::string_view name = "unknown";
};

template <>
struct impls_for<tag_then> {
  static constexpr std::string_view name = "then";
};

template <>
struct impls_for<tag_just> {
  static constexpr std::string_view name = "just";
};

// ============================================================
// Tests
// ============================================================

TEST_CASE("tuple structured binding extracts tag as first element", "[tag_of]") {
  // A sender is a tuple: [tag, data, children...]
  std::tuple<tag_then, int, int> sndr{tag_then{}, 42, 7};

  auto&& [tag, data, child] = sndr;

  // The first structured binding element IS the algorithm tag
  // The tag type is tag_then (decays to value type for comparison)
  REQUIRE(std::is_same_v<std::decay_t<decltype(tag)>, tag_then>);
  REQUIRE(data == 42);
  REQUIRE(child == 7);
}

TEST_CASE("tag_of_t reads the tag type via tuple_element", "[tag_of]") {
  using sender_t = std::tuple<tag_then, int>;

  // tag_of_t is just the type of element 0
  using tag_t = tag_of_t<sender_t>;
  REQUIRE(std::is_same_v<tag_t, tag_then>);
}

TEST_CASE("impls-for dispatches per tag", "[tag_of]") {
  using sender_t = std::tuple<tag_then, int>;

  using tag_t = tag_of_t<sender_t>;
  REQUIRE(std::is_same_v<tag_t, tag_then>);
  REQUIRE(impls_for<tag_t>::name == "then");
}

TEST_CASE("different tags give different impls-for", "[tag_of]") {
  {
    using sender_t = std::tuple<tag_then, int>;
    REQUIRE(impls_for<tag_of_t<sender_t>>::name == "then");
  }
  {
    using sender_t = std::tuple<tag_just, int>;
    REQUIRE(impls_for<tag_of_t<sender_t>>::name == "just");
  }
}
