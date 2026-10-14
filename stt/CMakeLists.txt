# STT: whisper.cpp integration boundary. Local, on-demand models
# (tiny/base/small), word-level timestamps, CPU by default, optional
# CUDA/Vulkan. With ARENA_ENABLE_WHISPER the arena_stt library binds the
# vendored whisper.cpp (third_party/whisper); without it Transcriber
# reports availability honestly and refuses work.
add_library(arena_stt STATIC whisper.cpp)
target_include_directories(arena_stt PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(arena_stt PUBLIC Qt6::Core Qt6::Network arena_core arena_media)

if(ARENA_ENABLE_WHISPER)
    target_link_libraries(arena_stt PUBLIC whisper)
    target_compile_definitions(arena_stt PUBLIC ARENA_ENABLE_WHISPER)
endif()
