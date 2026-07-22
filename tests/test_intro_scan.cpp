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
#include <catch2/catch_approx.hpp>
#include <stdexec/execution.hpp>
#include <exec/static_thread_pool.hpp>
#include <algorithm>
#include <vector>
#include <span>
#include <array>

namespace ex = stdexec;

// P2300R9 §1.3.2 — Asynchronous inclusive scan
// https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2022/p2300r9.html#example-async-inclusive-scan

template <ex::scheduler Sch>
[[nodiscard]]
auto async_inclusive_scan(
    Sch sch,
    std::span<const double> input,
    std::span<double> output,
    double init,
    std::size_t tile_count) -> ex::sender auto
{
    std::size_t const tile_size = (input.size() + tile_count - 1) / tile_count;

    std::vector<double> partials(tile_count + 1);
    partials[0] = init;

    return ex::just(std::move(partials))
         | ex::continues_on(sch)
         | ex::bulk(tile_count,
             [=](std::size_t i, std::vector<double>& partials) {
                 auto start = i * tile_size;
                 auto end   = std::min(input.size(), (i + 1) * tile_size);
                 partials[i + 1] = *--std::inclusive_scan(
                     begin(input) + static_cast<long>(start),
                     begin(input) + static_cast<long>(end),
                     begin(output) + static_cast<long>(start));
             })
         | ex::then(
             [](std::vector<double>&& partials) {
                 std::inclusive_scan(begin(partials), end(partials), begin(partials));
                 return std::move(partials);
             })
         | ex::bulk(tile_count,
             [=](std::size_t i, std::vector<double>& partials) {
                 auto start = i * tile_size;
                 auto end   = std::min(input.size(), (i + 1) * tile_size);
                 std::for_each(
                     begin(output) + static_cast<long>(start),
                     begin(output) + static_cast<long>(end),
                     [&](double& e) { e = partials[i] + e; });
             })
         | ex::then(
             [=](std::vector<double>&&) {
                 return output;
             });
}

TEST_CASE("async_inclusive_scan produces correct result", "[intro][scan]") {
    exec::static_thread_pool pool{4};
    auto sch = pool.get_scheduler();

    std::array input{1.0, 2.0, -1.0, -2.0};
    std::array<double, 4> output{};

    auto [result] = ex::sync_wait(
        async_inclusive_scan(sch, input, output, 0.0, 2)
    ).value();

    CHECK(result.data() == output.data());
    CHECK(output[0] == Catch::Approx(1.0));
    CHECK(output[1] == Catch::Approx(3.0));
    CHECK(output[2] == Catch::Approx(2.0));
    CHECK(output[3] == Catch::Approx(0.0));
}

TEST_CASE("async_inclusive_scan with single tile", "[intro][scan]") {
    exec::static_thread_pool pool{2};
    auto sch = pool.get_scheduler();

    std::array input{3.0, 1.0, 4.0, 1.0, 5.0};
    std::array<double, 5> output{};

    auto [result] = ex::sync_wait(
        async_inclusive_scan(sch, input, output, 0.0, 1)
    ).value();

    CHECK(result.data() == output.data());
    CHECK(output[0] == Catch::Approx(3.0));
    CHECK(output[1] == Catch::Approx(4.0));
    CHECK(output[2] == Catch::Approx(8.0));
    CHECK(output[3] == Catch::Approx(9.0));
    CHECK(output[4] == Catch::Approx(14.0));
}
