# Render: compositor boundary. M0 ships the effect registry (the effect
# list is the contract); M3 adds the deterministic software compositor,
# the LRU frame cache (default 512 MB), and golden-frame tests. A GPU path
# (D3D11 on Windows / OpenGL) hooks in behind isGpuAvailable().
add_library(arena_render STATIC render.cpp compositor.cpp)
target_include_directories(arena_render PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(arena_render PUBLIC Qt6::Core Qt6::Gui arena_core)
