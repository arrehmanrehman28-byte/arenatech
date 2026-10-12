# STT: whisper.cpp integration boundary. Local, on-demand models
# (tiny/base/small), word-level timestamps, CPU by default, optional
# CUDA/Vulkan. The real binding lands in M7 behind ARENA_ENABLE_WHISPER;
# M0 ships the interface and an honest availability check.
add_library(arena_stt STATIC whisper.cpp)
target_include_directories(arena_stt PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(arena_stt PUBLIC Qt6::Core arena_core)
