add_library(echo_framework_data STATIC
    src/data.cpp
    src/schema.cpp
)
if(WIN32)
    target_sources(echo_framework_data PRIVATE src/windows_save_storage.cpp)
    target_link_libraries(echo_framework_data PRIVATE shell32 advapi32 ole32)
endif()
if(PS5)
    target_sources(echo_framework_data PRIVATE src/ps5_save_storage.cpp)
endif()
add_library(EchoFramework::Data ALIAS echo_framework_data)
target_include_directories(echo_framework_data PUBLIC include)
target_compile_features(echo_framework_data PUBLIC cxx_std_20)
target_link_libraries(echo_framework_data PUBLIC EchoFramework::Core yyjson)

if(MSVC)
    target_compile_options(echo_framework_data PRIVATE /W4 /permissive-)
else()
    target_compile_options(echo_framework_data PRIVATE -Wall -Wextra -Wpedantic)
endif()
