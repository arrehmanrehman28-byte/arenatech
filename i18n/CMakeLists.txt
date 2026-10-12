# i18n: JSON translation files, drop-in languages, immediate switching.
# Links arena_core for logging only (no dependency cycle: core never uses i18n).
add_library(arena_i18n STATIC i18n.cpp)
target_include_directories(arena_i18n PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(arena_i18n PUBLIC Qt6::Core arena_core)
