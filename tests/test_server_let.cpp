#include <catch2/catch_test_macros.hpp>
#include <stdexec/execution.hpp>
#include <exec/static_thread_pool.hpp>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace ex = stdexec;

// P2300R9 §1.5 "Examples: Server theme" — Composability with execution::let_*
// https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2022/p2300r9.html#example-server-let

namespace {

struct http_request {
    std::string url;
    std::vector<std::pair<std::string, std::string>> headers;
    std::string body;
};

struct http_response {
    int status_code;
    std::string body;
};

auto schedule_request_start(int idx) -> ex::sender auto {
    std::string url = "/query?image_idx=" + std::to_string(idx);
    if (idx < 0)
        url.clear();
    return ex::just(http_request{std::move(url), {}, {}});
}

auto validate_request(const http_request& req) -> ex::sender auto {
    INFO("validating request " << req.url);
    if (req.url.empty())
        throw std::invalid_argument("No URL");
    return ex::just(req);
}

auto handle_request(const http_request& req) -> ex::sender auto {
    return ex::just(http_response{200, "OK: " + req.url});
}

auto error_to_response(std::exception_ptr err) -> ex::sender auto {
    try {
        std::rethrow_exception(err);
    } catch (const std::invalid_argument& e) {
        return ex::just(http_response{404, e.what()});
    } catch (const std::exception& e) {
        return ex::just(http_response{500, e.what()});
    } catch (...) {
        return ex::just(http_response{500, "Unknown"});
    }
}

auto stopped_to_response() -> ex::sender auto {
    return ex::just(http_response{503, "Service unavailable"});
}

} // anonymous namespace

TEST_CASE("server let_* pipeline succeeds for valid request", "[server][let]") {
    bool response_sent = false;

    auto snd =
        schedule_request_start(42)
        | ex::let_value([](const http_request& req) { return validate_request(req); })
        | ex::let_value([](const http_request& req) { return handle_request(req); })
        | ex::let_error([](std::exception_ptr e) { return error_to_response(e); })
        | ex::let_stopped([] { return stopped_to_response(); })
        | ex::let_value([&](const http_response& resp) {
            CHECK(resp.status_code == 200);
            response_sent = true;
            return ex::just();
        });

    auto opt = ex::sync_wait(std::move(snd));
    REQUIRE(opt.has_value());
    REQUIRE(response_sent);
}

TEST_CASE("server let_* pipeline catches error and converts to 404", "[server][let]") {
    bool error_response_sent = false;

    auto snd =
        schedule_request_start(-1) // empty URL → invalid_argument
        | ex::let_value([](const http_request& req) { return validate_request(req); })
        | ex::let_value([](const http_request& req) { return handle_request(req); })
        | ex::let_error([&](std::exception_ptr e) {
            error_response_sent = true;
            return error_to_response(e);
        })
        | ex::let_stopped([] { return stopped_to_response(); })
        | ex::let_value([&](const http_response& resp) {
            CHECK(resp.status_code == 404);
            return ex::just();
        });

    auto opt = ex::sync_wait(std::move(snd));
    REQUIRE(opt.has_value());
    REQUIRE(error_response_sent);
}

TEST_CASE("server let_* pipeline handles cancellation via let_stopped", "[server][let]") {
    bool stopped_response_sent = false;

    auto snd =
        ex::just_stopped()
        | ex::let_error([](std::exception_ptr e) { return error_to_response(e); })
        | ex::let_stopped([&]() -> ex::sender auto {
            stopped_response_sent = true;
            return ex::just(http_response{503, "cancelled"});
        })
        | ex::let_value([&](const http_response& resp) {
            CHECK(resp.status_code == 503);
            return ex::just();
        });

    auto opt = ex::sync_wait(std::move(snd));
    REQUIRE(opt.has_value());
    REQUIRE(stopped_response_sent);
}

TEST_CASE("server let_* pipeline with thread pool", "[server][let][thread_pool]") {
    exec::static_thread_pool pool{4};
    auto sch = pool.get_scheduler();

    bool response_sent = false;

    auto snd =
        ex::starts_on(sch,
            schedule_request_start(7)
            | ex::let_value([](const http_request& req) { return validate_request(req); })
            | ex::let_value([](const http_request& req) { return handle_request(req); })
            | ex::let_error([](std::exception_ptr e) { return error_to_response(e); })
            | ex::let_stopped([] { return stopped_to_response(); })
            | ex::let_value([&](const http_response& resp) {
                CHECK(resp.status_code == 200);
                response_sent = true;
                return ex::just();
            }));

    auto opt = ex::sync_wait(std::move(snd));
    REQUIRE(opt.has_value());
    REQUIRE(response_sent);
}
