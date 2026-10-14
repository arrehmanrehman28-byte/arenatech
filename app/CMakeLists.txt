# App: UI shell (window, panels, dialogs, theming, palette).
# Themer lives in its own library so unit tests can link it without the executable.
add_library(arena_app_theme STATIC themer.cpp)
target_include_directories(arena_app_theme PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(arena_app_theme PUBLIC Qt6::Gui Qt6::Widgets arena_core)

# App logic (window, panels, playback) as a static library so the executable
# and the headless GUI smoke test share exactly the same code.
add_library(arena_app_lib STATIC
    mainwindow.cpp
    panels.cpp
    timeline.cpp
    playback.cpp
    aipanel.cpp
    commandpalette.cpp
    dialogs.cpp
    exportdialog.cpp
    tour.cpp
)
target_include_directories(arena_app_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(arena_app_lib
    PUBLIC arena_app_theme arena_core arena_i18n arena_media arena_render arena_stt arena_ai
           Qt6::Widgets Qt6::Network
)
if(Qt6Multimedia_FOUND)
    target_link_libraries(arena_app_lib PUBLIC Qt6::Multimedia)
    target_compile_definitions(arena_app_lib PUBLIC ARENA_HAS_QTAUDIO=1)
endif()
target_compile_definitions(arena_app_lib PUBLIC
    ARENA_I18N_DIR="${CMAKE_SOURCE_DIR}/i18n"
    ARENA_THEME_JSON="${CMAKE_SOURCE_DIR}/assets/theme/theme.json"
    ARENA_ASSETS_DIR="${CMAKE_SOURCE_DIR}/assets"
)

add_executable(arenaedit main.cpp)
target_link_libraries(arenaedit PRIVATE arena_app_lib)

if(WIN32)
    set_target_properties(arenaedit PROPERTIES OUTPUT_NAME "ArenaEdit" WIN32_EXECUTABLE TRUE)
else()
    set_target_properties(arenaedit PROPERTIES OUTPUT_NAME "arenaedit")
endif()
