add_library(echo_framework_vfx STATIC
    src/emitter_data.cpp
    src/vfx.cpp
)
add_library(EchoFramework::VFX ALIAS echo_framework_vfx)
target_include_directories(echo_framework_vfx PUBLIC include)
target_compile_features(echo_framework_vfx PUBLIC cxx_std_20)
target_link_libraries(echo_framework_vfx PUBLIC
    EchoFramework::Core EchoFramework::Data EchoFramework::Rendering)

if(MSVC)
    target_compile_options(echo_framework_vfx PRIVATE /W4 /permissive-)
else()
    target_compile_options(echo_framework_vfx PRIVATE -Wall -Wextra -Wpedantic)
endif()
