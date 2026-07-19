#include <catch2/catch_test_macros.hpp>
#include <stdexec/execution.hpp>
#include <stdexec/stop_token.hpp>

#include <atomic>
#include <optional>
#include <system_error>

namespace ex = stdexec;

// P2300R9 §1.4 — Asynchronous Windows socket recv
// https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2022/p2300r9.html#example-async-windows-socket-recv
//
// This implementation faithfully reproduces the spec's cancellable async_recv()
// operation for Windows sockets, using WSAOVERLAPPED + WSARecv + IOCP.

#ifdef _WIN32

#include <winsock2.h>
#include <windows.h>

struct operation_base : WSAOVERLAPPED {
    using completion_fn = void(operation_base* op, DWORD bytesTransferred, int errorCode) noexcept;

    completion_fn* completed;
};

template <class Receiver>
struct recv_op : operation_base {
    using operation_state_concept = ex::operation_state_t;

    recv_op(SOCKET s, void* data, size_t len, Receiver r)
        : receiver(std::move(r))
        , sock(s) {
        this->Internal = 0;
        this->InternalHigh = 0;
        this->Offset = 0;
        this->OffsetHigh = 0;
        this->hEvent = NULL;
        this->completed = &recv_op::on_complete;
        buffer.len = static_cast<ULONG>(len);
        buffer.buf = static_cast<CHAR*>(data);
    }

    void start() & noexcept {
        auto st = ex::get_stop_token(ex::get_env(receiver));
        if (st.stop_requested()) {
            ex::set_stopped(std::move(receiver));
            return;
        }

        const bool stopPossible = st.stop_possible();
        if (!stopPossible) {
            ready.store(true, std::memory_order_relaxed);
        }

        DWORD bytesTransferred = 0;
        DWORD flags = 0;
        int result = WSARecv(sock, &buffer, 1, &bytesTransferred, &flags,
                             static_cast<WSAOVERLAPPED*>(this), NULL);
        if (result == SOCKET_ERROR) {
            int errorCode = WSAGetLastError();
            if (errorCode != WSA_IO_PENDING) {
                if (errorCode == WSA_OPERATION_ABORTED) {
                    ex::set_stopped(std::move(receiver));
                } else {
                    ex::set_error(std::move(receiver),
                                  std::error_code(errorCode, std::system_category()));
                }
                return;
            }
        } else {
            ex::set_value(std::move(receiver), static_cast<std::size_t>(bytesTransferred));
            return;
        }

        if (stopPossible) {
            stopCallback.emplace(std::move(st), cancel_cb{*this});

            if (ready.load(std::memory_order_acquire) ||
                ready.exchange(true, std::memory_order_acq_rel)) {
                stopCallback.reset();

                BOOL ok = WSAGetOverlappedResult(sock, (WSAOVERLAPPED*)this,
                                                  &bytesTransferred, FALSE, &flags);
                if (ok) {
                    ex::set_value(std::move(receiver), static_cast<std::size_t>(bytesTransferred));
                } else {
                    int errorCode = WSAGetLastError();
                    ex::set_error(std::move(receiver),
                                  std::error_code(errorCode, std::system_category()));
                }
            }
        }
    }

    struct cancel_cb {
        recv_op& op;

        void operator()() noexcept {
            CancelIoEx((HANDLE)op.sock, (OVERLAPPED*)(WSAOVERLAPPED*)&op);
        }
    };

    static void on_complete(operation_base* op, DWORD bytesTransferred, int errorCode) noexcept {
        recv_op& self = *static_cast<recv_op*>(op);

        if (self.ready.load(std::memory_order_acquire) ||
            self.ready.exchange(true, std::memory_order_acq_rel)) {
            self.stopCallback.reset();

            if (errorCode == 0) {
                ex::set_value(std::move(self.receiver), static_cast<std::size_t>(bytesTransferred));
            } else {
                ex::set_error(std::move(self.receiver),
                              std::error_code(errorCode, std::system_category()));
            }
        }
    }

    using stop_callback_t =
        ex::stop_callback_for_t<
            ex::stop_token_of_t<ex::env_of_t<Receiver>>,
            cancel_cb>;

    Receiver receiver;
    SOCKET sock;
    WSABUF buffer;
    std::optional<stop_callback_t> stopCallback;
    std::atomic<bool> ready{false};
};

struct recv_sender {
    using sender_concept = ex::sender_t;

    SOCKET sock;
    void* data;
    size_t len;

    template <class Self, class... Env>
    static consteval auto get_completion_signatures() -> ex::completion_signatures<
        ex::set_value_t(std::size_t),
        ex::set_error_t(std::error_code),
        ex::set_stopped_t()>
    {
        return {};
    }

    template <class Receiver>
    recv_op<Receiver> connect(Receiver r) const {
        return recv_op<Receiver>{sock, data, len, std::move(r)};
    }
};

recv_sender async_recv(SOCKET s, void* data, size_t len) {
    return recv_sender{s, data, len};
}

// ---------------------------------------------------------------------------
// Tests — concept checks only (actual IO requires IOCP event loop)
// ---------------------------------------------------------------------------

TEST_CASE("recv_sender models the sender concept", "[intro][socket][win]") {
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    REQUIRE(s != INVALID_SOCKET);

    char buf[64];
    recv_sender snd{s, buf, sizeof(buf)};
    static_assert(ex::sender<recv_sender>);
    static_assert(ex::sender<decltype(snd)>);

    closesocket(s);
}

TEST_CASE("async_recv returns recv_sender", "[intro][socket][win]") {
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    REQUIRE(s != INVALID_SOCKET);

    char buf[64];
    auto snd = async_recv(s, buf, sizeof(buf));
    static_assert(std::same_as<decltype(snd), recv_sender>);

    closesocket(s);
}

TEST_CASE("recv_op models operation_state", "[intro][socket][win]") {
    WSADATA wsaData;
    int wsaResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    REQUIRE(wsaResult == 0);

    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    REQUIRE(s != INVALID_SOCKET);

    char buf[64];
    recv_sender snd{s, buf, sizeof(buf)};

    // sync_wait wires up the receiver chain; WSARecv fails on unconnected socket
    // triggering the error path and proving the plumbing works
    REQUIRE_THROWS_AS(ex::sync_wait(std::move(snd)), std::system_error);

    closesocket(s);
    WSACleanup();
}

#endif // _WIN32
