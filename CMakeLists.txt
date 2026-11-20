cmake_minimum_required(VERSION 3.24)
project(ProjectEcho VERSION 0.1.0 LANGUAGES C CXX)

include(FetchContent)
include(CTest)

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

option(ECHO_FRAMEWORK_BUILD_TESTS "Build headless Echo Framework tests" ${BUILD_TESTING})
option(ECHO_FRAMEWORK_BUILD_EXAMPLE "Build the headless core demonstration" ${BUILD_TESTING})
add_subdirectory(framework)

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

add_library(echo_assets STATIC
    src/platform/sdl/assets.cpp
    src/platform/sdl/stb_image_impl.c
)
target_include_directories(echo_assets PUBLIC src PRIVATE third_party)
target_compile_features(echo_assets PUBLIC cxx_std_20)
target_link_libraries(echo_assets PUBLIC SDL2-static)

add_executable(project_echo
    src/platform/sdl/main.cpp
    src/platform/sdl/framework_integration.cpp
    src/platform/sdl/renderer.cpp
    src/input/dualsense_diagnostics.cpp
)
target_compile_features(project_echo PRIVATE cxx_std_20)
target_compile_definitions(project_echo PRIVATE SDL_MAIN_HANDLED)
target_link_libraries(project_echo PRIVATE echo_game echo_assets SDL2-static
    EchoFramework::Core EchoFramework::Data)
# Runtime assets are loaded from <executable dir>/assets.
add_custom_command(TARGET project_echo POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:project_echo>/assets")

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
    src/input/dualsense_diagnostics.cpp
)
target_compile_features(echo_sdl_tests PRIVATE cxx_std_20)
target_compile_definitions(echo_sdl_tests PRIVATE SDL_MAIN_HANDLED
    ECHO_SOURCE_ASSETS="${CMAKE_CURRENT_SOURCE_DIR}/assets/")
target_link_libraries(echo_sdl_tests PRIVATE echo_game echo_assets SDL2-static)
if(MSVC)
    target_compile_options(echo_sdl_tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(echo_sdl_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_test(NAME echo_control_prompts COMMAND echo_sdl_tests)

add_executable(echo_framework_integration_tests
    tests/framework_integration_tests.cpp
    src/platform/sdl/framework_integration.cpp
)
target_include_directories(echo_framework_integration_tests PRIVATE src)
target_compile_features(echo_framework_integration_tests PRIVATE cxx_std_20)
target_compile_definitions(echo_framework_integration_tests PRIVATE SDL_MAIN_HANDLED
    ECHO_SOURCE_ASSETS="${CMAKE_CURRENT_SOURCE_DIR}/assets/")
target_link_libraries(echo_framework_integration_tests PRIVATE
    SDL2-static EchoFramework::Data)
if(MSVC)
    target_compile_options(echo_framework_integration_tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(echo_framework_integration_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_test(NAME echo_framework_game_integration COMMAND echo_framework_integration_tests)

add_executable(echo_dualsense_tests
    tests/dualsense_diagnostics_tests.cpp
    src/input/dualsense_diagnostics.cpp
)
target_include_directories(echo_dualsense_tests PRIVATE src)
target_compile_features(echo_dualsense_tests PRIVATE cxx_std_20)
target_compile_definitions(echo_dualsense_tests PRIVATE SDL_MAIN_HANDLED)
target_link_libraries(echo_dualsense_tests PRIVATE SDL2-static)
if(MSVC)
    target_compile_options(echo_dualsense_tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(echo_dualsense_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_test(NAME echo_dualsense_diagnostics COMMAND echo_dualsense_tests)
