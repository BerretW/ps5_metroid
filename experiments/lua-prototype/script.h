#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { ECHO_LUA_MAX_SOURCE = 4096, ECHO_LUA_ERROR_SIZE = 256 };

typedef struct EchoLuaInput {
    int distance;
    int health;
} EchoLuaInput;

typedef struct EchoLuaOutput {
    int move_x;
    int fire;
    int visible;
} EchoLuaOutput;

/* A fresh, restricted interpreter is created for each call. Output is transactional. */
int echo_lua_evaluate(const char* source, size_t length, EchoLuaInput input,
                      EchoLuaOutput* output, char error[ECHO_LUA_ERROR_SIZE]);

#ifdef __cplusplus
}
#endif
