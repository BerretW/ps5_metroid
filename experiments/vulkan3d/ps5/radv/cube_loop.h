#pragma once

#include "cube_probe.h"

int echo_radv_cube_loop(EchoRadvCube *cube, VkDevice device, PFN_vkGetDeviceProcAddr get_proc,
                        VkQueue queue, VkSwapchainKHR swapchain, VkCommandPool pool,
                        VkCommandBuffer command, VkFence fence, VkSemaphore acquired,
                        uint32_t image_count, EchoRadvEvent event);
