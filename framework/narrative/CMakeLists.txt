add_library(echo_framework_narrative STATIC
    src/narrative.cpp
)
add_library(EchoFramework::Narrative ALIAS echo_framework_narrative)
target_include_directories(echo_framework_narrative PUBLIC include)
target_compile_features(echo_framework_narrative PUBLIC cxx_std_20)
target_link_libraries(echo_framework_narrative PUBLIC EchoFramework::Core EchoFramework::Data)

if(MSVC)
    target_compile_options(echo_framework_narrative PRIVATE /W4 /permissive-)
else()
    target_compile_options(echo_framework_narrative PRIVATE -Wall -Wextra -Wpedantic)
endif()
