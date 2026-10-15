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
        if(WIN32 AND DEFINED ARENA_FFMPEG_DIR)
            # Windows release path (D-044): a FFmpeg dev prefix provided by
            # the release pipeline (vcpkg port or ffmpeg.org build). Layout:
            #   <dir>/include/libav*  <dir>/lib/*.lib  <dir>/bin/*.dll
            # The DLLs are staged into the portable payload by the
            # packaging step (ci.yml windows job copies bin/*.dll).
            set(AV_INCLUDE_DIRS "${ARENA_FFMPEG_DIR}/include")
            set(AV_LIBRARIES "")
            foreach(_c IN ITEMS avformat avcodec avutil swscale swresample)
                set(_lib "${ARENA_FFMPEG_DIR}/lib/${_c}.lib")
                if(NOT EXISTS "${_lib}")
                    message(FATAL_ERROR "ARENA_FFMPEG_DIR: missing ${_lib}")
                endif()
                if(NOT TARGET ${_c})
                    add_library(${_c} SHARED IMPORTED)
                    # MSVC shared layout: lib/<name>.lib is the import
                    # library (IMPORTED_IMPLIB); the runtime DLL (bin/) is
                    # resolved at runtime via PATH / the staged payload.
                    set_target_properties(${_c} PROPERTIES
                        IMPORTED_IMPLIB "${_lib}")
                endif()
                list(APPEND AV_LIBRARIES ${_c})
            endforeach()
            if(NOT TARGET PkgConfig::AV)
                add_library(PkgConfig::AV INTERFACE IMPORTED)
                set_target_properties(PkgConfig::AV PROPERTIES
                    INTERFACE_INCLUDE_DIRECTORIES "${AV_INCLUDE_DIRS}"
                    INTERFACE_LINK_LIBRARIES "${AV_LIBRARIES}")
            endif()
            message(STATUS "Arena Edit: FFmpeg from ${ARENA_FFMPEG_DIR} - media pipeline enabled")
        else()
            find_package(PkgConfig REQUIRED)
            pkg_check_modules(AV REQUIRED IMPORTED_TARGET libavformat libavcodec libavutil
                              libswscale libswresample)
            message(STATUS "Arena Edit: FFmpeg ${AV_VERSION} (explicitly enabled)")
        endif()
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

# --- Windows runtime DLL staging -------------------------------------------------
# vcpkg's automatic post-build copy misses transitive dependencies
# (Qt6Widgets, libssl, ...) and build-time test discovery
# (gtest_discover_tests) cannot rely on PATH. Stage the complete vcpkg
# triplet bin/*.dll set next to every executable so each exe is
# self-contained in the build tree (also simplifies portable packaging).
if(WIN32 AND TARGET Qt6::Core)
    get_target_property(_arena_qt_core_loc Qt6::Core IMPORTED_LOCATION_RELEASE)
    if(NOT _arena_qt_core_loc)
        get_target_property(_arena_qt_core_loc Qt6::Core IMPORTED_LOCATION)
    endif()
    if(_arena_qt_core_loc)
        get_filename_component(_arena_vcpkg_bin "${_arena_qt_core_loc}" DIRECTORY)
        file(GLOB _arena_runtime_dlls "${_arena_vcpkg_bin}/*.dll")
        function(arena_stage_runtime_dlls target)
            foreach(_arena_dll IN LISTS _arena_runtime_dlls)
                add_custom_command(TARGET ${target} POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                            "${_arena_dll}" "$<TARGET_FILE_DIR:${target}>"
                    VERBATIM)
            endforeach()
        endfunction()
        foreach(_arena_exe arenaedit arena_ai_live arena_stt_check
                            arena_export_bench arena_tests arena_guismoke)
            if(TARGET ${_arena_exe})
                arena_stage_runtime_dlls(${_arena_exe})
            endif()
        endforeach()
    endif()
endif()
