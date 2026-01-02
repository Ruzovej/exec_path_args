cmake_minimum_required(VERSION 3.16)

add_library(
    ips
        STATIC
            "${CMAKE_CURRENT_LIST_DIR}/src/ips.cxx"
)
target_include_directories(
    ips
        PUBLIC
            "${CMAKE_CURRENT_LIST_DIR}/include"
)
