add_executable(echo_framework_demo main.cpp)
target_link_libraries(echo_framework_demo PRIVATE EchoFramework::Core EchoFramework::Entities
    EchoFramework::Events EchoFramework::Data)
target_compile_features(echo_framework_demo PRIVATE cxx_std_20)
target_compile_definitions(echo_framework_demo PRIVATE
    ECHO_FRAMEWORK_DEMO_CONFIG="${CMAKE_CURRENT_SOURCE_DIR}/game.json")
add_test(NAME echo_framework_headless_demo COMMAND echo_framework_demo)
