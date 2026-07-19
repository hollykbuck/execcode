cmake_minimum_required(VERSION 3.25)
project(stdexec-tutorials VERSION 1.0.0 LANGUAGES CXX)

find_package(Catch2 REQUIRED)
find_package(spdlog REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(P2300 REQUIRED)

set(STDEXEC_TEST_SOURCES
  tests/main.cpp
  tests/test_basics.cpp
  tests/test_thread_pool.cpp
  tests/test_pipe.cpp
  tests/test_coroutine.cpp
  tests/test_receiver.cpp
  tests/test_completion_signatures.cpp
  tests/test_then_upon.cpp
  tests/test_let_value.cpp
  tests/test_starts_on.cpp
  tests/test_when_all.cpp
  tests/test_bulk.cpp
  tests/test_into_variant.cpp
  tests/test_spawn_scope.cpp
  tests/test_write_env.cpp
  tests/test_stop_token.cpp
  tests/test_run_loop.cpp
  tests/test_server_let.cpp
  tests/test_server_on.cpp
  tests/test_impl_then.cpp
  tests/test_impl_retry.cpp
  tests/test_intro_read.cpp
  tests/test_intro_scan.cpp
  tests/test_intro_socket.cpp
  tests/test_custom_scheduler.cpp
  tests/test_env_query.cpp
  tests/test_domain.cpp
  tests/test_cpo.cpp
  tests/test_parallel_scheduler.cpp
  tests/test_windows_thread_pool.cpp
  tests/test_beast_server.cpp
  tests/test_async_mutex.cpp
  tests/test_async_pass.cpp
  tests/test_async_mutex_v2.cpp
  tests/test_async_manual_reset_event.cpp
)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  list(APPEND STDEXEC_TEST_SOURCES tests/test_io_uring_context.cpp)
endif()

add_executable(stdexec-tests ${STDEXEC_TEST_SOURCES})

target_link_libraries(stdexec-tests
  PRIVATE
    Catch2::Catch2WithMain
    p2300::p2300
    Boost::headers
)

target_link_libraries(stdexec-tests PRIVATE spdlog::spdlog)
target_link_libraries(stdexec-tests PRIVATE nlohmann_json::nlohmann_json)
# MUST enable header only mode to prevent link error, query_parallel_scheduler_backend 
# is a weak symbol
# this is a workaround
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  find_package(TBB QUIET)
  if(NOT TBB_FOUND)
    target_compile_definitions(stdexec-tests PRIVATE STDEXEC_PARALLEL_SCHEDULER_HEADER_ONLY)
  endif()
endif()
target_compile_features(stdexec-tests PRIVATE cxx_std_20)

set(BUILD_TESTING ON CACHE BOOL "" FORCE)
include(CTest)
include(Catch)
catch_discover_tests(stdexec-tests)
