#include "script.h"

#include <cstring>
#include <iostream>
#include <string>

namespace {
int failures{};

void check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

bool run(const char* source, EchoLuaInput input, EchoLuaOutput& output, char* error) {
    return echo_lua_evaluate(source, std::strlen(source), input, &output, error) != 0;
}
}

int main() {
    constexpr char script[] = R"(
return function(input)
    return {move_x = input.distance > 10 and 1 or 0,
            fire = input.distance <= 10,
            visible = input.health > 0}
end
)";
    EchoLuaOutput output{};
    char error[ECHO_LUA_ERROR_SIZE]{};
    for (int i = 0; i < 10; ++i) {
        check(run(script, {15, 3}, output, error) && output.move_x == 1 &&
                  !output.fire && output.visible,
              "AI and UI outputs are reproducible for identical input");
    }
    check(run(script, {5, 0}, output, error) && output.move_x == 0 &&
              output.fire && !output.visible,
          "script outputs respond to changed input");

    const EchoLuaOutput previous = output;
    check(!run("while true do end", {}, output, error) &&
              std::strstr(error, "instruction limit") != nullptr &&
              output.move_x == previous.move_x && output.fire == previous.fire &&
              output.visible == previous.visible,
          "runaway scripts fail without publishing partial output");
    check(!run("return function(i) return {move_x=2,fire=false,visible=true} end",
               {}, output, error) && std::strstr(error, "move_x") != nullptr,
          "out-of-range AI output is rejected");
    check(!run("return function(i) return {move_x=0,fire=false,visible=true,x=1} end",
               {}, output, error) && std::strstr(error, "unknown") != nullptr,
          "unknown output fields are rejected");
    check(!run("return function(i) return {move_x=0,fire=false,visible=true,"
               "[\"fire\\0extra\"]=true} end", {}, output, error) &&
              std::strstr(error, "unknown") != nullptr,
          "embedded NUL cannot disguise an unknown output field");
    check(!run("return function(i) return os.execute('echo unsafe') end",
               {}, output, error) && error[0] != '\0',
          "scripts cannot access operating system APIs");
    check(!run("return function(i) local a='x'; while true do a=a..a end end",
               {}, output, error) && std::strstr(error, "memory") != nullptr,
          "unbounded allocation reaches the memory limit explicitly");
    check(!run("return function(", {}, output, error) && error[0] != '\0',
          "syntax errors are reported");
    const char bytecode[] = {'\x1b', 'L', 'u', 'a', '\x54'};
    check(!echo_lua_evaluate(bytecode, sizeof(bytecode), {}, &output, error) &&
              error[0] != '\0',
          "precompiled Lua bytecode is rejected");
    const std::string oversized(ECHO_LUA_MAX_SOURCE + 1, 'x');
    check(!echo_lua_evaluate(oversized.data(), oversized.size(), {}, &output, error),
          "oversized source is rejected");
    return failures == 0 ? 0 : 1;
}
