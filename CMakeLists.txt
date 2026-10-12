# Arena Edit — root build
# Professional, lightweight, multi-language video editor for Windows 10/11 (64-bit).
# Development also builds on Linux (Qt 6) for CI verification of platform-neutral code.
cmake_minimum_required(VERSION 3.21)
project(ArenaEdit VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

option(ARENA_BUILD_TESTS "Build unit tests" ON)
option(ARENA_ENABLE_FFMPEG "Link real FFmpeg (media phase)" OFF)
option(ARENA_ENABLE_WHISPER "Link whisper.cpp (STT phase)" OFF)
option(ARENA_UPX "Compress final binary with UPX (off by default)" OFF)

if(MSVC)
    add_compile_options(/W4 /permissive- /utf-8)
    add_compile_definitions(UNICODE _UNICODE NOMINMAX)
else()
    add_compile_options(-Wall -Wextra)
endif()

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets Network Concurrent)

set(ARENA_VERSION "${PROJECT_VERSION}")
set(ARENA_BUILD_DATE "${ARENA_BUILD_DATE}" CACHE STRING "Build date stamp")
if(NOT ARENA_BUILD_DATE)
    string(TIMESTAMP ARENA_BUILD_DATE "%Y-%m-%d %H:%M UTC" UTC)
endif()
add_compile_definitions(ARENA_VERSION="${ARENA_VERSION}" ARENA_BUILD_DATE="${ARENA_BUILD_DATE}")

add_subdirectory(core)
add_subdirectory(i18n)
add_subdirectory(media)
add_subdirectory(render)
add_subdirectory(stt)
add_subdirectory(ai)
add_subdirectory(app)

if(ARENA_BUILD_TESTS)
    enable_testing()
    find_package(GTest REQUIRED)
    add_subdirectory(tests)
endif()
