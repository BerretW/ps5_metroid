#define VK_NO_PROTOTYPES
#include "display_probe.h"
#include "display_selection.h"
#ifdef ECHO_RADV_FRAME_PROBE
#include "frame_probe.h"
#endif

#include <limits.h>

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
vk_icdGetInstanceProcAddr(VkInstance instance, const char *name);

int echo_radv_check_display_extensions(EchoRadvEvent event)
{
    PFN_vkEnumerateInstanceExtensionProperties enumerate =
        (PFN_vkEnumerateInstanceExtensionProperties)vk_icdGetInstanceProcAddr(
            VK_NULL_HANDLE, "vkEnumerateInstanceExtensionProperties");
    if (!enumerate) {
        event("radv.instance.extensions.missing_function", 0);
        return 1;
    }
    VkExtensionProperties extensions[128];
    uint32_t count = 128;
    if (event("radv.instance.extensions.begin", 0) != 0) return 1;
    VkResult result = enumerate(NULL, &count, extensions);
    if (event("radv.instance.extensions.end", result) != 0 || result != VK_SUCCESS) return 1;
    if (count > 128) {
        event("radv.instance.extensions.overflow", 0);
        return 1;
    }
    if (!echo_radv_has_extension(extensions, count, VK_KHR_SURFACE_EXTENSION_NAME) ||
        !echo_radv_has_extension(extensions, count, VK_KHR_DISPLAY_EXTENSION_NAME)) {
        event("radv.instance.extensions.unsupported", 0);
        return 1;
    }
    return event("radv.instance.extensions.supported", 0) != 0;
}

int echo_radv_probe_display(VkInstance instance, VkPhysicalDevice physical, EchoRadvEvent event)
{
#define LOAD(type, name) type name = (type)vk_icdGetInstanceProcAddr(instance, "vk" #name)
    LOAD(PFN_vkGetPhysicalDeviceProperties, GetPhysicalDeviceProperties);
    LOAD(PFN_vkGetPhysicalDeviceQueueFamilyProperties, GetPhysicalDeviceQueueFamilyProperties);
    LOAD(PFN_vkEnumerateDeviceExtensionProperties, EnumerateDeviceExtensionProperties);
    LOAD(PFN_vkGetPhysicalDeviceDisplayPropertiesKHR, GetPhysicalDeviceDisplayPropertiesKHR);
    LOAD(PFN_vkGetDisplayModePropertiesKHR, GetDisplayModePropertiesKHR);
    LOAD(PFN_vkCreateDevice, CreateDevice);
    LOAD(PFN_vkGetDeviceProcAddr, GetDeviceProcAddr);
#undef LOAD
    if (!GetPhysicalDeviceProperties || !GetPhysicalDeviceQueueFamilyProperties ||
        !EnumerateDeviceExtensionProperties || !GetPhysicalDeviceDisplayPropertiesKHR ||
        !GetDisplayModePropertiesKHR || !CreateDevice || !GetDeviceProcAddr) {
        event("radv.display.missing_function", 0);
        return 1;
    }
    VkPhysicalDeviceProperties properties;
    if (event("radv.properties.begin", 0) != 0) return 1;
    GetPhysicalDeviceProperties(physical, &properties);
    if (event("radv.properties.end", 0) != 0 ||
        event("radv.api.major", (int)VK_VERSION_MAJOR(properties.apiVersion)) != 0 ||
        event("radv.api.minor", (int)VK_VERSION_MINOR(properties.apiVersion)) != 0 ||
        event("radv.vendor_id", (int)properties.vendorID) != 0 ||
        event("radv.device_id", (int)properties.deviceID) != 0) return 1;

    uint32_t count = 0;
    if (event("radv.queues.begin", 0) != 0) return 1;
    GetPhysicalDeviceQueueFamilyProperties(physical, &count, NULL);
    if (count == 0 || count > 32) {
        event("radv.queues.unsupported_count", count > INT_MAX ? -1 : (int)count);
        return 1;
    }
    VkQueueFamilyProperties queues[32];
    GetPhysicalDeviceQueueFamilyProperties(physical, &count, queues);
    if (count > 32) {
        event("radv.queues.overflow", 0);
        return 1;
    }
    uint32_t queue = echo_radv_graphics_queue(queues, count);
    if (event("radv.queues.end", (int)count) != 0) return 1;
    if (queue == UINT32_MAX) {
        event("radv.queues.no_graphics", 0);
        return 1;
    }
    if (event("radv.queue.graphics", (int)queue) != 0) return 1;

    VkExtensionProperties extensions[512];
    count = 512;
    if (event("radv.device.extensions.begin", 0) != 0) return 1;
    VkResult result = EnumerateDeviceExtensionProperties(physical, NULL, &count, extensions);
    if (event("radv.device.extensions.end", result) != 0 || result != VK_SUCCESS) return 1;
    if (count > 512) {
        event("radv.device.extensions.overflow", 0);
        return 1;
    }
    if (!echo_radv_has_extension(extensions, count, VK_KHR_SWAPCHAIN_EXTENSION_NAME)) {
        event("radv.device.swapchain.unsupported", 0);
        return 1;
    }
    if (event("radv.device.swapchain.supported", 0) != 0) return 1;

    VkDisplayPropertiesKHR displays[8];
    count = 8;
    if (event("radv.displays.begin", 0) != 0) return 1;
    result = GetPhysicalDeviceDisplayPropertiesKHR(physical, &count, displays);
    if (event("radv.displays.end", result) != 0 || result != VK_SUCCESS) return 1;
    if (count > 8) {
        event("radv.displays.overflow", 0);
        return 1;
    }
    if (count == 0 || displays[0].display == VK_NULL_HANDLE) {
        event("radv.displays.none", 0);
        return 1;
    }
    if (event("radv.displays.count", (int)count) != 0) return 1;
    VkDisplayModePropertiesKHR modes[32];
    count = 32;
    if (event("radv.modes.begin", 0) != 0) return 1;
    result = GetDisplayModePropertiesKHR(physical, displays[0].display, &count, modes);
    if (event("radv.modes.end", result) != 0 || result != VK_SUCCESS) return 1;
    if (count > 32) {
        event("radv.modes.overflow", 0);
        return 1;
    }
    uint32_t selected = echo_radv_display_mode(modes, count);
    if (selected == UINT32_MAX) {
        event("radv.modes.unsupported", (int)count);
        return 1;
    }
    VkDisplayModeParametersKHR mode = modes[selected].parameters;
    if (mode.visibleRegion.width > INT_MAX || mode.visibleRegion.height > INT_MAX || mode.refreshRate > INT_MAX) {
        event("radv.modes.overflow", 0);
        return 1;
    }
    if (event("radv.mode.width", (int)mode.visibleRegion.width) != 0 ||
        event("radv.mode.height", (int)mode.visibleRegion.height) != 0 ||
        event("radv.mode.refresh_millihz", (int)mode.refreshRate) != 0) return 1;

    float priority = 1.0f;
    VkDeviceQueueCreateInfo queue_info = { .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = queue, .queueCount = 1, .pQueuePriorities = &priority };
    const char *device_extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    VkDeviceCreateInfo device_info = { .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = 1, .pQueueCreateInfos = &queue_info,
        .enabledExtensionCount = 1, .ppEnabledExtensionNames = device_extensions };
    VkDevice device = VK_NULL_HANDLE;
    if (event("radv.device.create.begin", 0) != 0) return 1;
    result = CreateDevice(physical, &device_info, NULL, &device);
    int logged = event("radv.device.create.end", result);
    if (result != VK_SUCCESS) return 1;
    if (device == VK_NULL_HANDLE) {
        event("radv.device.create.null", 0);
        return 1;
    }
    PFN_vkDestroyDevice destroy = (PFN_vkDestroyDevice)GetDeviceProcAddr(device, "vkDestroyDevice");
    PFN_vkGetDeviceQueue get_queue = (PFN_vkGetDeviceQueue)GetDeviceProcAddr(device, "vkGetDeviceQueue");
    if (!destroy) {
        event("radv.device.destroy.missing_function", 0);
        return 1;
    }
    int status = 1;
    if (!get_queue) {
        event("radv.device.queue.missing_function", 0);
    } else if (logged == 0 && event("radv.device.queue.begin", 0) == 0) {
        VkQueue handle = VK_NULL_HANDLE;
        get_queue(device, queue, 0, &handle);
        if (event("radv.device.queue.end", handle == VK_NULL_HANDLE ? 1 : 0) == 0 && handle != VK_NULL_HANDLE) {
#ifdef ECHO_RADV_FRAME_PROBE
            status = echo_radv_first_frame(instance, physical, device, handle, queue, displays[0], modes[selected], event);
            if (status != ECHO_RADV_FRAME_FAILED) return status;
#else
            status = 0;
#endif
        }
    }
    if (event("radv.device.destroy.begin", 0) != 0) status = 1;
    destroy(device, NULL);
    if (event("radv.device.destroy.end", 0) != 0) status = 1;
    return status;
}
