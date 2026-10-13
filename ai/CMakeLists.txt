# AI: Kilo Gateway client (OpenAI-compatible), prompts, action validator, key store.
add_library(arena_ai STATIC
    kilogateway.cpp
    actionvalidator.cpp
    aifeatures.cpp
    keystore.cpp
)
target_include_directories(arena_ai PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(arena_ai PUBLIC Qt6::Core Qt6::Network arena_core)
