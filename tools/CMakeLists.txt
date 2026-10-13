# Dev tools (not shipped).
add_executable(arena_ai_live aifeatures_live.cpp)
target_link_libraries(arena_ai_live PRIVATE arena_ai arena_core)
