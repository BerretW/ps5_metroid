#pragma once

#include "display_probe.h"

enum {
    ECHO_RADV_FRAME_PRESENTED = 0,
    ECHO_RADV_FRAME_FAILED = 1,
    ECHO_RADV_FRAME_HOLD_FAILED = 2
};

int echo_radv_first_frame(VkInstance instance, VkPhysicalDevice physical, VkDevice device,
                          VkQueue queue, uint32_t family, VkDisplayPropertiesKHR display,
                          VkDisplayModePropertiesKHR mode, EchoRadvEvent event);
