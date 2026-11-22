#define VK_NO_PROTOTYPES
#include "frame_probe.h"
#ifdef ECHO_RADV_CUBE_PROBE
#include "cube_probe.h"
#endif
#ifdef ECHO_RADV_CUBE_INTERACTIVE
#include "cube_loop.h"
int echo_radv_cube_loop(EchoRadvCube *cube, VkDevice device, PFN_vkGetDeviceProcAddr get_proc,
                        VkQueue queue, VkSwapchainKHR swapchain, VkCommandPool pool,
                        VkCommandBuffer command, VkFence fence, VkSemaphore acquired,
                        uint32_t image_count, EchoRadvEvent event)
{
    (void)cube; (void)device; (void)get_proc; (void)queue; (void)swapchain; (void)pool;
    (void)command; (void)fence; (void)acquired; (void)image_count; (void)event;
    return 1;
}
#endif

#include <stdio.h>
#include <string.h>

#define HANDLE(type, n) ((type)(uintptr_t)(n))
static int scenario, bad, destroyed, submits, presents, clears, semaphore_sequence;
#ifdef ECHO_RADV_CUBE_PROBE
static int cube_destroyed;
int echo_radv_cube_create(EchoRadvCube *cube, VkDevice device, PFN_vkGetDeviceProcAddr get_proc,
                          VkFormat format, VkExtent2D extent, const VkImage *images, uint32_t count,
                          EchoRadvEvent event)
{
    (void)device; (void)get_proc; (void)format; (void)extent; (void)images; (void)event;
    cube->count = count;
    return 0;
}
int echo_radv_cube_record(EchoRadvCube *cube, VkCommandBuffer command, uint32_t index, EchoRadvEvent event)
{
    (void)command; (void)event;
    if (index >= cube->count) bad = 1;
    if (scenario == 6) return 1;
    ++clears;
    return 0;
}
void echo_radv_cube_destroy(EchoRadvCube *cube) { (void)cube; ++cube_destroyed; }
#endif
static int record(const char *stage, int code)
{
    (void)code;
    return (scenario == 3 && strcmp(stage, "radv.frame.acquire.end") == 0) ||
           (scenario == 5 && strcmp(stage, "radv.frame.semaphore.acquire.end") == 0) ? -1 : 0;
}
static VkResult VKAPI_CALL planes(VkPhysicalDevice p, uint32_t *count, VkDisplayPlanePropertiesKHR *out)
{
    *count = 1;
    out[0] = (VkDisplayPlanePropertiesKHR){HANDLE(VkDisplayKHR, 1), 0};
    return VK_SUCCESS;
}
static VkResult VKAPI_CALL supported(VkPhysicalDevice p, uint32_t plane, uint32_t *count, VkDisplayKHR *out)
{
    *count = 1;
    out[0] = HANDLE(VkDisplayKHR, 1);
    return VK_SUCCESS;
}
static VkResult VKAPI_CALL plane_caps(VkPhysicalDevice p, VkDisplayModeKHR m, uint32_t plane,
                                      VkDisplayPlaneCapabilitiesKHR *out)
{
    memset(out, 0, sizeof(*out));
    out->supportedAlpha = VK_DISPLAY_PLANE_ALPHA_OPAQUE_BIT_KHR;
    out->minSrcExtent = out->maxSrcExtent = out->minDstExtent = out->maxDstExtent = (VkExtent2D){1920, 1080};
    return VK_SUCCESS;
}
static VkResult VKAPI_CALL surface_create(VkInstance i, const VkDisplaySurfaceCreateInfoKHR *info,
                                          const VkAllocationCallbacks *a, VkSurfaceKHR *out)
{
    if (info->planeIndex != 0 || info->imageExtent.width != 1920 ||
        info->alphaMode != VK_DISPLAY_PLANE_ALPHA_OPAQUE_BIT_KHR) bad = 1;
    *out = HANDLE(VkSurfaceKHR, 2);
    return VK_SUCCESS;
}
static void VKAPI_CALL surface_destroy(VkInstance i, VkSurfaceKHR s, const VkAllocationCallbacks *a) { ++destroyed; }
static VkResult VKAPI_CALL surface_support(VkPhysicalDevice p, uint32_t f, VkSurfaceKHR s, VkBool32 *out)
{
    *out = VK_TRUE;
    return VK_SUCCESS;
}
static VkResult VKAPI_CALL surface_caps(VkPhysicalDevice p, VkSurfaceKHR s, VkSurfaceCapabilitiesKHR *out)
{
    memset(out, 0, sizeof(*out));
    out->minImageCount = 2;
    out->maxImageCount = scenario == 4 ? 2 : 5;
    out->currentExtent = out->minImageExtent = out->maxImageExtent = (VkExtent2D){1920, 1080};
    out->maxImageArrayLayers = 1;
    out->supportedTransforms = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    out->supportedCompositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    out->supportedUsageFlags = scenario == 1 ? 0 :
#ifdef ECHO_RADV_CUBE_PROBE
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
#else
        VK_IMAGE_USAGE_TRANSFER_DST_BIT;
#endif
    return VK_SUCCESS;
}
static VkResult VKAPI_CALL formats(VkPhysicalDevice p, VkSurfaceKHR s, uint32_t *count, VkSurfaceFormatKHR *out)
{
    *count = 1;
    out[0] = (VkSurfaceFormatKHR){VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};
    return VK_SUCCESS;
}
static VkResult VKAPI_CALL modes(VkPhysicalDevice p, VkSurfaceKHR s, uint32_t *count, VkPresentModeKHR *out)
{
    *count = 1;
    out[0] = VK_PRESENT_MODE_FIFO_KHR;
    return VK_SUCCESS;
}
static VkResult VKAPI_CALL swap_create(VkDevice d, const VkSwapchainCreateInfoKHR *info,
                                       const VkAllocationCallbacks *a, VkSwapchainKHR *out)
{
    if (info->minImageCount != (scenario == 4 ? 2u : 3u) ||
#ifdef ECHO_RADV_CUBE_PROBE
        info->imageUsage != VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT ||
#else
        info->imageUsage != VK_IMAGE_USAGE_TRANSFER_DST_BIT ||
#endif
        info->presentMode != VK_PRESENT_MODE_FIFO_KHR) bad = 1;
    *out = HANDLE(VkSwapchainKHR, 3);
    return VK_SUCCESS;
}
static void VKAPI_CALL swap_destroy(VkDevice d, VkSwapchainKHR s, const VkAllocationCallbacks *a) { ++destroyed; }
static VkResult VKAPI_CALL images(VkDevice d, VkSwapchainKHR s, uint32_t *count, VkImage *out)
{
    *count = scenario == 4 ? 2 : 3;
    for (uint32_t i = 0; i < *count; ++i) out[i] = HANDLE(VkImage, i + 10);
    return VK_SUCCESS;
}
static VkResult VKAPI_CALL acquire(VkDevice d, VkSwapchainKHR s, uint64_t timeout, VkSemaphore sem,
                                   VkFence fence, uint32_t *index)
{
    if (!timeout || sem != HANDLE(VkSemaphore, 5) || fence != VK_NULL_HANDLE) bad = 1;
    *index = 1;
    return VK_SUCCESS;
}
static VkResult VKAPI_CALL present(VkQueue q, const VkPresentInfoKHR *info)
{
    ++presents;
    if (info->waitSemaphoreCount != 1 || info->pWaitSemaphores[0] != HANDLE(VkSemaphore, 6) ||
        info->pImageIndices[0] != 1) bad = 1;
    return VK_SUCCESS;
}
static VkResult VKAPI_CALL pool_create(VkDevice d, const VkCommandPoolCreateInfo *info,
                                       const VkAllocationCallbacks *a, VkCommandPool *out)
{
    *out = HANDLE(VkCommandPool, 4);
    return VK_SUCCESS;
}
static void VKAPI_CALL pool_destroy(VkDevice d, VkCommandPool p, const VkAllocationCallbacks *a) { ++destroyed; }
static VkResult VKAPI_CALL alloc(VkDevice d, const VkCommandBufferAllocateInfo *info, VkCommandBuffer *out)
{
    *out = HANDLE(VkCommandBuffer, 8);
    return VK_SUCCESS;
}
static VkResult VKAPI_CALL begin(VkCommandBuffer c, const VkCommandBufferBeginInfo *info) { return VK_SUCCESS; }
static VkResult VKAPI_CALL end(VkCommandBuffer c) { return VK_SUCCESS; }
static void VKAPI_CALL barrier(VkCommandBuffer c, VkPipelineStageFlags src, VkPipelineStageFlags dst,
                               VkDependencyFlags flags, uint32_t memory_count, const VkMemoryBarrier *memory,
                               uint32_t buffer_count, const VkBufferMemoryBarrier *buffers,
                               uint32_t image_count, const VkImageMemoryBarrier *image)
{
    if (image_count != 1 || image->image != HANDLE(VkImage, 11) ||
        (clears == 0 && (image->oldLayout != VK_IMAGE_LAYOUT_UNDEFINED ||
                         image->newLayout != VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)) ||
        (clears == 1 && (image->oldLayout != VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL ||
                         image->newLayout != VK_IMAGE_LAYOUT_PRESENT_SRC_KHR))) bad = 1;
}
static void VKAPI_CALL clear(VkCommandBuffer c, VkImage i, VkImageLayout layout, const VkClearColorValue *color,
                             uint32_t count, const VkImageSubresourceRange *ranges)
{
    ++clears;
    if (i != HANDLE(VkImage, 11) || layout != VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL ||
        count != 1 || color->float32[2] <= color->float32[0]) bad = 1;
}
static VkResult VKAPI_CALL sem_create(VkDevice d, const VkSemaphoreCreateInfo *info,
                                      const VkAllocationCallbacks *a, VkSemaphore *out)
{
    *out = HANDLE(VkSemaphore, 5 + semaphore_sequence++ % 2);
    return VK_SUCCESS;
}
static void VKAPI_CALL sem_destroy(VkDevice d, VkSemaphore s, const VkAllocationCallbacks *a) { ++destroyed; }
static VkResult VKAPI_CALL fence_create(VkDevice d, const VkFenceCreateInfo *info,
                                        const VkAllocationCallbacks *a, VkFence *out)
{
    *out = HANDLE(VkFence, 7);
    return VK_SUCCESS;
}
static void VKAPI_CALL fence_destroy(VkDevice d, VkFence f, const VkAllocationCallbacks *a) { ++destroyed; }
static VkResult VKAPI_CALL submit(VkQueue q, uint32_t count, const VkSubmitInfo *info, VkFence f)
{
    ++submits;
    if (count != 1 || info->waitSemaphoreCount != 1 || info->pWaitSemaphores[0] != HANDLE(VkSemaphore, 5) ||
        info->pWaitDstStageMask[0] !=
#ifdef ECHO_RADV_CUBE_PROBE
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT ||
#else
        VK_PIPELINE_STAGE_TRANSFER_BIT ||
#endif
        info->signalSemaphoreCount != 1 || info->pSignalSemaphores[0] != HANDLE(VkSemaphore, 6) ||
        f != HANDLE(VkFence, 7)) bad = 1;
    return VK_SUCCESS;
}
static VkResult VKAPI_CALL wait_fence(VkDevice d, uint32_t count, const VkFence *f, VkBool32 all, uint64_t timeout)
{
    if (!timeout || count != 1 || f[0] != HANDLE(VkFence, 7) || !all) bad = 1;
    return scenario == 2 ? VK_TIMEOUT : VK_SUCCESS;
}
static PFN_vkVoidFunction VKAPI_CALL device_proc(VkDevice d, const char *name)
{
#define MATCH(key, fn) if (strcmp(name, "vk" key) == 0) return (PFN_vkVoidFunction)fn
    MATCH("CreateSwapchainKHR", swap_create); MATCH("DestroySwapchainKHR", swap_destroy);
    MATCH("GetSwapchainImagesKHR", images); MATCH("AcquireNextImageKHR", acquire); MATCH("QueuePresentKHR", present);
    MATCH("CreateCommandPool", pool_create); MATCH("DestroyCommandPool", pool_destroy);
    MATCH("AllocateCommandBuffers", alloc); MATCH("BeginCommandBuffer", begin); MATCH("EndCommandBuffer", end);
    MATCH("CmdPipelineBarrier", barrier); MATCH("CmdClearColorImage", clear);
    MATCH("CreateSemaphore", sem_create); MATCH("DestroySemaphore", sem_destroy);
    MATCH("CreateFence", fence_create); MATCH("DestroyFence", fence_destroy);
    MATCH("QueueSubmit", submit); MATCH("WaitForFences", wait_fence);
#undef MATCH
    return NULL;
}
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vk_icdGetInstanceProcAddr(VkInstance i, const char *name)
{
#define MATCH(key, fn) if (strcmp(name, "vk" key) == 0) return (PFN_vkVoidFunction)fn
    MATCH("GetPhysicalDeviceDisplayPlanePropertiesKHR", planes);
    MATCH("GetDisplayPlaneSupportedDisplaysKHR", supported); MATCH("GetDisplayPlaneCapabilitiesKHR", plane_caps);
    MATCH("CreateDisplayPlaneSurfaceKHR", surface_create); MATCH("DestroySurfaceKHR", surface_destroy);
    MATCH("GetPhysicalDeviceSurfaceSupportKHR", surface_support);
    MATCH("GetPhysicalDeviceSurfaceCapabilitiesKHR", surface_caps);
    MATCH("GetPhysicalDeviceSurfaceFormatsKHR", formats); MATCH("GetPhysicalDeviceSurfacePresentModesKHR", modes);
    MATCH("GetDeviceProcAddr", device_proc);
#undef MATCH
    return NULL;
}
int main(void)
{
    VkDisplayPropertiesKHR display = { .display = HANDLE(VkDisplayKHR, 1),
        .supportedTransforms = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR };
    VkDisplayModePropertiesKHR mode = { .displayMode = HANDLE(VkDisplayModeKHR, 1),
        .parameters = { .visibleRegion = {1920, 1080}, .refreshRate = 59940 } };
    const int expected[] = {
#ifdef ECHO_RADV_CUBE_INTERACTIVE
                            ECHO_RADV_FRAME_HOLD_FAILED,
#else
                            ECHO_RADV_FRAME_PRESENTED,
#endif
                            ECHO_RADV_FRAME_FAILED,
                            ECHO_RADV_FRAME_HOLD_FAILED, ECHO_RADV_FRAME_HOLD_FAILED,
#ifdef ECHO_RADV_CUBE_INTERACTIVE
                            ECHO_RADV_FRAME_HOLD_FAILED,
#else
                            ECHO_RADV_FRAME_PRESENTED,
#endif
                            ECHO_RADV_FRAME_FAILED, ECHO_RADV_FRAME_HOLD_FAILED};
#ifdef ECHO_RADV_CUBE_PROBE
    const int scenario_count = 7;
#else
    const int scenario_count = 5;
#endif
    for (scenario = 0; scenario < scenario_count; ++scenario) {
        bad = destroyed = submits = presents = clears = semaphore_sequence = 0;
#ifdef ECHO_RADV_CUBE_PROBE
        cube_destroyed = 0;
#endif
        int result = echo_radv_first_frame(HANDLE(VkInstance, 1), HANDLE(VkPhysicalDevice, 1),
            HANDLE(VkDevice, 1), HANDLE(VkQueue, 1), 0, display, mode, record);
        if (result != expected[scenario] || bad ||
            ((scenario == 0 || scenario == 4) && (submits != 1 || presents != 1 || clears != 1 || destroyed != 0)) ||
            (scenario == 1 && (destroyed != 1 || submits != 0)) ||
            (scenario == 2 && (submits != 1 || presents != 0 || destroyed != 0)) ||
            (scenario == 3 && (submits != 0 || destroyed != 0)) ||
            (scenario == 5 && (submits != 0 || destroyed != 4)) ||
            (scenario == 6 && (submits != 0 || destroyed != 0))
#ifdef ECHO_RADV_CUBE_PROBE
            || (scenario == 5 ? cube_destroyed != 1 : cube_destroyed != 0)
#endif
            ) {
            fprintf(stderr, "frame scenario %d failed: result=%d bad=%d destroyed=%d submit=%d present=%d clear=%d\n",
                    scenario, result, bad, destroyed, submits, presents, clears);
            return 1;
        }
    }
    puts("RADV first-frame mock tests PASS");
    return 0;
}
