cmake_minimum_required(VERSION 3.24)
project(echo_ps5_host LANGUAGES C CXX)

if(NOT CMAKE_HOST_WIN32)
    message(FATAL_ERROR "The packaging tools must be built on native Windows.")
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(ZLIB_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(ZLIB_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(ZLIB_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory("${ZLIB_SOURCE}" zlib)

set(native "${BOILERPLATE_SOURCE}/tooling/native")
file(READ "${native}/sce_module_writer.cpp" writer)
set(original "const std::uint64_t relro_file = got.file_offset;")
string(FIND "${writer}" "${original}" found)
if(found LESS 0)
    message(FATAL_ERROR "Pinned converter RELRO patch context is missing.")
endif()
# The GOT need not be the first RELRO section (SDL has .data.rel.ro).
string(REPLACE "${original}"
    "require(got.address >= relro_start && got.file_offset >= got.address - relro_start,
            \"GOT/RELRO file mapping would underflow\");
    const std::uint64_t relro_file = got.file_offset - (got.address - relro_start);"
    writer "${writer}")
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/sce_module_writer.cpp" CONTENT "${writer}")
add_executable(libc-builder "${native}/libc_builder.cpp")
add_executable(ps5-native-tool
    "${native}/native_app_builder.cpp"
    "${native}/self_container.cpp"
    "${native}/elf_object.cpp"
    "${CMAKE_CURRENT_BINARY_DIR}/sce_module_writer.cpp")
target_include_directories(ps5-native-tool PRIVATE "${native}")
target_link_libraries(ps5-native-tool PRIVATE zlibstatic)
if(MSVC)
    target_compile_options(libc-builder PRIVATE /W4 /EHsc)
    target_compile_options(ps5-native-tool PRIVATE /W4 /EHsc)
endif()
