cmake_minimum_required(VERSION 3.24)
project(echo_ps5 LANGUAGES C CXX)
if(NOT PS5 OR NOT CMAKE_HOST_WIN32)
    message(FATAL_ERROR "Use the native Windows PS5 toolchain.")
endif()
option(ECHO_PS5_SAVE_STORAGE_DIAGNOSTICS
    "Run an explicit save-storage probe at startup in the diagnostic build" OFF)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
file(TO_CMAKE_PATH "${BOILERPLATE_SOURCE}" BOILERPLATE_SOURCE)
set(native "${BOILERPLATE_SOURCE}/tooling/native")
add_executable(hello
    "${BOILERPLATE_SOURCE}/src/main.cpp"
    "${BOILERPLATE_SOURCE}/src/demo_renderer.cpp"
    "${native}/app_crt.cpp"
    "${native}/app_cpp_runtime.cpp")
set_target_properties(hello PROPERTIES SUFFIX ".llvm.elf")
target_compile_options(hello PRIVATE -O2 -Wall -Wextra -ffunction-sections -fdata-sections)
target_link_options(hello PRIVATE
    "-T${native}/ps5-pie.ld"
    "--version-script=${native}/app-symbols.map"
    -e _start)
set(stubs)
foreach(module kernel SceLibcInternal SceSystemService SceVideoOut ScePad SceUserService SceAudioOut SceKeyboard SceImeDialog)
    list(APPEND stubs "${PS5_SDK_ROOT}/target/lib/lib${module}.so")
endforeach()
target_link_libraries(hello PRIVATE --as-needed ${stubs})

if(ECHO_PS5_GAME)
    file(TO_CMAKE_PATH "${SDL_SOURCE}" SDL_SOURCE)
    get_filename_component(echo_root "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
    add_subdirectory("${echo_root}/framework" framework)
    if(ECHO_PS5_VULKAN)
        if(NOT ECHO_RADV_LINK_INPUTS OR NOT EXISTS "${ECHO_RADV_LINK_INPUTS}" OR
           NOT ECHO_PS5_NO_KEYBOARD OR ECHO_PS5_STARTUP_DIAGNOSTICS)
            message(FATAL_ERROR "PS5 Vulkan requires verified RADV link inputs and the controller-only non-diagnostic SDL build.")
        endif()
        include("${ECHO_RADV_LINK_INPUTS}")
        add_subdirectory("${echo_root}/framework/rendering" framework_rendering)
        add_subdirectory("${echo_root}/framework/rendering_vulkan" framework_rendering_vulkan)
    endif()
    if(NOT TARGET EchoFramework::Narrative)
        message(FATAL_ERROR "PROJECT ECHO requires ECHO_FRAMEWORK_ENABLE_NARRATIVE=ON.")
    endif()
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_TEST OFF CACHE BOOL "" FORCE)
    set(SDL_TESTS OFF CACHE BOOL "" FORCE)
    set(SDL_OPENGL OFF CACHE BOOL "" FORCE)
    set(SDL_OPENGLES OFF CACHE BOOL "" FORCE)
    set(SDL_VULKAN OFF CACHE BOOL "" FORCE)
    set(SDL_HIDAPI OFF CACHE BOOL "" FORCE)
    set(SDL_LOADSO OFF CACHE BOOL "" FORCE)
    set(SDL_FILESYSTEM OFF CACHE BOOL "" FORCE)
    set(SDL_LIBSAMPLERATE OFF CACHE BOOL "" FORCE)
    # Compile-only feature checks would incorrectly enable unexported BSD APIs.
    set(CMAKE_TRY_COMPILE_TARGET_TYPE EXECUTABLE)
    set(CMAKE_REQUIRED_LINK_OPTIONS -e main)
    set(CMAKE_REQUIRED_LIBRARIES ${stubs})
    # The patched SDL copy has no .git; do not discover the parent repository.
    set(CMAKE_DISABLE_FIND_PACKAGE_Git TRUE)
    add_subdirectory("${SDL_SOURCE}" sdl)
    add_executable(project_echo_ps5
        "${echo_root}/src/game/game.cpp"
        "${echo_root}/src/game/world_layout.cpp"
        "${echo_root}/src/platform/sdl/main.cpp"
        "${echo_root}/src/platform/sdl/framework_integration.cpp"
        "${echo_root}/src/platform/sdl/renderer.cpp"
        "${echo_root}/src/input/dualsense_diagnostics.cpp"
        "${echo_root}/src/platform/sdl/assets.cpp"
        "${echo_root}/src/platform/sdl/stb_image_impl.c"
        "${native}/app_crt.cpp"
        "${native}/app_cpp_runtime.cpp")
    set_target_properties(project_echo_ps5 PROPERTIES SUFFIX ".llvm.elf")
    target_include_directories(project_echo_ps5 PRIVATE
        "${echo_root}/src"
        "${echo_root}/third_party"
        "${echo_root}/framework/core/include"
        "${echo_root}/framework/rendering/include"
        "${echo_root}/framework/inventory/include")
    target_compile_definitions(project_echo_ps5 PRIVATE SDL_MAIN_HANDLED)
    if(ECHO_PS5_STARTUP_DIAGNOSTICS)
        target_compile_definitions(project_echo_ps5 PRIVATE ECHO_PS5_STARTUP_DIAGNOSTICS)
        target_compile_definitions(SDL2-static PRIVATE ECHO_PS5_STARTUP_DIAGNOSTICS)
    endif()
    if(ECHO_PS5_SAVE_STORAGE_DIAGNOSTICS)
        target_compile_definitions(project_echo_ps5 PRIVATE ECHO_PS5_SAVE_STORAGE_DIAGNOSTICS)
    endif()
    if(ECHO_PS5_NO_KEYBOARD)
        if(ECHO_PS5_STARTUP_DIAGNOSTICS)
            target_compile_definitions(SDL2-static PRIVATE ECHO_PS5_DIAGNOSTIC_NO_KEYBOARD)
        else()
            target_compile_definitions(SDL2-static PRIVATE ECHO_PS5_NO_KEYBOARD)
        endif()
    endif()
    target_compile_options(project_echo_ps5 PRIVATE -O2 -Wall -Wextra -ffunction-sections -fdata-sections)
    if(ECHO_PS5_VULKAN)
        target_sources(project_echo_ps5 PRIVATE
            "${echo_root}/src/platform/sdl/canvas_assets.cpp"
            "${echo_root}/src/platform/sdl/gpu_canvas.cpp"
            "${echo_root}/experiments/vulkan3d/ps5/radv/compat.c")
        target_compile_definitions(project_echo_ps5 PRIVATE ECHO_PS5_VULKAN)
        target_link_options(project_echo_ps5 PRIVATE
            "-T${echo_root}/experiments/vulkan3d/ps5/runtime/runtime-layout.ld"
            "-L${native}" "--version-script=${ECHO_RADV_ALIASES_MAP}"
            "--version-script=${echo_root}/experiments/vulkan3d/ps5/runtime/runtime-symbols.map"
            --exclude-libs=ALL --error-limit=0 ${ECHO_RADV_LINK_FLAGS} -e _start)
    else()
        target_link_options(project_echo_ps5 PRIVATE
            "-T${native}/ps5-pie.ld" "--version-script=${native}/app-symbols.map" -e _start)
    endif()
    target_link_directories(project_echo_ps5 PRIVATE "${PS5_SDK_ROOT}/target/lib")
    if(ECHO_PS5_VULKAN)
        target_link_libraries(project_echo_ps5 PRIVATE --start-group SDL2-static
            EchoFramework::Core EchoFramework::Data EchoFramework::Narrative
            EchoFramework::Inventory EchoFramework::Attributes EchoFramework::Equipment
            EchoFramework::Abilities EchoFramework::Quests EchoFramework::Cutscenes EchoFramework::World
            EchoFramework::RenderingVulkan EchoFramework::Rendering --end-group ${ECHO_RADV_STUBS}
            --whole-archive "${ECHO_RADV_ARCHIVE}" --no-whole-archive --start-group
            "${PS5_SDK_ROOT}/target/lib/libc++.a" "${PS5_SDK_ROOT}/target/lib/libc++abi.a"
            "${PS5_SDK_ROOT}/target/lib/libunwind.a"
            "${PS5_SDK_ROOT}/target/lib/libclang_rt.builtins-x86_64.a"
            "${PS5_SDK_ROOT}/target/lib/libps5platform.a"
            --end-group --as-needed ${stubs})
    else()
        target_link_libraries(project_echo_ps5 PRIVATE --start-group SDL2-static
            EchoFramework::Core EchoFramework::Data EchoFramework::Narrative
            EchoFramework::Inventory EchoFramework::Attributes EchoFramework::Equipment
            EchoFramework::Abilities EchoFramework::Quests
            EchoFramework::Cutscenes EchoFramework::World
            --end-group --as-needed ${stubs})
    endif()
endif()
