cmake_minimum_required(VERSION 2.8)

set(perf_test_sources
    perf_test.c
)

add_executable(perf_test ${perf_test_sources})
target_link_libraries(perf_test xtm_api_static pthread)
