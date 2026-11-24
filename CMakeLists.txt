cmake_minimum_required(VERSION 3.24)
project(ProjectEcho VERSION 0.1.0 LANGUAGES C CXX)

if(MSVC AND DEFINED ECHO_MSVC_INCLUDE_PREFIX)
    set(CMAKE_CL_SHOWINCLUDES_PREFIX "${ECHO_MSVC_INCLUDE_PREFIX}")
endif()

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
add_subdirectory(framework/rendering)
add_subdirectory(framework/vfx)

set(ECHO_GRAPHICS_BACKEND "Vulkan" CACHE STRING "Production graphics backend")
set_property(CACHE ECHO_GRAPHICS_BACKEND PROPERTY STRINGS Vulkan SDL)
if(NOT ECHO_GRAPHICS_BACKEND STREQUAL "Vulkan" AND NOT ECHO_GRAPHICS_BACKEND STREQUAL "SDL")
    message(FATAL_ERROR "ECHO_GRAPHICS_BACKEND must be Vulkan or SDL.")
endif()

if(WIN32 AND ECHO_GRAPHICS_BACKEND STREQUAL "Vulkan")
    include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/webp.cmake")
    add_subdirectory(framework/rendering_vulkan)
endif()

if(NOT TARGET EchoFramework::Narrative)
    message(FATAL_ERROR "PROJECT ECHO requires ECHO_FRAMEWORK_ENABLE_NARRATIVE=ON.")
endif()
if(NOT TARGET EchoFramework::Power)
    message(FATAL_ERROR "PROJECT ECHO requires ECHO_FRAMEWORK_ENABLE_POWER=ON.")
endif()

add_library(echo_game
    src/game/game.cpp
    src/game/world_layout.cpp
    src/game/shmup.cpp
)
target_include_directories(echo_game PUBLIC src)
target_compile_features(echo_game PUBLIC cxx_std_20)
target_link_libraries(echo_game PUBLIC
    EchoFramework::Narrative EchoFramework::Inventory EchoFramework::Attributes
    EchoFramework::Equipment EchoFramework::Abilities EchoFramework::Quests
    EchoFramework::Cutscenes EchoFramework::World EchoFramework::Power)

if(ECHO_FRAMEWORK_BUILD_TOOLS)
    add_executable(echo_project_component_editor framework/tools/component_editor.cpp)
    target_compile_definitions(echo_project_component_editor PRIVATE ECHO_PROJECT_COMPONENTS)
    target_link_libraries(echo_project_component_editor PRIVATE echo_game)
    target_compile_features(echo_project_component_editor PRIVATE cxx_std_20)

    add_executable(echo_project_schema_export framework/tools/schema_export.cpp)
    target_compile_definitions(echo_project_schema_export PRIVATE
        ECHO_PROJECT_COMPONENTS ECHO_MANIFEST_HAS_NARRATIVE
        ECHO_MANIFEST_HAS_INVENTORY ECHO_MANIFEST_HAS_ATTRIBUTES
        ECHO_MANIFEST_HAS_EQUIPMENT ECHO_MANIFEST_HAS_ABILITIES
        ECHO_MANIFEST_HAS_QUESTS ECHO_MANIFEST_HAS_CUTSCENES
        ECHO_MANIFEST_HAS_WORLD ECHO_MANIFEST_HAS_POWER)
    target_link_libraries(echo_project_schema_export PRIVATE
        echo_game EchoFramework::Power)
    target_compile_features(echo_project_schema_export PRIVATE cxx_std_20)

    add_executable(echo_project_content_validate
        framework/tools/project_content_validate.cpp)
    target_link_libraries(echo_project_content_validate PRIVATE
        echo_game EchoFramework::Power)
    target_compile_features(echo_project_content_validate PRIVATE cxx_std_20)

    if(WIN32)
        add_library(echo_editor_model STATIC tools/editor/content_model.cpp tools/editor/map_model.cpp
            tools/editor/windows_storage.cpp)
        target_link_libraries(echo_editor_model PUBLIC echo_game)
        target_compile_features(echo_editor_model PUBLIC cxx_std_20)
        add_executable(echo_editor WIN32 tools/editor/windows_editor.cpp)
        target_compile_definitions(echo_editor PRIVATE UNICODE _UNICODE NOMINMAX)
        target_link_libraries(echo_editor PRIVATE echo_editor_model comdlg32)
        target_compile_features(echo_editor PRIVATE cxx_std_20)
        if(MSVC)
            target_compile_options(echo_editor_model PRIVATE /EHsc)
            target_compile_options(echo_editor PRIVATE /EHsc)
        endif()
        if(ECHO_FRAMEWORK_BUILD_TESTS)
            add_executable(echo_editor_tests tools/editor/content_model_tests.cpp)
            target_link_libraries(echo_editor_tests PRIVATE echo_editor_model)
            target_compile_features(echo_editor_tests PRIVATE cxx_std_20)
            if(MSVC)
                target_compile_options(echo_editor_tests PRIVATE /EHsc)
            endif()
            add_test(NAME echo_editor_content COMMAND echo_editor_tests
                "${CMAKE_CURRENT_SOURCE_DIR}/assets")
        endif()
    endif()

    if(ECHO_FRAMEWORK_BUILD_TESTS)
        set(ECHO_PROJECT_CAPABILITY_MANIFEST
            "${CMAKE_CURRENT_BINARY_DIR}/project-capability-manifest.json")
        add_custom_command(
            OUTPUT "${ECHO_PROJECT_CAPABILITY_MANIFEST}"
            COMMAND $<TARGET_FILE:echo_project_schema_export>
                    "${ECHO_PROJECT_CAPABILITY_MANIFEST}"
            DEPENDS echo_project_schema_export
            VERBATIM
        )
        add_custom_target(echo_project_manifest_fixture
            DEPENDS "${ECHO_PROJECT_CAPABILITY_MANIFEST}")
        add_dependencies(echo_project_content_validate
            echo_project_manifest_fixture)

        find_program(ECHO_POWERSHELL_EXECUTABLE NAMES pwsh.exe powershell.exe)
        if(ECHO_POWERSHELL_EXECUTABLE)
            add_test(NAME echo_project_content_validator
                COMMAND "${ECHO_POWERSHELL_EXECUTABLE}" -NoProfile
                    -ExecutionPolicy Bypass
                    -File "${CMAKE_CURRENT_SOURCE_DIR}/tests/project_content_validator_tests.ps1"
                    -Validator $<TARGET_FILE:echo_project_content_validate>
                    -GameConfig "${CMAKE_CURRENT_SOURCE_DIR}/assets/framework/game.json"
                    -Dialogue "${CMAKE_CURRENT_SOURCE_DIR}/assets/framework/engineer.json"
                        -ItemCatalog "${CMAKE_CURRENT_SOURCE_DIR}/assets/framework/items.json"
                    -Manifest "${ECHO_PROJECT_CAPABILITY_MANIFEST}"
            )
        endif()
    endif()
endif()

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

if(ECHO_GRAPHICS_BACKEND STREQUAL "SDL" OR NOT WIN32)
add_executable(project_echo
    src/platform/sdl/main.cpp
    src/platform/sdl/framework_integration.cpp
    src/platform/sdl/renderer.cpp
    src/platform/presentation/ui_presentation.cpp
    src/input/dualsense_diagnostics.cpp
)
target_compile_features(project_echo PRIVATE cxx_std_20)
target_include_directories(project_echo PRIVATE framework/rendering/include)
target_compile_definitions(project_echo PRIVATE SDL_MAIN_HANDLED)
target_link_libraries(project_echo PRIVATE echo_game echo_assets SDL2-static
    EchoFramework::Core EchoFramework::Data EchoFramework::Power EchoFramework::VFX)
if(MSVC)
    target_compile_options(project_echo PRIVATE /W4 /permissive-)
else()
    target_compile_options(project_echo PRIVATE -Wall -Wextra -Wpedantic)
endif()
endif()

    if(WIN32 AND ECHO_GRAPHICS_BACKEND STREQUAL "Vulkan")
        add_executable(project_echo_vulkan
            src/platform/sdl/main.cpp
            src/platform/sdl/framework_integration.cpp
            src/platform/sdl/renderer.cpp
            src/platform/presentation/ui_presentation.cpp
            src/platform/presentation/canvas_assets.cpp
            src/platform/presentation/gpu_canvas.cpp
            src/platform/presentation/menu_glyph.cpp
            src/platform/sdl/assets.cpp
            src/platform/sdl/stb_image_impl.c
            src/input/dualsense_diagnostics.cpp
        )
        target_compile_features(project_echo_vulkan PRIVATE cxx_std_20)
        target_include_directories(project_echo_vulkan PRIVATE
            src third_party framework/rendering/include framework/rendering_vulkan/include
            "${libwebp_SOURCE_DIR}/src")
        target_compile_definitions(project_echo_vulkan PRIVATE SDL_MAIN_HANDLED ECHO_VULKAN_RENDERER)
        target_link_libraries(project_echo_vulkan PRIVATE
            echo_game SDL2-static webpdecoder user32
            EchoFramework::Core EchoFramework::Data EchoFramework::Power EchoFramework::Narrative
            EchoFramework::Inventory EchoFramework::Attributes EchoFramework::Equipment
            EchoFramework::Abilities EchoFramework::Quests EchoFramework::Cutscenes
            EchoFramework::World EchoFramework::VFX EchoFramework::Rendering
            EchoFramework::RenderingVulkan)
        find_program(ECHO_GLSLANG glslangValidator HINTS "$ENV{VULKAN_SDK}/Bin" REQUIRED)
        find_program(ECHO_SPIRV_VAL spirv-val HINTS "$ENV{VULKAN_SDK}/Bin" REQUIRED)
        set(echo_game_shaders)
        foreach(stage vert frag)
            set(shader_source "${CMAKE_CURRENT_SOURCE_DIR}/experiments/vulkan3d/assets/shaders/scene.${stage}")
            set(shader_output "${CMAKE_CURRENT_BINARY_DIR}/generated/shaders/scene.${stage}.spv")
            add_custom_command(OUTPUT "${shader_output}"
                COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/generated/shaders"
                COMMAND "${ECHO_GLSLANG}" -V --target-env vulkan1.1 "${shader_source}" -o "${shader_output}"
                COMMAND "${ECHO_SPIRV_VAL}" --target-env vulkan1.1 "${shader_output}"
                DEPENDS "${shader_source}" VERBATIM)
            list(APPEND echo_game_shaders "${shader_output}")
        endforeach()
        add_custom_target(echo_game_vulkan_shaders
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:project_echo_vulkan>/assets/shaders"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${echo_game_shaders}
                "$<TARGET_FILE_DIR:project_echo_vulkan>/assets/shaders"
            DEPENDS ${echo_game_shaders} VERBATIM)
        add_dependencies(project_echo_vulkan echo_game_vulkan_shaders)
        if(MSVC)
            target_compile_options(project_echo_vulkan PRIVATE /W4 /permissive-)
        else()
            target_compile_options(project_echo_vulkan PRIVATE -Wall -Wextra -Wpedantic)
        endif()
        set_target_properties(project_echo_vulkan PROPERTIES OUTPUT_NAME "project_echo_vulkan")
    endif()

# Runtime assets are loaded from the selected executable's directory.
if(TARGET project_echo_vulkan)
    set(echo_game_target project_echo_vulkan)
else()
    set(echo_game_target project_echo)
endif()
add_custom_target(echo_runtime_assets
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:${echo_game_target}>/assets")
add_dependencies(${echo_game_target} echo_runtime_assets)

enable_testing()
if(WIN32)
    find_program(ECHO_POWERSHELL_EXECUTABLE NAMES pwsh.exe powershell.exe)
    if(ECHO_POWERSHELL_EXECUTABLE)
        add_test(NAME echo_windows_vulkan_sdk
            COMMAND "${ECHO_POWERSHELL_EXECUTABLE}" -NoProfile
                -ExecutionPolicy Bypass
                -File "${CMAKE_CURRENT_SOURCE_DIR}/tests/windows_vulkan_sdk_tests.ps1")
    endif()
endif()
option(ECHO_VULKAN_GPU_TESTS "Run Vulkan tests that require a Windows GPU and display" OFF)
if(WIN32 AND ECHO_VULKAN_GPU_TESTS AND ECHO_GRAPHICS_BACKEND STREQUAL "Vulkan")
    add_test(NAME echo_vulkan_game_smoke COMMAND project_echo_vulkan --vulkan-smoke-test)
    set_tests_properties(echo_vulkan_game_smoke PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles"
        TIMEOUT 120 RUN_SERIAL TRUE)
endif()
add_executable(echo_tests tests/game_tests.cpp)
target_link_libraries(echo_tests PRIVATE echo_game)
target_include_directories(echo_tests PRIVATE framework/rendering/include)
target_compile_features(echo_tests PRIVATE cxx_std_20)
target_compile_definitions(echo_tests PRIVATE ECHO_SOURCE_ASSETS="${CMAKE_CURRENT_SOURCE_DIR}/assets/")
add_test(NAME echo_gameplay COMMAND echo_tests)

add_executable(echo_gpu_canvas_tests
    tests/gpu_canvas_tests.cpp
    src/platform/presentation/canvas_assets.cpp
    src/platform/presentation/gpu_canvas.cpp
    src/platform/presentation/ui_presentation.cpp
)
target_compile_features(echo_gpu_canvas_tests PRIVATE cxx_std_20)
target_include_directories(echo_gpu_canvas_tests PRIVATE src)
target_compile_definitions(echo_gpu_canvas_tests PRIVATE
    ECHO_SOURCE_ASSETS="${CMAKE_CURRENT_SOURCE_DIR}/assets/")
target_link_libraries(echo_gpu_canvas_tests PRIVATE echo_game EchoFramework::Power
    EchoFramework::Rendering)
add_test(NAME echo_gpu_canvas_lifecycle COMMAND echo_gpu_canvas_tests)

add_executable(echo_sdl_tests
    tests/sdl_renderer_tests.cpp
    src/platform/sdl/framework_integration.cpp
    src/platform/sdl/renderer.cpp
    src/platform/presentation/ui_presentation.cpp
    src/input/dualsense_diagnostics.cpp
)
target_compile_features(echo_sdl_tests PRIVATE cxx_std_20)
target_include_directories(echo_sdl_tests PRIVATE framework/rendering/include)
target_compile_definitions(echo_sdl_tests PRIVATE SDL_MAIN_HANDLED
    ECHO_SOURCE_ASSETS="${CMAKE_CURRENT_SOURCE_DIR}/assets/")
target_link_libraries(echo_sdl_tests PRIVATE echo_game echo_assets SDL2-static
    EchoFramework::Data EchoFramework::Power EchoFramework::VFX)
if(MSVC)
    target_compile_options(echo_sdl_tests PRIVATE /W4 /permissive-)
    # The single test main holds many Game/WorldLayout copies on the stack.
    target_link_options(echo_sdl_tests PRIVATE /STACK:8388608)
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
    echo_game SDL2-static EchoFramework::Data EchoFramework::Power)
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
