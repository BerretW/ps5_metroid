#pragma once
#ifdef ECHO_PS5_VULKAN
#define VK_NO_PROTOTYPES
#endif
#include <vulkan/vulkan.h>
#ifdef ECHO_PS5_VULKAN
#include "ps5_dispatch.hpp"
#endif
namespace echo::rendering {
const char* platformSurfaceExtension() noexcept;
VkResult createPlatformSurface(VkInstance, VkPhysicalDevice, void* nativeWindow,
                               VkSurfaceKHR*, VkExtent2D* preferredExtent) noexcept;
}
