cmake_minimum_required(VERSION 3.24)
project(ProjectEcho VERSION 0.1.0 LANGUAGES C CXX)

include(FetchContent)

set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    SDL2
    GIT_REPOSITORY https://github.com/ps5-payload-dev/SDL.git
    GIT_TAG ee4c47dc0d617b3bc8f35108f9956baf228a1322
    GIT_SHALLOW FALSE
)
FetchContent_MakeAvailable(SDL2)

add_library(echo_game
    src/game/game.cpp
)
target_include_directories(echo_game PUBLIC src)
target_compile_features(echo_game PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(echo_game PRIVATE /W4 /permissive-)
else()
    target_compile_options(echo_game PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(project_echo
    src/platform/sdl/main.cpp
    src/platform/sdl/renderer.cpp
)
target_compile_features(project_echo PRIVATE cxx_std_20)
target_compile_definitions(project_echo PRIVATE SDL_MAIN_HANDLED)
target_link_libraries(project_echo PRIVATE echo_game SDL2-static)

if(MSVC)
    target_compile_options(project_echo PRIVATE /W4 /permissive-)
else()
    target_compile_options(project_echo PRIVATE -Wall -Wextra -Wpedantic)
endif()

enable_testing()
add_executable(echo_tests tests/game_tests.cpp)
target_link_libraries(echo_tests PRIVATE echo_game)
target_compile_features(echo_tests PRIVATE cxx_std_20)
add_test(NAME echo_gameplay COMMAND echo_tests)

add_executable(echo_sdl_tests
    tests/sdl_renderer_tests.cpp
    src/platform/sdl/renderer.cpp
)
target_compile_features(echo_sdl_tests PRIVATE cxx_std_20)
target_compile_definitions(echo_sdl_tests PRIVATE SDL_MAIN_HANDLED)
target_link_libraries(echo_sdl_tests PRIVATE echo_game SDL2-static)
if(MSVC)
    target_compile_options(echo_sdl_tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(echo_sdl_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_test(NAME echo_control_prompts COMMAND echo_sdl_tests)
