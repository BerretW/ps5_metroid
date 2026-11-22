#define VK_NO_PROTOTYPES
#include "display_probe.h"
#include "display_selection.h"
#ifdef ECHO_RADV_FRAME_PROBE
#include "frame_probe.h"
static int frame_outcome;
int echo_radv_first_frame(VkInstance instance, VkPhysicalDevice physical, VkDevice device,
                          VkQueue queue, uint32_t family, VkDisplayPropertiesKHR display,
                          VkDisplayModePropertiesKHR mode, EchoRadvEvent event)
{
    (void)instance; (void)physical; (void)device; (void)queue; (void)family;
    (void)display; (void)mode; (void)event;
    return frame_outcome;
}
#endif

#include <stdio.h>
#include <string.h>

static int scenario;
static int destroyed;
static int width;
static int height;
static int refresh;

static int record(const char *stage, int code)
{
    if (strcmp(stage, "radv.mode.width") == 0) width = code;
    if (strcmp(stage, "radv.mode.height") == 0) height = code;
    if (strcmp(stage, "radv.mode.refresh_millihz") == 0) refresh = code;
    return scenario == 4 && strcmp(stage, "radv.device.create.end") == 0 ? -1 : 0;
}

static VkResult VKAPI_CALL instance_extensions(const char *layer, uint32_t *count, VkExtensionProperties *items)
{
    (void)layer;
    memset(items, 0, sizeof(*items) * 2);
    memcpy(items[0].extensionName, VK_KHR_SURFACE_EXTENSION_NAME, sizeof(VK_KHR_SURFACE_EXTENSION_NAME));
    if (scenario != 1)
        memcpy(items[1].extensionName, VK_KHR_DISPLAY_EXTENSION_NAME, sizeof(VK_KHR_DISPLAY_EXTENSION_NAME));
    *count = 2;
    return VK_SUCCESS;
}

static void VKAPI_CALL properties(VkPhysicalDevice physical, VkPhysicalDeviceProperties *out)
{
    (void)physical;
    memset(out, 0, sizeof(*out));
    out->apiVersion = VK_API_VERSION_1_0;
    out->vendorID = 0x1002;
}

static void VKAPI_CALL queues(VkPhysicalDevice physical, uint32_t *count, VkQueueFamilyProperties *out)
{
    (void)physical;
    *count = 2;
    if (out) {
        memset(out, 0, sizeof(*out) * 2);
        out[0].queueFlags = VK_QUEUE_GRAPHICS_BIT;
        out[1].queueFlags = VK_QUEUE_GRAPHICS_BIT;
        out[1].queueCount = scenario == 2 ? 0 : 1;
    }
}

static VkResult VKAPI_CALL device_extensions(VkPhysicalDevice physical, const char *layer, uint32_t *count,
                                            VkExtensionProperties *out)
{
    (void)physical;
    (void)layer;
    memset(out, 0, sizeof(*out));
    memcpy(out->extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME, sizeof(VK_KHR_SWAPCHAIN_EXTENSION_NAME));
    *count = 1;
    return VK_SUCCESS;
}

static VkResult VKAPI_CALL displays(VkPhysicalDevice physical, uint32_t *count, VkDisplayPropertiesKHR *out)
{
    (void)physical;
    memset(out, 0, sizeof(*out));
    out->display = (VkDisplayKHR)(uintptr_t)1;
    *count = 1;
    return VK_SUCCESS;
}

static VkResult VKAPI_CALL modes(VkPhysicalDevice physical, VkDisplayKHR display, uint32_t *count,
                                 VkDisplayModePropertiesKHR *out)
{
    (void)physical;
    (void)display;
    memset(out, 0, sizeof(*out) * 3);
    for (uint32_t i = 0; i < 3; ++i) out[i].displayMode = (VkDisplayModeKHR)(uintptr_t)(i + 1);
    out[0].parameters.visibleRegion = (VkExtent2D){3840, 2160};
    out[0].parameters.refreshRate = 59940;
    out[1].parameters.visibleRegion = (VkExtent2D){1920, 1080};
    out[1].parameters.refreshRate = 59940;
    out[2].parameters.visibleRegion = (VkExtent2D){1920, 1080};
    out[2].parameters.refreshRate = scenario == 3 ? 0 : 119880;
    if (scenario == 3) {
        out[0].parameters.visibleRegion = (VkExtent2D){640, 480};
        out[1].parameters.visibleRegion = (VkExtent2D){640, 480};
        out[2].parameters.visibleRegion = (VkExtent2D){640, 480};
    }
    *count = 3;
    return VK_SUCCESS;
}

static VkResult VKAPI_CALL create_device(VkPhysicalDevice physical, const VkDeviceCreateInfo *info,
                                         const VkAllocationCallbacks *allocator, VkDevice *out)
{
    (void)physical;
    (void)allocator;
    if (info->queueCreateInfoCount != 1 || info->pQueueCreateInfos[0].queueFamilyIndex != 1 ||
        info->enabledExtensionCount != 1 ||
        strcmp(info->ppEnabledExtensionNames[0], VK_KHR_SWAPCHAIN_EXTENSION_NAME) != 0)
        return VK_ERROR_INITIALIZATION_FAILED;
    *out = (VkDevice)(uintptr_t)1;
    return VK_SUCCESS;
}

static void VKAPI_CALL destroy_device(VkDevice device, const VkAllocationCallbacks *allocator)
{
    (void)device;
    (void)allocator;
    ++destroyed;
}

static void VKAPI_CALL get_queue(VkDevice device, uint32_t family, uint32_t index, VkQueue *out)
{
    (void)device;
    *out = family == 1 && index == 0 ? (VkQueue)(uintptr_t)1 : VK_NULL_HANDLE;
}

static PFN_vkVoidFunction VKAPI_CALL get_device_proc(VkDevice device, const char *name)
{
    (void)device;
    if (strcmp(name, "vkDestroyDevice") == 0) return (PFN_vkVoidFunction)destroy_device;
    if (strcmp(name, "vkGetDeviceQueue") == 0) return (PFN_vkVoidFunction)get_queue;
    return NULL;
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vk_icdGetInstanceProcAddr(VkInstance instance, const char *name)
{
    (void)instance;
#define MATCH(key, fn) if (strcmp(name, "vk" key) == 0) return (PFN_vkVoidFunction)fn
    MATCH("EnumerateInstanceExtensionProperties", instance_extensions);
    MATCH("GetPhysicalDeviceProperties", properties);
    MATCH("GetPhysicalDeviceQueueFamilyProperties", queues);
    MATCH("EnumerateDeviceExtensionProperties", device_extensions);
    MATCH("GetPhysicalDeviceDisplayPropertiesKHR", displays);
    MATCH("GetDisplayModePropertiesKHR", modes);
    MATCH("CreateDevice", create_device);
    MATCH("GetDeviceProcAddr", get_device_proc);
#undef MATCH
    return NULL;
}

int main(void)
{
    VkInstance instance = (VkInstance)(uintptr_t)1;
    VkPhysicalDevice physical = (VkPhysicalDevice)(uintptr_t)1;
#ifdef ECHO_RADV_FRAME_PROBE
    frame_outcome = ECHO_RADV_FRAME_PRESENTED;
    if (echo_radv_probe_display(instance, physical, record) != ECHO_RADV_FRAME_PRESENTED || destroyed != 0) return 1;
    frame_outcome = ECHO_RADV_FRAME_FAILED;
    if (echo_radv_probe_display(instance, physical, record) != ECHO_RADV_FRAME_FAILED || destroyed != 1) return 2;
    frame_outcome = ECHO_RADV_FRAME_HOLD_FAILED;
    if (echo_radv_probe_display(instance, physical, record) != ECHO_RADV_FRAME_HOLD_FAILED || destroyed != 1) return 3;
#else
    if (echo_radv_check_display_extensions(record) != 0 ||
        echo_radv_probe_display(instance, physical, record) != 0 ||
        destroyed != 1 || width != 1920 || height != 1080 || refresh != 59940) {
        fputs("valid display/device capability probe failed\n", stderr);
        return 1;
    }
    scenario = 1;
    if (echo_radv_check_display_extensions(record) == 0) return 2;
    scenario = 2;
    if (echo_radv_probe_display(instance, physical, record) == 0 || destroyed != 1) return 3;
    scenario = 3;
    if (echo_radv_probe_display(instance, physical, record) == 0 || destroyed != 1) return 4;
    scenario = 4;
    if (echo_radv_probe_display(instance, physical, record) == 0 || destroyed != 2) return 5;
#endif
    return 0;
}
