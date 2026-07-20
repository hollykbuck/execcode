#include <catch2/catch_test_macros.hpp>
#include <stdexec/execution.hpp>
#include <exec/static_thread_pool.hpp>
#include <cstring>
#include <array>
#include <thread>
#include <atomic>

namespace ex = stdexec;

// P2300R9 §1.5 "Examples: Server theme" — Moving between execution resources
// https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2022/p2300r9.html#example-server-on

namespace {

auto legacy_read_from_socket(int /*sock*/, char* buffer, size_t buffer_len) -> size_t {
    const char fake_data[] = "Hello, world!";
    size_t sz = sizeof(fake_data) - 1;
    size_t count = (std::min)(sz, buffer_len);
    std::memcpy(buffer, fake_data, count);
    return count;
}

void process_read_data(char* /*read_data*/, size_t read_len) {
    CHECK(read_len > 0);
}

} // anonymous namespace

TEST_CASE("server starts_on + continues_on switches execution resources", "[server][on]") {
    exec::static_thread_pool io_pool{1};
    exec::static_thread_pool work_pool{4};
    auto io_sched = io_pool.get_scheduler();
    auto work_sched = work_pool.get_scheduler();

    std::array<char, 256> buffer{};
    std::atomic<std::thread::id> read_thread_id{};
    std::atomic<std::thread::id> process_thread_id{};
    auto main_tid = std::this_thread::get_id();

    auto snd_read = ex::just(0, buffer.data(), buffer.size())
                  | ex::then(legacy_read_from_socket);

    auto snd =
        ex::starts_on(io_sched, std::move(snd_read))
        | ex::then([&](size_t len) {
            read_thread_id.store(std::this_thread::get_id());
            return len;
        })
        | ex::continues_on(work_sched)
        | ex::then([&](size_t len) {
            process_thread_id.store(std::this_thread::get_id());
            process_read_data(buffer.data(), len);
        });

    auto opt = ex::sync_wait(std::move(snd));
    REQUIRE(opt.has_value());

    // read should have happened on IO pool thread (not main, not work)
    REQUIRE(read_thread_id.load() != main_tid);
    // process should have happened on work pool thread
    REQUIRE(process_thread_id.load() != main_tid);
    // They should be different threads (different pools)
    REQUIRE(read_thread_id.load() != process_thread_id.load());
}

TEST_CASE("server starts_on reads on specified scheduler", "[server][on]") {
    exec::static_thread_pool io_pool{1};
    auto io_sched = io_pool.get_scheduler();

    std::array<char, 64> buffer{};
    std::atomic<std::thread::id> exec_tid{};
    auto main_tid = std::this_thread::get_id();

    ex::sender auto snd = ex::starts_on(io_sched,
        ex::just(0, buffer.data(), buffer.size())
        | ex::then([&](int, char* buf, size_t sz) {
            exec_tid.store(std::this_thread::get_id());
            return legacy_read_from_socket(0, buf, sz);
        })
    );

    auto [len] = ex::sync_wait(std::move(snd)).value();

    REQUIRE(len > 0);
    REQUIRE(exec_tid.load() != main_tid);
}

TEST_CASE("server continues_on transfers to work scheduler", "[server][on]") {
    exec::static_thread_pool io_pool{1};
    exec::static_thread_pool work_pool{4};
    auto io_sched = io_pool.get_scheduler();
    auto work_sched = work_pool.get_scheduler();

    std::array<char, 64> buffer{};
    std::atomic<std::thread::id> io_tid{};
    std::atomic<std::thread::id> work_tid{};

    ex::sender auto snd = ex::starts_on(io_sched,
        ex::just(0, buffer.data(), buffer.size())
        | ex::then([&](int, char* buf, size_t sz) {
            io_tid.store(std::this_thread::get_id());
            return legacy_read_from_socket(0, buf, sz);
        })
    )
    | ex::continues_on(work_sched)
    | ex::then([&](size_t sz) {
        work_tid.store(std::this_thread::get_id());
        return sz;
    });

    auto [len] = ex::sync_wait(std::move(snd)).value();

    REQUIRE(len > 0);
    REQUIRE(io_tid.load() != work_tid.load());
}
