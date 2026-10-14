# Arena Edit — root build
# Professional, lightweight, multi-language video editor for Windows 10/11 (64-bit).
# Development also builds on Linux (Qt 6) for CI verification of platform-neutral code.
cmake_minimum_required(VERSION 3.21)
# C is required by the vendored whisper.cpp/ggml (M7, .c sources).
project(ArenaEdit VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

option(ARENA_BUILD_TESTS "Build unit tests" ON)
option(ARENA_ENABLE_WHISPER "Link whisper.cpp (STT phase)" OFF)
option(ARENA_UPX "Compress final binary with UPX (off by default)" OFF)

if(MSVC)
    add_compile_options(/W4 /permissive- /utf-8)
    add_compile_definitions(UNICODE _UNICODE NOMINMAX)
else()
    add_compile_options(-Wall -Wextra)
endif()

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets Network Concurrent)
# Audio output for M1 playback (WASAPI on Windows, ALSA on Linux).
find_package(Qt6 6.5 QUIET COMPONENTS Multimedia)

# FFmpeg (LGPL components only: avformat/avcodec/avutil/swscale/swresample).
# -DARENA_ENABLE_FFMPEG=OFF/ON is honored; otherwise auto-detected via
# pkg-config (Linux/CI) or imported targets from the package manager (Windows).
if(NOT DEFINED ARENA_ENABLE_FFMPEG)
    set(ARENA_ENABLE_FFMPEG OFF)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(AV QUIET IMPORTED_TARGET libavformat libavcodec libavutil
                          libswscale libswresample)
    endif()
    if(AV_FOUND)
        set(ARENA_ENABLE_FFMPEG ON)
        message(STATUS "Arena Edit: FFmpeg ${AV_VERSION} auto-detected - media pipeline enabled")
    else()
        message(STATUS "Arena Edit: FFmpeg not found - building media stubs (M1 features disabled)")
    endif()
    set(ARENA_ENABLE_FFMPEG ${ARENA_ENABLE_FFMPEG} CACHE BOOL
        "Link real FFmpeg (media pipeline)" FORCE)
else()
    if(ARENA_ENABLE_FFMPEG)
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(AV REQUIRED IMPORTED_TARGET libavformat libavcodec libavutil
                          libswscale libswresample)
        message(STATUS "Arena Edit: FFmpeg ${AV_VERSION} (explicitly enabled)")
    else()
        message(STATUS "Arena Edit: FFmpeg explicitly disabled - building media stubs")
    endif()
endif()

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
add_subdirectory(tools)

# whisper.cpp (M7 STT) — vendored at third_party/whisper (MIT).
# Added LAST so the warning suppression below cannot leak into arena
# targets (already created). Zero-warning policy applies to arena sources;
# the vendored tree keeps its own history of warning states.
if(ARENA_ENABLE_WHISPER)
    set(WHISPER_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(WHISPER_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(WHISPER_BUILD_SERVER OFF CACHE BOOL "" FORCE)
    set(WHISPER_ALL_WARNINGS OFF CACHE BOOL "" FORCE)
    set(BUILD_SHARED_LIBS OFF) # whisper defaults to ON on Linux
    if(NOT MSVC)
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -w")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -w")
    endif()
    add_subdirectory(third_party/whisper EXCLUDE_FROM_ALL)
    if(NOT MSVC)
        string(REPLACE " -w" "" CMAKE_C_FLAGS "${CMAKE_C_FLAGS}")
        string(REPLACE " -w" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
    endif()
    message(STATUS "Arena Edit: whisper.cpp v${WHISPER_VERSION} (CPU) — STT enabled")
endif()

if(ARENA_BUILD_TESTS)
    enable_testing()
    find_package(GTest REQUIRED)
    add_subdirectory(tests)
endif()
