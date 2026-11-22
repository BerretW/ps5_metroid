add_library(echo_framework_rendering STATIC
    src/hierarchy.cpp
    src/rendering.cpp
    src/rendering_data.cpp
    src/gltf.cpp
)
add_library(EchoFramework::Rendering ALIAS echo_framework_rendering)
target_include_directories(echo_framework_rendering PUBLIC include)
target_compile_features(echo_framework_rendering PUBLIC cxx_std_20)
target_link_libraries(echo_framework_rendering PUBLIC EchoFramework::Core EchoFramework::Data)

if(MSVC)
    target_compile_options(echo_framework_rendering PRIVATE /W4 /permissive-)
else()
    target_compile_options(echo_framework_rendering PRIVATE -Wall -Wextra -Wpedantic)
endif()
