cmake_minimum_required(VERSION 3.24)
project(EchoVulkanExperiment VERSION 0.1.0 LANGUAGES C CXX)
if(MSVC AND DEFINED ECHO_MSVC_INCLUDE_PREFIX)
    # The native build script measures the UTF-8 prefix rather than guessing its locale.
    set(CMAKE_CL_SHOWINCLUDES_PREFIX "${ECHO_MSVC_INCLUDE_PREFIX}")
endif()
include(CTest)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
get_filename_component(ECHO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(ECHO_FRAMEWORK_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ECHO_FRAMEWORK_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(ECHO_FRAMEWORK_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
add_subdirectory("${ECHO_ROOT}/framework" framework)
add_subdirectory("${ECHO_ROOT}/framework/rendering" rendering)
add_subdirectory("${ECHO_ROOT}/framework/vfx" vfx)
add_subdirectory("${ECHO_ROOT}/framework/rendering_vulkan" rendering_vulkan)
find_program(ECHO_GLSLANG glslangValidator HINTS "$ENV{VULKAN_SDK}/Bin" REQUIRED)
find_program(ECHO_SPIRV_VAL spirv-val HINTS "$ENV{VULKAN_SDK}/Bin" REQUIRED)
set(shader_outputs)
foreach(stage vert frag)
    set(source "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/scene.${stage}")
    set(output "${CMAKE_CURRENT_BINARY_DIR}/assets/shaders/scene.${stage}.spv")
    add_custom_command(OUTPUT "${output}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/assets/shaders"
        COMMAND "${ECHO_GLSLANG}" -V --target-env vulkan1.1 "${source}" -o "${output}"
        COMMAND "${ECHO_SPIRV_VAL}" --target-env vulkan1.1 "${output}"
        DEPENDS "${source}" VERBATIM)
    list(APPEND shader_outputs "${output}")
endforeach()
add_custom_target(echo_vulkan_shaders DEPENDS ${shader_outputs})
add_executable(echo_vulkan3d src/main.cpp src/png.cpp)
target_include_directories(echo_vulkan3d PRIVATE "${ECHO_ROOT}/third_party")
target_link_libraries(echo_vulkan3d PRIVATE EchoFramework::RenderingVulkan EchoFramework::VFX user32)
add_dependencies(echo_vulkan3d echo_vulkan_shaders)
add_custom_target(echo_vulkan_assets
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/assets"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/assets/scene.json" "${CMAKE_CURRENT_BINARY_DIR}/assets/scene.json"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/assets/reactor_sparks.json" "${CMAKE_CURRENT_BINARY_DIR}/assets/reactor_sparks.json"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${ECHO_ROOT}/assets/sprites/player.png" "${CMAKE_CURRENT_BINARY_DIR}/assets/player.png")
add_dependencies(echo_vulkan3d echo_vulkan_assets)
add_executable(echo_rendering_manifest src/export_manifest.cpp)
target_link_libraries(echo_rendering_manifest PRIVATE EchoFramework::Rendering EchoFramework::VFX)
add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/rendering-manifest.json"
    COMMAND $<TARGET_FILE:echo_rendering_manifest> "${CMAKE_CURRENT_BINARY_DIR}/rendering-manifest.json"
    DEPENDS echo_rendering_manifest VERBATIM)
add_custom_target(echo_rendering_manifest_asset ALL DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/rendering-manifest.json")
if(MSVC)
    target_compile_options(echo_vulkan3d PRIVATE /W4 /permissive-)
endif()
if(BUILD_TESTING)
    add_executable(echo_rendering_tests tests/core_tests.cpp)
    target_link_libraries(echo_rendering_tests PRIVATE EchoFramework::Rendering EchoFramework::VFX)
    add_test(NAME echo_rendering_core COMMAND echo_rendering_tests)
    add_executable(echo_gltf_tests tests/gltf_tests.cpp)
    target_link_libraries(echo_gltf_tests PRIVATE EchoFramework::Rendering)
    add_test(NAME echo_rendering_gltf COMMAND echo_gltf_tests)
    add_test(NAME echo_vulkan_gpu COMMAND echo_vulkan3d --frames 60 --validation --resize-test --probe
        --capture "${CMAKE_CURRENT_BINARY_DIR}/gpu-frame.ppm")
    set_tests_properties(echo_vulkan_gpu PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE)
    set_tests_properties(echo_vulkan_gpu PROPERTIES FIXTURES_SETUP echo_gpu_capture)
    find_program(ECHO_POWERSHELL NAMES pwsh.exe powershell.exe REQUIRED)
    add_test(NAME echo_vulkan_frame
        COMMAND "${ECHO_POWERSHELL}" -NoProfile -ExecutionPolicy Bypass
        -File "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_frame.ps1"
        -ImagePath "${CMAKE_CURRENT_BINARY_DIR}/gpu-frame.ppm")
    set_tests_properties(echo_vulkan_frame PROPERTIES FIXTURES_REQUIRED echo_gpu_capture TIMEOUT 30)
endif()
