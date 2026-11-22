#include "platform_surface.hpp"

#include <cstdio>
#include <cstdint>
#include <initializer_list>

namespace {

enum class Scenario { Valid, NoDisplay, NoMode, NoPlane, BadCapabilities, NullSurface, Incomplete };
Scenario scenario = Scenario::Valid;
int createCalls{};
VkDisplaySurfaceCreateInfoKHR selected{};
const auto display = reinterpret_cast<VkDisplayKHR>(static_cast<std::uintptr_t>(1));
const auto otherDisplay = reinterpret_cast<VkDisplayKHR>(static_cast<std::uintptr_t>(2));
const auto mode720 = reinterpret_cast<VkDisplayModeKHR>(static_cast<std::uintptr_t>(3));
const auto mode1080 = reinterpret_cast<VkDisplayModeKHR>(static_cast<std::uintptr_t>(4));
const auto surfaceHandle = reinterpret_cast<VkSurfaceKHR>(static_cast<std::uintptr_t>(5));

VKAPI_ATTR VkResult VKAPI_CALL displays(VkPhysicalDevice, std::uint32_t* count,
                                        VkDisplayPropertiesKHR* items) {
    if (scenario == Scenario::Incomplete) return VK_INCOMPLETE;
    *count = scenario == Scenario::NoDisplay ? 0 : 1;
    if (*count) {
        items[0] = {};
        items[0].display = display;
        items[0].supportedTransforms = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    }
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL modes(VkPhysicalDevice, VkDisplayKHR,
                                     std::uint32_t* count, VkDisplayModePropertiesKHR* items) {
    *count = scenario == Scenario::NoMode ? 0 : 2;
    if (*count) {
        items[0] = {mode1080, {{1920, 1080}, 59940}};
        items[1] = {mode720, {{1280, 720}, 60000}};
    }
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL planes(VkPhysicalDevice, std::uint32_t* count,
                                      VkDisplayPlanePropertiesKHR* items) {
    *count = 2;
    items[0] = {otherDisplay, 0};
    items[1] = {VK_NULL_HANDLE, 3};
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL supported(VkPhysicalDevice, std::uint32_t,
                                         std::uint32_t* count, VkDisplayKHR* items) {
    *count = 1;
    items[0] = scenario == Scenario::NoPlane ? otherDisplay : display;
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL capabilities(VkPhysicalDevice, VkDisplayModeKHR,
                                            std::uint32_t, VkDisplayPlaneCapabilitiesKHR* caps) {
    *caps = {};
    caps->supportedAlpha = scenario == Scenario::BadCapabilities
        ? 0 : VK_DISPLAY_PLANE_ALPHA_OPAQUE_BIT_KHR;
    caps->maxSrcExtent = {4096, 4096};
    caps->maxDstExtent = {4096, 4096};
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL create(VkInstance, const VkDisplaySurfaceCreateInfoKHR* info,
                                      const VkAllocationCallbacks*, VkSurfaceKHR* output) {
    ++createCalls;
    selected = *info;
    *output = scenario == Scenario::NullSurface ? VK_NULL_HANDLE : surfaceHandle;
    return VK_SUCCESS;
}

} // namespace

namespace echo::rendering {
PFN_vkGetPhysicalDeviceDisplayPropertiesKHR vkGetPhysicalDeviceDisplayPropertiesKHR = displays;
PFN_vkGetDisplayModePropertiesKHR vkGetDisplayModePropertiesKHR = modes;
PFN_vkGetPhysicalDeviceDisplayPlanePropertiesKHR vkGetPhysicalDeviceDisplayPlanePropertiesKHR = planes;
PFN_vkGetDisplayPlaneSupportedDisplaysKHR vkGetDisplayPlaneSupportedDisplaysKHR = supported;
PFN_vkGetDisplayPlaneCapabilitiesKHR vkGetDisplayPlaneCapabilitiesKHR = capabilities;
PFN_vkCreateDisplayPlaneSurfaceKHR vkCreateDisplayPlaneSurfaceKHR = create;
}

int main() {
    int failures{};
    const auto check = [&](bool condition, const char* message) {
        if (!condition) { std::fprintf(stderr, "FAIL: %s\n", message); ++failures; }
    };
    VkSurfaceKHR surface{};
    VkExtent2D extent{};
    auto result = echo::rendering::createPlatformSurface({}, {}, nullptr, &surface, &extent);
    check(result == VK_SUCCESS && surface == surfaceHandle && extent.width == 1280 &&
          extent.height == 720 && createCalls == 1 && selected.displayMode == mode720 &&
          selected.planeIndex == 1 && selected.planeStackIndex == 3 &&
          selected.alphaMode == VK_DISPLAY_PLANE_ALPHA_OPAQUE_BIT_KHR,
          "surface selects a supported mode and compatible display plane");
    for (auto invalid : {Scenario::NoDisplay, Scenario::NoMode, Scenario::NoPlane,
                         Scenario::BadCapabilities, Scenario::Incomplete}) {
        scenario = invalid;
        surface = {};
        createCalls = 0;
        result = echo::rendering::createPlatformSurface({}, {}, nullptr, &surface, &extent);
        check(result != VK_SUCCESS && surface == VK_NULL_HANDLE && createCalls == 0,
              "unsupported or incomplete display capabilities fail before surface creation");
    }
    scenario = Scenario::NullSurface;
    surface = {};
    result = echo::rendering::createPlatformSurface({}, {}, nullptr, &surface, &extent);
    check(result != VK_SUCCESS && surface == VK_NULL_HANDLE,
          "a successful driver call returning a null surface is rejected");
    return failures ? 1 : 0;
}
