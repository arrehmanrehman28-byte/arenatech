# Media: FFmpeg wrapper boundary. Phase M0 ships the interfaces + file-level
# probing; the real FFmpeg (LGPL, dynamic) integration lands in M1 behind
# ARENA_ENABLE_FFMPEG so the skeleton builds without the dependency.
add_library(arena_media STATIC media.cpp)
target_include_directories(arena_media PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(arena_media PUBLIC Qt6::Core arena_core)
