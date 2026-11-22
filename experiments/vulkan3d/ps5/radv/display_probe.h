#pragma once

#include <vulkan/vulkan.h>

typedef int (*EchoRadvEvent)(const char *stage, int code);

int echo_radv_check_display_extensions(EchoRadvEvent event);
int echo_radv_probe_display(VkInstance instance, VkPhysicalDevice physical, EchoRadvEvent event);
