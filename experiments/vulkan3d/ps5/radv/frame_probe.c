#define VK_NO_PROTOTYPES
#include "frame_probe.h"
#ifdef ECHO_RADV_CUBE_PROBE
#include "cube_probe.h"
#endif
#ifdef ECHO_RADV_CUBE_INTERACTIVE
#include "cube_loop.h"
#endif

#include <stdint.h>

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
vk_icdGetInstanceProcAddr(VkInstance instance, const char *name);

int echo_radv_first_frame(VkInstance instance, VkPhysicalDevice physical, VkDevice device,
                          VkQueue queue, uint32_t family, VkDisplayPropertiesKHR display,
                          VkDisplayModePropertiesKHR mode, EchoRadvEvent event)
{
#define LOAD_I(type, name) type name = (type)vk_icdGetInstanceProcAddr(instance, "vk" #name)
    LOAD_I(PFN_vkGetPhysicalDeviceDisplayPlanePropertiesKHR, GetPhysicalDeviceDisplayPlanePropertiesKHR);
    LOAD_I(PFN_vkGetDisplayPlaneSupportedDisplaysKHR, GetDisplayPlaneSupportedDisplaysKHR);
    LOAD_I(PFN_vkGetDisplayPlaneCapabilitiesKHR, GetDisplayPlaneCapabilitiesKHR);
    LOAD_I(PFN_vkCreateDisplayPlaneSurfaceKHR, CreateDisplayPlaneSurfaceKHR);
    LOAD_I(PFN_vkDestroySurfaceKHR, DestroySurfaceKHR);
    LOAD_I(PFN_vkGetPhysicalDeviceSurfaceSupportKHR, GetPhysicalDeviceSurfaceSupportKHR);
    LOAD_I(PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR, GetPhysicalDeviceSurfaceCapabilitiesKHR);
    LOAD_I(PFN_vkGetPhysicalDeviceSurfaceFormatsKHR, GetPhysicalDeviceSurfaceFormatsKHR);
    LOAD_I(PFN_vkGetPhysicalDeviceSurfacePresentModesKHR, GetPhysicalDeviceSurfacePresentModesKHR);
    LOAD_I(PFN_vkGetDeviceProcAddr, GetDeviceProcAddr);
#undef LOAD_I
    if (!GetPhysicalDeviceDisplayPlanePropertiesKHR || !GetDisplayPlaneSupportedDisplaysKHR ||
        !GetDisplayPlaneCapabilitiesKHR || !CreateDisplayPlaneSurfaceKHR || !DestroySurfaceKHR ||
        !GetPhysicalDeviceSurfaceSupportKHR || !GetPhysicalDeviceSurfaceCapabilitiesKHR ||
        !GetPhysicalDeviceSurfaceFormatsKHR || !GetPhysicalDeviceSurfacePresentModesKHR || !GetDeviceProcAddr) {
        event("radv.frame.instance_function.missing", 0);
        return ECHO_RADV_FRAME_FAILED;
    }
#define LOAD_D(type, name) type name = (type)GetDeviceProcAddr(device, "vk" #name)
    LOAD_D(PFN_vkCreateSwapchainKHR, CreateSwapchainKHR);
    LOAD_D(PFN_vkDestroySwapchainKHR, DestroySwapchainKHR);
    LOAD_D(PFN_vkGetSwapchainImagesKHR, GetSwapchainImagesKHR);
    LOAD_D(PFN_vkAcquireNextImageKHR, AcquireNextImageKHR);
    LOAD_D(PFN_vkQueuePresentKHR, QueuePresentKHR);
    LOAD_D(PFN_vkCreateCommandPool, CreateCommandPool);
    LOAD_D(PFN_vkDestroyCommandPool, DestroyCommandPool);
    LOAD_D(PFN_vkAllocateCommandBuffers, AllocateCommandBuffers);
    LOAD_D(PFN_vkBeginCommandBuffer, BeginCommandBuffer);
    LOAD_D(PFN_vkEndCommandBuffer, EndCommandBuffer);
    LOAD_D(PFN_vkCmdPipelineBarrier, CmdPipelineBarrier);
    LOAD_D(PFN_vkCmdClearColorImage, CmdClearColorImage);
    LOAD_D(PFN_vkCreateSemaphore, CreateSemaphore);
    LOAD_D(PFN_vkDestroySemaphore, DestroySemaphore);
    LOAD_D(PFN_vkCreateFence, CreateFence);
    LOAD_D(PFN_vkDestroyFence, DestroyFence);
    LOAD_D(PFN_vkQueueSubmit, QueueSubmit);
    LOAD_D(PFN_vkWaitForFences, WaitForFences);
#undef LOAD_D
    if (!CreateSwapchainKHR || !DestroySwapchainKHR || !GetSwapchainImagesKHR || !AcquireNextImageKHR ||
        !QueuePresentKHR || !CreateCommandPool || !DestroyCommandPool || !AllocateCommandBuffers ||
        !BeginCommandBuffer || !EndCommandBuffer || !CmdPipelineBarrier || !CmdClearColorImage ||
        !CreateSemaphore || !DestroySemaphore || !CreateFence || !DestroyFence || !QueueSubmit || !WaitForFences) {
        event("radv.frame.device_function.missing", 0);
        return ECHO_RADV_FRAME_FAILED;
    }
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkCommandPool pool = VK_NULL_HANDLE;
    VkSemaphore acquired = VK_NULL_HANDLE, rendered = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
#ifdef ECHO_RADV_CUBE_PROBE
    EchoRadvCube cube = {0};
    int cube_ready = 0;
    const VkImageUsageFlags required_usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
#else
    const VkImageUsageFlags required_usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
#endif
    int status = ECHO_RADV_FRAME_FAILED;
    VkResult result;

    VkDisplayPlanePropertiesKHR planes[8];
    uint32_t count = 8;
    if (event("radv.frame.planes.begin", 0) != 0) goto cleanup;
    result = GetPhysicalDeviceDisplayPlanePropertiesKHR(physical, &count, planes);
    if (event("radv.frame.planes.end", result) != 0 || result != VK_SUCCESS) goto cleanup;
    if (count == 0 || count > 8 || !(display.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR)) {
        event("radv.frame.planes.unsupported", 0);
        goto cleanup;
    }
    uint32_t plane = UINT32_MAX;
    for (uint32_t i = 0; i < count; ++i) {
        if (planes[i].currentDisplay != VK_NULL_HANDLE && planes[i].currentDisplay != display.display) continue;
        VkDisplayKHR supported[8];
        uint32_t supported_count = 8;
        result = GetDisplayPlaneSupportedDisplaysKHR(physical, i, &supported_count, supported);
        if (result != VK_SUCCESS || supported_count > 8) {
            event("radv.frame.plane_displays.error", result);
            goto cleanup;
        }
        for (uint32_t j = 0; j < supported_count; ++j) {
            if (supported[j] == display.display) { plane = i; break; }
        }
        if (plane != UINT32_MAX) break;
    }
    if (plane == UINT32_MAX) {
        event("radv.frame.plane.unsupported", 0);
        goto cleanup;
    }
    VkDisplayPlaneCapabilitiesKHR plane_caps;
    if (event("radv.frame.plane_caps.begin", (int)plane) != 0) goto cleanup;
    result = GetDisplayPlaneCapabilitiesKHR(physical, mode.displayMode, plane, &plane_caps);
    if (event("radv.frame.plane_caps.end", result) != 0 || result != VK_SUCCESS) goto cleanup;
    VkExtent2D extent = mode.parameters.visibleRegion;
    if (!(plane_caps.supportedAlpha & VK_DISPLAY_PLANE_ALPHA_OPAQUE_BIT_KHR) ||
        plane_caps.minSrcPosition.x > 0 || plane_caps.maxSrcPosition.x < 0 ||
        plane_caps.minSrcPosition.y > 0 || plane_caps.maxSrcPosition.y < 0 ||
        plane_caps.minDstPosition.x > 0 || plane_caps.maxDstPosition.x < 0 ||
        plane_caps.minDstPosition.y > 0 || plane_caps.maxDstPosition.y < 0 ||
        extent.width < plane_caps.minSrcExtent.width || extent.width > plane_caps.maxSrcExtent.width ||
        extent.height < plane_caps.minSrcExtent.height || extent.height > plane_caps.maxSrcExtent.height ||
        extent.width < plane_caps.minDstExtent.width || extent.width > plane_caps.maxDstExtent.width ||
        extent.height < plane_caps.minDstExtent.height || extent.height > plane_caps.maxDstExtent.height) {
        event("radv.frame.plane_caps.unsupported", 0);
        goto cleanup;
    }
    VkDisplaySurfaceCreateInfoKHR surface_info = {
        .sType = VK_STRUCTURE_TYPE_DISPLAY_SURFACE_CREATE_INFO_KHR,
        .displayMode = mode.displayMode,
        .planeIndex = plane,
        .planeStackIndex = planes[plane].currentStackIndex,
        .transform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
        .globalAlpha = 1.0f,
        .alphaMode = VK_DISPLAY_PLANE_ALPHA_OPAQUE_BIT_KHR,
        .imageExtent = extent
    };
    if (event("radv.frame.surface.create.begin", 0) != 0) goto cleanup;
    result = CreateDisplayPlaneSurfaceKHR(instance, &surface_info, NULL, &surface);
    if (event("radv.frame.surface.create.end", result) != 0 || result != VK_SUCCESS) goto cleanup;
    if (surface == VK_NULL_HANDLE) { event("radv.frame.surface.null", 0); goto cleanup; }
    VkBool32 support = VK_FALSE;
    if (event("radv.frame.surface.support.begin", 0) != 0) goto cleanup;
    result = GetPhysicalDeviceSurfaceSupportKHR(physical, family, surface, &support);
    if (event("radv.frame.surface.support.end", result) != 0 || result != VK_SUCCESS) goto cleanup;
    if (!support) { event("radv.frame.surface.unsupported_queue", 0); goto cleanup; }
    VkSurfaceCapabilitiesKHR caps;
    if (event("radv.frame.surface.caps.begin", 0) != 0) goto cleanup;
    result = GetPhysicalDeviceSurfaceCapabilitiesKHR(physical, surface, &caps);
    if (event("radv.frame.surface.caps.end", result) != 0 || result != VK_SUCCESS) goto cleanup;
    if ((caps.supportedUsageFlags & required_usage) != required_usage ||
        !(caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR) ||
        !(caps.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR) ||
        caps.maxImageArrayLayers < 1 ||
        extent.width < caps.minImageExtent.width || extent.width > caps.maxImageExtent.width ||
        extent.height < caps.minImageExtent.height || extent.height > caps.maxImageExtent.height ||
        (caps.currentExtent.width != UINT32_MAX &&
         (extent.width != caps.currentExtent.width || extent.height != caps.currentExtent.height))) {
        event("radv.frame.surface.caps.unsupported", 0);
        goto cleanup;
    }
    VkSurfaceFormatKHR formats[32];
    count = 32;
    if (event("radv.frame.formats.begin", 0) != 0) goto cleanup;
    result = GetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &count, formats);
    if (event("radv.frame.formats.end", result) != 0 || result != VK_SUCCESS) goto cleanup;
    if (count == 0 || count > 32) { event("radv.frame.formats.invalid_count", 0); goto cleanup; }
    VkSurfaceFormatKHR chosen = {VK_FORMAT_UNDEFINED, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};
    for (uint32_t i = 0; i < count; ++i) {
        if ((formats[i].format == VK_FORMAT_B8G8R8A8_UNORM ||
             (count == 1 && formats[i].format == VK_FORMAT_UNDEFINED)) &&
            formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            chosen.format = VK_FORMAT_B8G8R8A8_UNORM;
            chosen.colorSpace = formats[i].colorSpace;
            break;
        }
    }
    if (chosen.format == VK_FORMAT_UNDEFINED) { event("radv.frame.format.unsupported", 0); goto cleanup; }
    VkPresentModeKHR presents[16];
    count = 16;
    if (event("radv.frame.present_modes.begin", 0) != 0) goto cleanup;
    result = GetPhysicalDeviceSurfacePresentModesKHR(physical, surface, &count, presents);
    if (event("radv.frame.present_modes.end", result) != 0 || result != VK_SUCCESS) goto cleanup;
    if (count == 0 || count > 16) { event("radv.frame.present_modes.invalid_count", 0); goto cleanup; }
    int fifo = 0;
    for (uint32_t i = 0; i < count; ++i) fifo |= presents[i] == VK_PRESENT_MODE_FIFO_KHR;
    uint32_t image_count = caps.minImageCount;
    if (image_count < 3 && (caps.maxImageCount == 0 || caps.maxImageCount >= 3)) image_count = 3;
    if (!fifo || image_count == 0 || image_count > 8 || (caps.maxImageCount != 0 && image_count > caps.maxImageCount)) {
        event("radv.frame.swapchain.unsupported", 0);
        goto cleanup;
    }
    VkSwapchainCreateInfoKHR swap_info = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR, .surface = surface,
        .minImageCount = image_count, .imageFormat = chosen.format, .imageColorSpace = chosen.colorSpace,
        .imageExtent = extent, .imageArrayLayers = 1, .imageUsage = required_usage,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE, .preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR, .presentMode = VK_PRESENT_MODE_FIFO_KHR,
        .clipped = VK_TRUE
    };
    if (event("radv.frame.swapchain.create.begin", 0) != 0) goto cleanup;
    result = CreateSwapchainKHR(device, &swap_info, NULL, &swapchain);
    if (event("radv.frame.swapchain.create.end", result) != 0 || result != VK_SUCCESS) goto cleanup;
    if (swapchain == VK_NULL_HANDLE) { event("radv.frame.swapchain.null", 0); goto cleanup; }
    VkImage images[8];
    count = 8;
    if (event("radv.frame.images.begin", 0) != 0) goto cleanup;
    result = GetSwapchainImagesKHR(device, swapchain, &count, images);
    if (event("radv.frame.images.end", result) != 0 || result != VK_SUCCESS) goto cleanup;
    if (count < image_count || count > 8) { event("radv.frame.images.invalid_count", 0); goto cleanup; }
#ifdef ECHO_RADV_CUBE_PROBE
    if (echo_radv_cube_create(&cube, device, GetDeviceProcAddr, chosen.format, extent, images, count, event) != 0) goto cleanup;
    cube_ready = 1;
#endif
    VkCommandPoolCreateInfo pool_info = { .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .queueFamilyIndex = family };
    if (event("radv.frame.pool.create.begin", 0) != 0) goto cleanup;
    result = CreateCommandPool(device, &pool_info, NULL, &pool);
    if (event("radv.frame.pool.create.end", result) != 0 || result != VK_SUCCESS) goto cleanup;
    if (pool == VK_NULL_HANDLE) { event("radv.frame.pool.null", 0); goto cleanup; }
    VkCommandBufferAllocateInfo alloc_info = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1 };
    VkCommandBuffer command = VK_NULL_HANDLE;
    if (event("radv.frame.command.allocate.begin", 0) != 0) goto cleanup;
    result = AllocateCommandBuffers(device, &alloc_info, &command);
    if (event("radv.frame.command.allocate.end", result) != 0 || result != VK_SUCCESS) goto cleanup;
    if (command == VK_NULL_HANDLE) { event("radv.frame.command.null", 0); goto cleanup; }
    VkSemaphoreCreateInfo semaphore_info = { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    if (event("radv.frame.semaphores.begin", 0) != 0) goto cleanup;
    result = CreateSemaphore(device, &semaphore_info, NULL, &acquired);
    if (event("radv.frame.semaphore.acquire.end", result) != 0 || result != VK_SUCCESS) goto cleanup;
    if (acquired == VK_NULL_HANDLE) { event("radv.frame.semaphore.acquire.null", 0); goto cleanup; }
    result = CreateSemaphore(device, &semaphore_info, NULL, &rendered);
    if (event("radv.frame.semaphore.render.end", result) != 0 || result != VK_SUCCESS) goto cleanup;
    if (rendered == VK_NULL_HANDLE) { event("radv.frame.semaphore.render.null", 0); goto cleanup; }
    VkFenceCreateInfo fence_info = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    if (event("radv.frame.fence.begin", 0) != 0) goto cleanup;
    result = CreateFence(device, &fence_info, NULL, &fence);
    if (event("radv.frame.fence.end", result) != 0 || result != VK_SUCCESS) goto cleanup;
    if (fence == VK_NULL_HANDLE) { event("radv.frame.fence.null", 0); goto cleanup; }
    uint32_t index = 0;
    if (event("radv.frame.acquire.begin", 0) != 0) goto cleanup;
    status = ECHO_RADV_FRAME_HOLD_FAILED;
    result = AcquireNextImageKHR(device, swapchain, UINT64_C(10000000000), acquired, VK_NULL_HANDLE, &index);
    if (event("radv.frame.acquire.end", result) != 0 || (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)) goto cleanup;
    if (index >= count || images[index] == VK_NULL_HANDLE) { event("radv.frame.acquire.invalid_index", 0); goto cleanup; }
    VkCommandBufferBeginInfo begin = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT };
    if (event("radv.frame.record.begin", 0) != 0) goto cleanup;
    result = BeginCommandBuffer(command, &begin);
    if (event("radv.frame.command.begin.end", result) != 0 || result != VK_SUCCESS) goto cleanup;
#ifdef ECHO_RADV_CUBE_PROBE
    if (echo_radv_cube_record(&cube, command, index, event) != 0) goto cleanup;
#else
    VkImageSubresourceRange range = { .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1 };
    VkImageMemoryBarrier barrier = { .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED, .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT, .image = images[index], .subresourceRange = range };
    CmdPipelineBarrier(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                       0, NULL, 0, NULL, 1, &barrier);
    VkClearColorValue color = { .float32 = {0.02f, 0.55f, 0.85f, 1.0f} };
    CmdClearColorImage(command, images[index], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &color, 1, &range);
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = 0;
    CmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0,
                       0, NULL, 0, NULL, 1, &barrier);
#endif
    result = EndCommandBuffer(command);
    if (event("radv.frame.record.end", result) != 0 || result != VK_SUCCESS) goto cleanup;
    VkPipelineStageFlags wait_stage =
#ifdef ECHO_RADV_CUBE_PROBE
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
#else
        VK_PIPELINE_STAGE_TRANSFER_BIT;
#endif
    VkSubmitInfo submit = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount = 1, .pWaitSemaphores = &acquired, .pWaitDstStageMask = &wait_stage,
        .commandBufferCount = 1, .pCommandBuffers = &command,
        .signalSemaphoreCount = 1, .pSignalSemaphores = &rendered };
    if (event("radv.frame.submit.begin", 0) != 0) goto cleanup;
    result = QueueSubmit(queue, 1, &submit, fence);
    status = ECHO_RADV_FRAME_HOLD_FAILED;
    if (event("radv.frame.submit.end", result) != 0 || result != VK_SUCCESS) return status;
    if (event("radv.frame.fence.wait.begin", 0) != 0) return status;
    result = WaitForFences(device, 1, &fence, VK_TRUE, UINT64_C(10000000000));
    if (event("radv.frame.fence.wait.end", result) != 0 || result != VK_SUCCESS) return status;
    VkPresentInfoKHR present = { .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1, .pWaitSemaphores = &rendered,
        .swapchainCount = 1, .pSwapchains = &swapchain, .pImageIndices = &index };
    if (event("radv.frame.present.begin", 0) != 0) return status;
    result = QueuePresentKHR(queue, &present);
    if (event("radv.frame.present.end", result) != 0 || (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)) return status;
#ifdef ECHO_RADV_CUBE_INTERACTIVE
    if (echo_radv_cube_loop(&cube, device, GetDeviceProcAddr, queue, swapchain, pool, command, fence,
                            acquired, count, event) != 0) {
        event("radv.cube.loop.failed", 0);
        return ECHO_RADV_FRAME_HOLD_FAILED;
    }
#endif
    if (event("radv.frame.resources.hold", 0) != 0) return status;
    return ECHO_RADV_FRAME_PRESENTED;

cleanup:
    if (status == ECHO_RADV_FRAME_HOLD_FAILED) return status;
    if (fence != VK_NULL_HANDLE) DestroyFence(device, fence, NULL);
    if (rendered != VK_NULL_HANDLE) DestroySemaphore(device, rendered, NULL);
    if (acquired != VK_NULL_HANDLE) DestroySemaphore(device, acquired, NULL);
    if (pool != VK_NULL_HANDLE) DestroyCommandPool(device, pool, NULL);
#ifdef ECHO_RADV_CUBE_PROBE
    if (cube_ready) echo_radv_cube_destroy(&cube);
#endif
    if (swapchain != VK_NULL_HANDLE) DestroySwapchainKHR(device, swapchain, NULL);
    if (surface != VK_NULL_HANDLE) DestroySurfaceKHR(instance, surface, NULL);
    return status;
}
