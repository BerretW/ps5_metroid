add_library(echo_framework_core STATIC
    src/core.cpp
)
add_library(EchoFramework::Core ALIAS echo_framework_core)
target_include_directories(echo_framework_core PUBLIC include)
target_compile_features(echo_framework_core PUBLIC cxx_std_20)

add_library(echo_framework_entities INTERFACE)
add_library(EchoFramework::Entities ALIAS echo_framework_entities)
target_link_libraries(echo_framework_entities INTERFACE echo_framework_core)

add_library(echo_framework_events INTERFACE)
add_library(EchoFramework::Events ALIAS echo_framework_events)
target_link_libraries(echo_framework_events INTERFACE echo_framework_core)

if(MSVC)
    target_compile_options(echo_framework_core PRIVATE /W4 /permissive-)
else()
    target_compile_options(echo_framework_core PRIVATE -Wall -Wextra -Wpedantic)
endif()
