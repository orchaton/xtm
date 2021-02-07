cmake_minimum_required(VERSION 2.8)

add_executable(ppq_test ppq.c)
target_link_libraries(ppq_test xtm_api_static pthread)
