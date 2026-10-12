# Render: compositor boundary. M0 ships the effect registry (the effect
# list is the contract); M3 adds the D3D11/OpenGL compositor, the LRU frame
# cache (default 512 MB), and golden-frame tests.
add_library(arena_render STATIC render.cpp)
target_include_directories(arena_render PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(arena_render PUBLIC Qt6::Core arena_core)
