#pragma once
#include <vulkan/vulkan.h>
namespace echo::rendering {
const char* platformSurfaceExtension() noexcept;
VkResult createPlatformSurface(VkInstance, void* nativeWindow, VkSurfaceKHR*) noexcept;
}
