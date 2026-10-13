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
    ARENA_TEST_FIXTURES="${CMAKE_SOURCE_DIR}/tests/fixtures"
)
target_include_directories(arena_tests PRIVATE ${CMAKE_SOURCE_DIR}/media)

include(GoogleTest)
gtest_discover_tests(arena_tests)

# Headless GUI smoke: full M1 pipeline through the real MainWindow.
# Runs on the offscreen platform; needs a real video fixture.
if(ARENA_ENABLE_FFMPEG AND EXISTS "${CMAKE_SOURCE_DIR}/tests/fixtures/clip01.mp4")
    add_executable(arena_guismoke guismoke.cpp)
    target_link_libraries(arena_guismoke PRIVATE arena_app_lib)
    add_test(NAME Guismoke.MediaPipeline
             COMMAND arena_guismoke "${CMAKE_SOURCE_DIR}/tests/fixtures/clip01.mp4")
    set_tests_properties(Guismoke.MediaPipeline PROPERTIES TIMEOUT 60)
endif()
