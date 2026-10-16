# Dev tools (not shipped).
add_executable(arena_ai_live aifeatures_live.cpp)
target_link_libraries(arena_ai_live PRIVATE arena_ai arena_core)
target_compile_definitions(arena_ai_live PRIVATE
    ARENA_I18N_DIR="${CMAKE_SOURCE_DIR}/i18n")

# Local STT end-to-end check (M7): model download/verify + transcription.
add_executable(arena_stt_check stt_check.cpp)
target_link_libraries(arena_stt_check PRIVATE arena_stt arena_media)

# M9 release benchmark: export encode FPS through the real ExportQueue path.
add_executable(arena_export_bench export_bench.cpp)
target_link_libraries(arena_export_bench PRIVATE arena_render arena_media Qt6::Core Qt6::Gui)

# Windows: stage the complete runtime DLL set next to each tool exe
# (setup in the top-level CMakeLists).
if(COMMAND arena_stage_exes)
    arena_stage_exes(arena_ai_live arena_stt_check arena_export_bench)
endif()
