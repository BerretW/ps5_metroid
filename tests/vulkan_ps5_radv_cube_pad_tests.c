#define VK_NO_PROTOTYPES
#include "cube_pad.h"
#include <stddef.h>
#include "SDL_ps5joystick.h"
#include <string.h>

static int scenario, opened, closed, motion, connected, disconnected;
static PS5_PadData sample;
int sceUserServiceInitialize(void *p) { (void)p; return scenario == 1 ? (int)0x80960003u : 0; }
int sceUserServiceGetLoginUserIdList(int users[4])
{
    users[0] = scenario == 2 ? -1 : 42;
    users[1] = users[2] = users[3] = -1;
    return 0;
}
int scePadInit(void) { return 0; }
int scePadOpen(int user, int type, int index, void *p)
{
    (void)p;
    if (user != 42 || type != 0 || index != 0) return -1;
    ++opened;
    return scenario == 3 ? -5 : 10;
}
int scePadClose(int handle) { if (handle == 10) ++closed; return 0; }
int scePadReadState(int handle, PS5_PadData *out)
{
    if (handle != 10 || scenario == 4) return -7;
    *out = sample;
    return 0;
}
static int event(const char *stage, int code)
{
    (void)code;
    if (strcmp(stage, "radv.pad.motion") == 0) ++motion;
    if (strcmp(stage, "radv.pad.connected") == 0) ++connected;
    if (strcmp(stage, "radv.pad.disconnected") == 0) ++disconnected;
    return scenario == 5 && strcmp(stage, "radv.pad.open.end") == 0 ? -1 : 0;
}
int main(void)
{
    EchoRadvPad pad;
    float angles[2] = {0, 0};
    scenario = 1;
    if (echo_radv_pad_open(&pad, event) != 0 || pad.handle != 10) return 1;
    sample.connected = 1;
    sample.leftStick.x = 128;
    sample.leftStick.y = 127;
    if (echo_radv_pad_update(&pad, angles, event) != 0 || angles[0] != 0 || angles[1] != 0 || connected != 1) return 2;
    sample.leftStick.x = 255;
    sample.leftStick.y = 0;
    if (echo_radv_pad_update(&pad, angles, event) != 0 ||
        angles[0] < 0.039f || angles[0] > 0.041f || angles[1] > -0.039f || angles[1] < -0.041f || motion != 1) return 3;
    sample.connected = 0;
    if (echo_radv_pad_update(&pad, angles, event) != 0 || disconnected != 1 || angles[0] > 0.041f) return 4;
    sample.connected = 1;
    if (echo_radv_pad_update(&pad, angles, event) != 0 || connected != 2 || motion != 1) return 5;
    scenario = 4;
    if (echo_radv_pad_update(&pad, angles, event) == 0) return 6;
    scenario = 2;
    opened = 0;
    if (echo_radv_pad_open(&pad, event) == 0 || opened != 0) return 7;
    scenario = 3;
    if (echo_radv_pad_open(&pad, event) == 0) return 8;
    scenario = 5;
    if (echo_radv_pad_open(&pad, event) == 0 || closed != 1) return 9;
    return 0;
}
