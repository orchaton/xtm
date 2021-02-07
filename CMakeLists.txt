cmake_minimum_required(VERSION 3.0)

#add_subdirectory(perf)
add_subdirectory(test)

set(xtm_api_sources
    xtm_api.cc
)

add_library(xtm_api_shared SHARED ${xtm_api_sources})
add_library(xtm_api_static STATIC ${xtm_api_sources})
set_property(TARGET xtm_api_static PROPERTY POSITION_INDEPENDENT_CODE ON)
