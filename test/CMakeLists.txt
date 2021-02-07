cmake_minimum_required(VERSION 2.8)

add_library(ppq SHARED ppq.c)
target_link_libraries(ppq xtm_api_static pthread)
