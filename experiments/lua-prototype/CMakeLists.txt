cmake_minimum_required(VERSION 3.24)
project(EchoLuaPrototype LANGUAGES C CXX)

if(CMAKE_CROSSCOMPILING OR NOT WIN32)
    message(FATAL_ERROR "The Lua prototype is host-only and currently requires Windows.")
endif()

include(CTest)
include(FetchContent)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/../../dependencies.ps5.json" dependencies)
string(JSON lua_url GET "${dependencies}" luaPrototype url)
string(JSON lua_hash GET "${dependencies}" luaPrototype sha256)
FetchContent_Declare(echo_lua URL "${lua_url}" URL_HASH "SHA256=${lua_hash}")
FetchContent_MakeAvailable(echo_lua)

set(lua_sources)
foreach(source IN ITEMS
        lapi lauxlib lcode lctype ldebug ldo ldump lfunc lgc llex lmem lobject
        lopcodes lparser lstate lstring ltable ltm lundump lvm lzio)
    list(APPEND lua_sources "${echo_lua_SOURCE_DIR}/src/${source}.c")
endforeach()
add_library(echo_lua_static STATIC ${lua_sources})
target_include_directories(echo_lua_static PUBLIC "${echo_lua_SOURCE_DIR}/src")

add_library(echo_lua_prototype STATIC script.c)
target_include_directories(echo_lua_prototype PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(echo_lua_prototype PRIVATE echo_lua_static)
add_executable(echo_lua_prototype_tests tests.cpp)
target_compile_features(echo_lua_prototype_tests PRIVATE cxx_std_20)
target_link_libraries(echo_lua_prototype_tests PRIVATE echo_lua_prototype)
add_test(NAME echo_lua_prototype COMMAND echo_lua_prototype_tests)

