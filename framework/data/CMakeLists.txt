add_library(echo_framework_data STATIC
    src/data.cpp
)
add_library(EchoFramework::Data ALIAS echo_framework_data)
target_include_directories(echo_framework_data PUBLIC include)
target_compile_features(echo_framework_data PUBLIC cxx_std_20)
target_link_libraries(echo_framework_data PUBLIC EchoFramework::Core yyjson)

if(MSVC)
    target_compile_options(echo_framework_data PRIVATE /W4 /permissive-)
else()
    target_compile_options(echo_framework_data PRIVATE -Wall -Wextra -Wpedantic)
endif()
