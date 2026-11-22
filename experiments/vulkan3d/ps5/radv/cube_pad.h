#pragma once

#include "display_probe.h"

typedef struct EchoRadvPad {
    int handle;
    int connected;
    int motion_reported;
    int first_read;
} EchoRadvPad;

int echo_radv_pad_open(EchoRadvPad *pad, EchoRadvEvent event);
int echo_radv_pad_update(EchoRadvPad *pad, float angles[2], EchoRadvEvent event);
