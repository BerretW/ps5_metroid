#pragma once

#include <stdint.h>
#include <string.h>
#include <vulkan/vulkan.h>

static inline int echo_radv_has_extension(const VkExtensionProperties *items, uint32_t count, const char *name)
{
    for (uint32_t i = 0; i < count; ++i) {
        if (strcmp(items[i].extensionName, name) == 0) return 1;
    }
    return 0;
}

static inline uint32_t echo_radv_graphics_queue(const VkQueueFamilyProperties *items, uint32_t count)
{
    for (uint32_t i = 0; i < count; ++i) {
        if (items[i].queueCount > 0 && (items[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) return i;
    }
    return UINT32_MAX;
}

static inline uint32_t echo_radv_display_mode(const VkDisplayModePropertiesKHR *items, uint32_t count)
{
    uint32_t best = UINT32_MAX;
    uint64_t best_pixels = UINT64_MAX;
    uint32_t best_refresh_delta = UINT32_MAX;
    for (uint32_t i = 0; i < count; ++i) {
        VkExtent2D extent = items[i].parameters.visibleRegion;
        uint32_t refresh = items[i].parameters.refreshRate;
        if (items[i].displayMode == VK_NULL_HANDLE || extent.width < 1280 || extent.height < 720 || refresh == 0) continue;
        uint64_t pixels = (uint64_t)extent.width * extent.height;
        uint32_t delta = refresh > 60000 ? refresh - 60000 : 60000 - refresh;
        if (pixels < best_pixels || (pixels == best_pixels && delta < best_refresh_delta)) {
            best = i;
            best_pixels = pixels;
            best_refresh_delta = delta;
        }
    }
    return best;
}
