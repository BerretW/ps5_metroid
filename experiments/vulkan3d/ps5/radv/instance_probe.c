#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/stat.h>

#ifdef ECHO_RADV_DISPLAY_PROBE
#include "display_probe.h"
#ifdef ECHO_RADV_FRAME_PROBE
#include "frame_probe.h"
#ifdef ECHO_RADV_CUBE_PROBE
#ifdef ECHO_RADV_CUBE_INTERACTIVE
#define ECHO_RADV_LOG "/data/echo-vulkan-lab/radv-cube-input.log"
#else
#define ECHO_RADV_LOG "/data/echo-vulkan-lab/radv-cube.log"
#endif
#else
#define ECHO_RADV_LOG "/data/echo-vulkan-lab/radv-frame.log"
#endif
#else
#define ECHO_RADV_LOG "/data/echo-vulkan-lab/radv-display.log"
#endif
#else
#define ECHO_RADV_LOG "/data/echo-vulkan-lab/radv-instance.log"
#endif

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
vk_icdGetInstanceProcAddr(VkInstance instance, const char *name);

int sceKernelDebugOutText(int channel, const char *text);
int sceKernelOpen(const char *path, int flags, unsigned int mode);
int64_t sceKernelWrite(int descriptor, const void *buffer, size_t size);
int sceKernelFsync(int descriptor);
int sceKernelClose(int descriptor);
int sceKernelUsleep(uint32_t microseconds);

__attribute__((visibility("hidden"))) void *__dso_handle = &__dso_handle;

static int event(const char *stage, int code)
{
    char line[160];
    int size = snprintf(line, sizeof(line), "{\"stage\":\"%s\",\"code\":%d}\n", stage, code);
    if (size < 0 || (size_t)size >= sizeof(line)) {
        sceKernelDebugOutText(0, "ECHO RADV: event formatting failed");
        return -1;
    }
    sceKernelDebugOutText(0, line);
    int fd = sceKernelOpen(ECHO_RADV_LOG, O_WRONLY | O_CREAT | O_APPEND, 0666);
    if (fd < 0) {
        sceKernelDebugOutText(0, "ECHO RADV: log open failed");
        return -1;
    }
    size_t written = 0;
    int failed = 0;
    while (written < (size_t)size) {
        int64_t n = sceKernelWrite(fd, line + written, (size_t)size - written);
        if (n <= 0 || (uint64_t)n > (size_t)size - written) {
            sceKernelDebugOutText(0, "ECHO RADV: log write failed");
            failed = 1;
            break;
        }
        written += (size_t)n;
    }
    if (sceKernelFsync(fd) != 0) {
        sceKernelDebugOutText(0, "ECHO RADV: log sync failed");
        failed = 1;
    }
    if (sceKernelClose(fd) != 0) {
        sceKernelDebugOutText(0, "ECHO RADV: log close failed");
        failed = 1;
    }
    return failed ? -1 : 0;
}

static int run_probe(void)
{
    if (event("radv.icd.get_create.begin", 0) != 0) return 1;
    PFN_vkCreateInstance create =
        (PFN_vkCreateInstance)vk_icdGetInstanceProcAddr(VK_NULL_HANDLE, "vkCreateInstance");
    if (!create) {
        event("radv.icd.get_create.missing", 0);
        return 1;
    }
#ifdef ECHO_RADV_DISPLAY_PROBE
    if (echo_radv_check_display_extensions(event) != 0) return 1;
    const char *extensions[] = {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_DISPLAY_EXTENSION_NAME};
#endif
    if (event("radv.instance.create.begin", 0) != 0) return 1;
    VkApplicationInfo application = { .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "ECHO RADV instance probe", .apiVersion = VK_API_VERSION_1_0 };
    VkInstanceCreateInfo info = { .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &application
#ifdef ECHO_RADV_DISPLAY_PROBE
        , .enabledExtensionCount = 2, .ppEnabledExtensionNames = extensions
#endif
    };
    VkInstance instance = VK_NULL_HANDLE;
    VkResult result = create(&info, NULL, &instance);
    int create_logged = event("radv.instance.create.end", result);
    if (result != VK_SUCCESS) return 1;
    if (instance == VK_NULL_HANDLE) {
        event("radv.instance.create.null", 0);
        return 1;
    }

    PFN_vkEnumeratePhysicalDevices enumerate =
        (PFN_vkEnumeratePhysicalDevices)vk_icdGetInstanceProcAddr(instance, "vkEnumeratePhysicalDevices");
    PFN_vkDestroyInstance destroy =
        (PFN_vkDestroyInstance)vk_icdGetInstanceProcAddr(instance, "vkDestroyInstance");
    if (!destroy) {
        event("radv.icd.instance_function.missing", 0);
        return 1;
    }
    int status = 1;
    if (create_logged != 0) goto cleanup;
    if (!enumerate) {
        event("radv.icd.instance_function.missing", 0);
        goto cleanup;
    }
    if (event("radv.physical.enumerate.begin", 0) != 0) goto cleanup;
    uint32_t count = 0;
    result = enumerate(instance, &count, NULL);
    if (event("radv.physical.enumerate.end", result) != 0 || result != VK_SUCCESS) goto cleanup;
    if (count > INT_MAX) {
        event("radv.physical.count.overflow", 0);
        goto cleanup;
    }
    if (event("radv.physical.count", (int)count) == 0 && count != 0) {
#ifdef ECHO_RADV_DISPLAY_PROBE
        VkPhysicalDevice physical = VK_NULL_HANDLE;
        uint32_t capacity = 1;
        if (event("radv.physical.handle.begin", 0) != 0) goto cleanup;
        result = enumerate(instance, &capacity, &physical);
        if (event("radv.physical.handle.end", result) != 0 ||
            (result != VK_SUCCESS && result != VK_INCOMPLETE)) goto cleanup;
        if (capacity != 1 || physical == VK_NULL_HANDLE) {
            event("radv.physical.handle.invalid", 0);
            goto cleanup;
        }
        status = echo_radv_probe_display(instance, physical, event);
#ifdef ECHO_RADV_FRAME_PROBE
        if (status != ECHO_RADV_FRAME_FAILED) return status;
#endif
#else
        status = 0;
#endif
    }
cleanup:
    if (event("radv.instance.destroy.begin", 0) != 0) status = 1;
    destroy(instance, NULL);
    if (event("radv.instance.destroy.end", 0) != 0) status = 1;
    return status;
}

int main(void)
{
    if (mkdir("/data/echo-vulkan-lab", 0777) != 0 && errno != EEXIST) {
        sceKernelDebugOutText(0, "ECHO RADV: log directory creation failed");
        return 1;
    }
    int fd = sceKernelOpen(ECHO_RADV_LOG, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0 || sceKernelClose(fd) != 0) {
        sceKernelDebugOutText(0, "ECHO RADV: log initialization failed");
        return 1;
    }
    int result = event("radv.start", 0) == 0 ? run_probe() : 1;
    event("radv.hold.start", result);
    for (int heartbeat = 1;; heartbeat = heartbeat == 12 ? 1 : heartbeat + 1) {
        int sleep_result = sceKernelUsleep(5000000);
        if (sleep_result != 0) {
            event("radv.hold.sleep_failed", sleep_result);
            return 1;
        }
        event("radv.hold.heartbeat", heartbeat);
    }
}
