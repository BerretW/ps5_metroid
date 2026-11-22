#include "platform_surface.hpp"
#include "display_selection.h"

#include <array>
#include <cstdio>

namespace echo::rendering {

const char* platformSurfaceExtension() noexcept { return VK_KHR_DISPLAY_EXTENSION_NAME; }

VkResult createPlatformSurface(VkInstance instance, VkPhysicalDevice physical, void*,
                               VkSurfaceKHR* surface, VkExtent2D* preferredExtent) noexcept {
    std::array<VkDisplayPropertiesKHR, 8> displays{};
    std::uint32_t count = static_cast<std::uint32_t>(displays.size());
    VkResult result = vkGetPhysicalDeviceDisplayPropertiesKHR(physical, &count, displays.data());
    if (result != VK_SUCCESS) return result;
    if (count == 0 || count > displays.size() || displays[0].display == VK_NULL_HANDLE ||
        !(displays[0].supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR)) {
        std::fprintf(stderr, "PS5 Vulkan display unavailable or does not support identity transform\n");
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    std::array<VkDisplayModePropertiesKHR, 32> modes{};
    count = static_cast<std::uint32_t>(modes.size());
    result = vkGetDisplayModePropertiesKHR(physical, displays[0].display, &count, modes.data());
    if (result != VK_SUCCESS) return result;
    if (count > modes.size()) return VK_ERROR_INITIALIZATION_FAILED;
    const std::uint32_t selected = echo_radv_display_mode(modes.data(), count);
    if (selected == UINT32_MAX) {
        std::fprintf(stderr, "PS5 Vulkan display has no supported mode\n");
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    std::array<VkDisplayPlanePropertiesKHR, 8> planes{};
    count = static_cast<std::uint32_t>(planes.size());
    result = vkGetPhysicalDeviceDisplayPlanePropertiesKHR(physical, &count, planes.data());
    if (result != VK_SUCCESS) return result;
    if (count == 0 || count > planes.size()) return VK_ERROR_INITIALIZATION_FAILED;
    std::uint32_t plane = UINT32_MAX;
    for (std::uint32_t index = 0; index < count; ++index) {
        if (planes[index].currentDisplay != VK_NULL_HANDLE &&
            planes[index].currentDisplay != displays[0].display) continue;
        std::array<VkDisplayKHR, 8> supported{};
        std::uint32_t supportedCount = static_cast<std::uint32_t>(supported.size());
        result = vkGetDisplayPlaneSupportedDisplaysKHR(physical, index, &supportedCount,
                                                        supported.data());
        if (result != VK_SUCCESS) return result;
        if (supportedCount > supported.size()) return VK_ERROR_INITIALIZATION_FAILED;
        for (std::uint32_t i = 0; i < supportedCount; ++i) {
            if (supported[i] == displays[0].display) { plane = index; break; }
        }
        if (plane != UINT32_MAX) break;
    }
    if (plane == UINT32_MAX) {
        std::fprintf(stderr, "PS5 Vulkan display has no compatible plane\n");
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkDisplayPlaneCapabilitiesKHR caps{};
    result = vkGetDisplayPlaneCapabilitiesKHR(physical, modes[selected].displayMode, plane, &caps);
    if (result != VK_SUCCESS) return result;
    const VkExtent2D extent = modes[selected].parameters.visibleRegion;
    if (!(caps.supportedAlpha & VK_DISPLAY_PLANE_ALPHA_OPAQUE_BIT_KHR) ||
        caps.minSrcPosition.x > 0 || caps.maxSrcPosition.x < 0 ||
        caps.minSrcPosition.y > 0 || caps.maxSrcPosition.y < 0 ||
        caps.minDstPosition.x > 0 || caps.maxDstPosition.x < 0 ||
        caps.minDstPosition.y > 0 || caps.maxDstPosition.y < 0 ||
        extent.width < caps.minSrcExtent.width || extent.width > caps.maxSrcExtent.width ||
        extent.height < caps.minSrcExtent.height || extent.height > caps.maxSrcExtent.height ||
        extent.width < caps.minDstExtent.width || extent.width > caps.maxDstExtent.width ||
        extent.height < caps.minDstExtent.height || extent.height > caps.maxDstExtent.height) {
        std::fprintf(stderr, "PS5 Vulkan display plane capabilities are incompatible\n");
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkDisplaySurfaceCreateInfoKHR info{VK_STRUCTURE_TYPE_DISPLAY_SURFACE_CREATE_INFO_KHR};
    info.displayMode = modes[selected].displayMode;
    info.planeIndex = plane;
    info.planeStackIndex = planes[plane].currentStackIndex;
    info.transform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    info.globalAlpha = 1.0F;
    info.alphaMode = VK_DISPLAY_PLANE_ALPHA_OPAQUE_BIT_KHR;
    info.imageExtent = extent;
    result = vkCreateDisplayPlaneSurfaceKHR(instance, &info, nullptr, surface);
    if (result == VK_SUCCESS) {
        if (*surface == VK_NULL_HANDLE) return VK_ERROR_INITIALIZATION_FAILED;
        *preferredExtent = extent;
        std::printf("{\"stage\":\"display.selected\",\"width\":%u,\"height\":%u,\"refresh_millihz\":%u}\n",
                    extent.width, extent.height, modes[selected].parameters.refreshRate);
    }
    return result;
}

} // namespace echo::rendering
