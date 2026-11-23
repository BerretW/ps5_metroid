#include "script.h"

#include "lua.h"
#include "lauxlib.h"

#include <stdlib.h>
#include <string.h>

enum { MEMORY_LIMIT = 256 * 1024, HOOK_INTERVAL = 100, HOOK_LIMIT = 100 };

typedef struct Run {
    const char* source;
    size_t length;
    size_t memory;
    unsigned hooks;
    EchoLuaInput input;
    EchoLuaOutput result;
} Run;

static void* allocate(void* context, void* pointer, size_t old_size, size_t new_size) {
    Run* run = (Run*)context;
    size_t previous = pointer ? old_size : 0;
    if (new_size == 0) {
        free(pointer);
        run->memory -= previous;
        return NULL;
    }
    if (new_size > previous && new_size - previous > MEMORY_LIMIT - run->memory)
        return NULL;
    void* next = realloc(pointer, new_size);
    if (next != NULL) run->memory = run->memory - previous + new_size;
    return next;
}

static void limit_instructions(lua_State* state, lua_Debug* debug) {
    (void)debug;
    Run* run = *(Run**)lua_getextraspace(state);
    if (++run->hooks > HOOK_LIMIT) luaL_error(state, "instruction limit exceeded");
}

static int protected_run(lua_State* state) {
    Run* run = (Run*)lua_touserdata(state, 1);
    if (luaL_loadbufferx(state, run->source, run->length, "script", "t") != LUA_OK)
        return lua_error(state);
    lua_call(state, 0, 1);
    if (!lua_isfunction(state, -1)) return luaL_error(state, "script must return a function");
    lua_createtable(state, 0, 2);
    lua_pushinteger(state, run->input.distance);
    lua_setfield(state, -2, "distance");
    lua_pushinteger(state, run->input.health);
    lua_setfield(state, -2, "health");
    lua_call(state, 1, 1);
    if (!lua_istable(state, -1)) return luaL_error(state, "function must return a table");
    lua_pushnil(state);
    while (lua_next(state, -2) != 0) {
        size_t length = 0;
        const char* key = lua_type(state, -2) == LUA_TSTRING
            ? lua_tolstring(state, -2, &length) : NULL;
        if (key == NULL ||
            !((length == 6 && memcmp(key, "move_x", 6) == 0) ||
              (length == 4 && memcmp(key, "fire", 4) == 0) ||
              (length == 7 && memcmp(key, "visible", 7) == 0)))
            return luaL_error(state, "unknown output field");
        lua_pop(state, 1);
    }
    lua_getfield(state, -1, "move_x");
    if (!lua_isinteger(state, -1) || lua_tointeger(state, -1) < -1 ||
        lua_tointeger(state, -1) > 1)
        return luaL_error(state, "move_x must be an integer from -1 to 1");
    run->result.move_x = (int)lua_tointeger(state, -1);
    lua_pop(state, 1);
    lua_getfield(state, -1, "fire");
    if (!lua_isboolean(state, -1)) return luaL_error(state, "fire must be boolean");
    run->result.fire = lua_toboolean(state, -1);
    lua_pop(state, 1);
    lua_getfield(state, -1, "visible");
    if (!lua_isboolean(state, -1)) return luaL_error(state, "visible must be boolean");
    run->result.visible = lua_toboolean(state, -1);
    return 0;
}

int echo_lua_evaluate(const char* source, size_t length, EchoLuaInput input,
                      EchoLuaOutput* output, char error[ECHO_LUA_ERROR_SIZE]) {
    if (error == NULL) return 0;
    error[0] = '\0';
    if (source == NULL || output == NULL || length == 0 || length > ECHO_LUA_MAX_SOURCE ||
        memchr(source, '\0', length) != NULL) {
        const char* message = "invalid script input";
        memcpy(error, message, strlen(message) + 1);
        return 0;
    }
    Run run = {0};
    run.source = source;
    run.length = length;
    run.input = input;
    lua_State* state = lua_newstate(allocate, &run);
    if (state == NULL) {
        const char* message = "interpreter allocation failed";
        memcpy(error, message, strlen(message) + 1);
        return 0;
    }
    *(Run**)lua_getextraspace(state) = &run;
    lua_sethook(state, limit_instructions, LUA_MASKCOUNT, HOOK_INTERVAL);
    lua_pushcfunction(state, protected_run);
    lua_pushlightuserdata(state, &run);
    int status = lua_pcall(state, 1, 0, 0);
    if (status != LUA_OK) {
        const char* message = lua_tostring(state, -1);
        if (message == NULL) message = "script failed";
        size_t size = strlen(message);
        if (size >= ECHO_LUA_ERROR_SIZE) size = ECHO_LUA_ERROR_SIZE - 1;
        memcpy(error, message, size);
        error[size] = '\0';
    } else {
        *output = run.result;
    }
    lua_close(state);
    return status == LUA_OK;
}
