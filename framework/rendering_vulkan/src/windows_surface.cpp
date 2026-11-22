#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define VK_USE_PLATFORM_WIN32_KHR
#include <windows.h>
#include "platform_surface.hpp"
namespace echo::rendering {
const char* platformSurfaceExtension() noexcept { return VK_KHR_WIN32_SURFACE_EXTENSION_NAME; }
VkResult createPlatformSurface(VkInstance instance, void* window, VkSurfaceKHR* surface) noexcept {
    VkWin32SurfaceCreateInfoKHR info{VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR};
    info.hinstance = GetModuleHandleW(nullptr);
    info.hwnd = static_cast<HWND>(window);
    return vkCreateWin32SurfaceKHR(instance, &info, nullptr, surface);
}
}
