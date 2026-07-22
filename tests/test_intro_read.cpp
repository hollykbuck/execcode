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
#include <stdexec/execution.hpp>
#include <memory>
#include <vector>
#include <cassert>
#include <span>
#include <cstring>

namespace ex = stdexec;

// P2300R9 §1.3.3 — Asynchronous dynamically-sized read
// https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2022/p2300r9.html#example-async-dynamically-sized-read

// A mock I/O handle that holds data to be "read"
struct mock_io_handle {
    std::vector<std::byte> data;
    std::size_t read_offset{0};
};

// A sender that reads from a mock_io_handle into a span<byte>
struct mock_read_sender {
    using sender_concept = ex::sender_t;

    mock_io_handle* handle_;
    std::span<std::byte> buffer_;

    template <class Self, class... Env>
    static consteval auto get_completion_signatures() -> ex::completion_signatures<
        ex::set_value_t(std::size_t),
        ex::set_error_t(std::exception_ptr)>
    {
        return {};
    }

    template <class Receiver>
    struct operation {
        mock_io_handle* handle_;
        std::span<std::byte> buffer_;
        Receiver receiver_;

        void start() & noexcept {
            std::size_t bytes_to_read =
                std::min(buffer_.size(), handle_->data.size() - handle_->read_offset);
            if (bytes_to_read > 0) {
                std::memcpy(buffer_.data(), handle_->data.data() + handle_->read_offset, bytes_to_read);
                handle_->read_offset += bytes_to_read;
            }
            ex::set_value(std::move(receiver_), bytes_to_read);
        }
    };

    template <ex::receiver R>
    auto connect(R r) -> operation<R> {
        return operation<R>{handle_, buffer_, std::move(r)};
    }
};

// async_read(handle) returns a pipeable sender adaptor closure
auto async_read(mock_io_handle* handle) {
    return ex::let_value([handle](std::span<std::byte> buf) -> ex::sender auto {
        return mock_read_sender{handle, buf};
    });
}

// The dynamic_buffer struct from the spec
struct dynamic_buffer {
    std::unique_ptr<std::byte[]> data;
    std::size_t size;
};

// async_read_array from the spec — faithfully reproduced
auto async_read_array(mock_io_handle* handle) -> ex::sender auto {
    return ex::just(dynamic_buffer{})
         | ex::let_value([handle](dynamic_buffer& buf) {
             return ex::just(std::as_writable_bytes(std::span(&buf.size, 1)))
                  | async_read(handle)
                  | ex::then([&buf](std::size_t bytes_read) {
                        assert(bytes_read == sizeof(buf.size));
                        buf.data = std::make_unique<std::byte[]>(buf.size);
                        return std::span(buf.data.get(), buf.size);
                    })
                  | async_read(handle)
                  | ex::then([&buf](std::size_t bytes_read) {
                        assert(bytes_read == buf.size);
                        return std::move(buf);
                    });
         });
}

TEST_CASE("async_read_array reads size-prefixed data", "[intro][read]") {
    std::size_t payload_size = 4;
    std::string_view payload = "woot";

    mock_io_handle handle;
    auto size_bytes = std::as_writable_bytes(std::span(&payload_size, 1));
    handle.data.insert(handle.data.end(), size_bytes.begin(), size_bytes.end());
    auto payload_bytes = std::as_bytes(std::span{payload.data(), payload.size()});
    handle.data.insert(handle.data.end(), payload_bytes.begin(), payload_bytes.end());

    auto [buf] = ex::sync_wait(async_read_array(&handle)).value();

    CHECK(buf.size == payload_size);
    CHECK(std::memcmp(buf.data.get(), payload.data(), payload_size) == 0);
}

TEST_CASE("async_read_array returns empty buffer for zero size", "[intro][read]") {
    std::size_t payload_size = 0;

    mock_io_handle handle;
    auto size_bytes = std::as_writable_bytes(std::span(&payload_size, 1));
    handle.data.insert(handle.data.end(), size_bytes.begin(), size_bytes.end());

    auto [buf] = ex::sync_wait(async_read_array(&handle)).value();

    CHECK(buf.size == 0);
    CHECK(buf.data != nullptr);
}
