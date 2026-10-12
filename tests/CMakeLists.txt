# Unit tests (GoogleTest). Core must never depend on AI: enforced here by
# build order — these tests link arena_core without arena_ai except for the
# explicit validator test.
add_executable(arena_tests
    test_core.cpp
    test_i18n.cpp
    test_actions.cpp
    test_themer.cpp
    test_media.cpp
)
target_link_libraries(arena_tests
    PRIVATE arena_core arena_i18n arena_media arena_render arena_stt
            arena_app_theme GTest::gtest GTest::gtest_main
)
target_link_libraries(arena_tests PRIVATE arena_ai) # validator under test
target_include_directories(arena_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/app
    ${CMAKE_SOURCE_DIR}/assets
    ${CMAKE_SOURCE_DIR}/stt
    ${CMAKE_SOURCE_DIR}/render
)
target_compile_definitions(arena_tests PRIVATE
    ARENA_I18N_DIR="${CMAKE_SOURCE_DIR}/i18n"
    ARENA_THEME_JSON="${CMAKE_SOURCE_DIR}/assets/theme/theme.json"
)

include(GoogleTest)
gtest_discover_tests(arena_tests)
