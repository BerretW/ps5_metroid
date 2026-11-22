#include "cube_pad.h"

#include <stddef.h>
#include "SDL_ps5joystick.h"

int echo_radv_pad_open(EchoRadvPad *pad, EchoRadvEvent event)
{
    pad->handle = -1;
    pad->connected = -1;
    pad->motion_reported = 0;
    pad->first_read = 1;
    if (event("radv.pad.user_service.begin", 0) != 0) return 1;
    int result = sceUserServiceInitialize(NULL);
    if (event("radv.pad.user_service.end", result) != 0 ||
        (result != 0 && (uint32_t)result != 0x80960003u)) return 1;
    if (event("radv.pad.init.begin", 0) != 0) return 1;
    result = scePadInit();
    if (event("radv.pad.init.end", result) != 0 || result != 0) return 1;
    int users[4] = {-1, -1, -1, -1};
    if (event("radv.pad.users.begin", 0) != 0) return 1;
    result = sceUserServiceGetLoginUserIdList(users);
    if (event("radv.pad.users.end", result) != 0 || result != 0) return 1;
    int user = -1;
    for (int i = 0; i < 4; ++i) {
        if (users[i] != -1) { user = users[i]; break; }
    }
    if (user == -1) { event("radv.pad.no_user", 0); return 1; }
    if (event("radv.pad.open.begin", 0) != 0) return 1;
    result = scePadOpen(user, PS5_PAD_PORT_TYPE_STANDARD, 0, NULL);
    if (event("radv.pad.open.end", result < 0 ? result : 0) != 0 || result < 0) {
        if (result >= 0) {
            int close_result = scePadClose(result);
            if (close_result != 0) event("radv.pad.close.error", close_result);
        }
        return 1;
    }
    pad->handle = result;
    return 0;
}

static float axis(uint8_t value)
{
    int centered = value < 128 ? (int)value - 127 : (int)value - 128;
    if (centered >= -19 && centered <= 19) return 0.0f;
    return (float)centered / 127.0f;
}

int echo_radv_pad_update(EchoRadvPad *pad, float angles[2], EchoRadvEvent event)
{
    PS5_PadData state = {0};
    if (pad->first_read && event("radv.pad.read.begin", 0) != 0) return 1;
    int result = scePadReadState(pad->handle, &state);
    if (pad->first_read) {
        pad->first_read = 0;
        if (event("radv.pad.read.end", result) != 0) return 1;
    }
    if (result != 0) { event("radv.pad.read.error", result); return 1; }
    int connected = state.connected != 0;
    if (connected != pad->connected) {
        if (event(connected ? "radv.pad.connected" : "radv.pad.disconnected", 0) != 0) return 1;
        pad->connected = connected;
    }
    if (!connected) return 0;
    float x = axis(state.leftStick.x), y = axis(state.leftStick.y);
    if ((x != 0.0f || y != 0.0f) && !pad->motion_reported) {
        if (event("radv.pad.motion", 0) != 0) return 1;
        pad->motion_reported = 1;
    }
    angles[0] += x * 0.04f;
    angles[1] += y * 0.04f;
    for (int i = 0; i < 2; ++i) {
        if (angles[i] > 6.2831853f) angles[i] -= 6.2831853f;
        if (angles[i] < -6.2831853f) angles[i] += 6.2831853f;
    }
    return 0;
}
