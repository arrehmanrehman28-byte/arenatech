# Media boundary. M0: file-level probing stubs. M1: FFmpeg (LGPL, dynamic)
# probe/decode/waveform/thumbnail/proxy pipeline behind ARENA_HAS_FFMPEG.
add_library(arena_media STATIC
    media.cpp
    ffmpeg_media.cpp
)
target_include_directories(arena_media PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(arena_media PUBLIC Qt6::Core Qt6::Gui arena_core)

if(ARENA_ENABLE_FFMPEG)
    target_link_libraries(arena_media PUBLIC PkgConfig::AV)
    target_compile_definitions(arena_media PUBLIC ARENA_HAS_FFMPEG=1)
endif()
