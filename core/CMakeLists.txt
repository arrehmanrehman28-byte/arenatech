# Core: project model, commands, settings, logging, autosave.
# No dependency on ai/ or Qt Network here (core must work 100% offline).
add_library(arena_core STATIC
    project.cpp
    command.cpp
    settings.cpp
    logger.cpp
    autosave.cpp
    util.cpp
    subtitles.cpp
)
target_include_directories(arena_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(arena_core PUBLIC Qt6::Core Qt6::Gui)
