cmake_minimum_required(VERSION 3.15)
project(rcn C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(SOURCES
    rcn.c
    rcn_daemon.c
    rcn_epoll.c
    rcn_relay.c
    rcn_util.c
    rcn_arg.c
    rcn_device.c
    rcn_peer.c
    rcn_stream.c
)

add_executable(rcn ${SOURCES})
target_include_directories(rcn PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

if (WIN32)
    target_link_libraries(rcn PRIVATE ws2_32 user32)
    if (MSVC)
        target_compile_options(rcn PRIVATE /W4 /utf-8 /D_CRT_SECURE_NO_WARNINGS)
    endif()
else()
    target_compile_options(rcn PRIVATE -Wall -Wextra -g)
endif()
