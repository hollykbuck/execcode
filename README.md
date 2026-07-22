# exec code samples

[![CI Build](https://github.com/hollykbuck/execcode/actions/workflows/ci.yml/badge.svg)](https://github.com/hollykbuck/execcode/actions/workflows/ci.yml)

This is the sample code repo for tutorial
https://hollykbuck.github.io/exec/

Recommended local developement setup:

```toml
[tasks.install]
run = "conan install . --build=missing -c=\"tools.cmake.cmaketoolchain:generator=Ninja\" -pr default"

[tasks.configure]
run = "cmake --preset conan-release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON"

[tasks.build]
run = "cmake --build --preset conan-release"
```

## Code Reading Guide

Start with `test_basics.cpp` for the basic sender pipeline pattern, then follow your interest:

| Order | Test File | Core Topic |
|-------|-----------|-----------|
| 1 | `test_basics.cpp` | just / then / sync_wait — getting started |
| 2 | `test_thread_pool.cpp` | static_thread_pool / schedule |
| 3 | `test_pipe.cpp` | pipe operator `operator\|` |
| 4 | `test_coroutine.cpp` | task / co_await / co_return |
| 5 | `test_receiver.cpp` | receiver concept |
| 6 | `test_then_upon.cpp` | then / upon_error / upon_stopped |
| 7 | `test_let_value.cpp` | let_value dynamic branching |
| 8 | `test_starts_on.cpp` | starts_on / continues_on context switching |
| 9 | `test_when_all.cpp` | when_all concurrent waiting |
| 10 | `test_bulk.cpp` | bulk parallel iteration |
| 11 | `test_into_variant.cpp` | into_variant type erasure |
| 12 | `test_spawn_scope.cpp` | spawn / structured concurrency |
| 13 | `test_write_env.cpp` | write_env |
| 14 | `test_stop_token.cpp` | stop tokens / cancellation |
| 15 | `test_run_loop.cpp` | run_loop manual event loop |
| 16 | `test_parallel_scheduler.cpp` | parallel scheduler |
| 17 | `test_completion_signatures.cpp` | completion_signatures metaprogramming |
| 18 | `test_env_query.cpp` | query / env system |
| 19 | `test_domain.cpp` | domain mechanism |
| 20 | `test_cpo.cpp` | tag_invoke custom CPO |
| — | **Implementations** | |
| 21 | `test_impl_then.cpp` | implementing then from scratch |
| 22 | `test_impl_retry.cpp` | implementing retry from scratch |
| — | **Advanced** | |
| 23 | `test_intro_read.cpp` | custom async read sender |
| 24 | `test_intro_scan.cpp` | composed async scan |
| 25 | `test_intro_socket.cpp` | cancellable Windows socket (Windows only) |
| — | **Custom Scheduler** | |
| 26 | `test_custom_scheduler.cpp` | custom scheduler implementation |
| — | **Platform-specific** | |
| 27 | `test_windows_thread_pool.cpp` | Windows thread pool (Windows only) |
| 28 | `test_io_uring_context.cpp` | Linux io_uring (Linux only) |
| — | **Application Integration** | |
| 29 | `test_beast_server.cpp` | Boost.Beast HTTP server |
| 30 | `test_server_let.cpp` / `test_server_on.cpp` | server integration patterns |
| — | **Appendix: libunifex Ports** | |
| 31 | `test_async_mutex.cpp` | async_mutex v1 |
| 32 | `test_async_mutex_v2.cpp` | async_mutex v2 (with cancellation) |
| 33 | `test_async_pass.cpp` | async_pass synchronous channel |
| 34 | `test_async_manual_reset_event.cpp` | async_manual_reset_event |

## License

This project is licensed under the [GNU General Public License v3.0](LICENSE).

