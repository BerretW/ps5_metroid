#define VK_NO_PROTOTYPES
#include "cube_loop.h"
#include "cube_pad.h"

#include <stdint.h>

int sceKernelUsleep(uint32_t microseconds);

static int quiet_event(const char *stage, int code)
{
    (void)stage;
    (void)code;
    return 0;
}

static int step_event(EchoRadvEvent event, const char *stage, VkResult result, int first)
{
    if (first || (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)) return event(stage, result);
    return 0;
}

int echo_radv_cube_loop(EchoRadvCube *cube, VkDevice device, PFN_vkGetDeviceProcAddr get_proc,
                        VkQueue queue, VkSwapchainKHR swapchain, VkCommandPool pool,
                        VkCommandBuffer command, VkFence fence, VkSemaphore acquired,
                        uint32_t image_count, EchoRadvEvent event)
{
#define LOAD(type, name) type name = (type)get_proc(device, "vk" #name)
    LOAD(PFN_vkCreateSemaphore, CreateSemaphore);
    LOAD(PFN_vkResetFences, ResetFences);
    LOAD(PFN_vkResetCommandPool, ResetCommandPool);
    LOAD(PFN_vkAcquireNextImageKHR, AcquireNextImageKHR);
    LOAD(PFN_vkBeginCommandBuffer, BeginCommandBuffer);
    LOAD(PFN_vkEndCommandBuffer, EndCommandBuffer);
    LOAD(PFN_vkQueueSubmit, QueueSubmit);
    LOAD(PFN_vkWaitForFences, WaitForFences);
    LOAD(PFN_vkQueuePresentKHR, QueuePresentKHR);
#undef LOAD
    if (!CreateSemaphore || !ResetFences || !ResetCommandPool || !AcquireNextImageKHR ||
        !BeginCommandBuffer || !EndCommandBuffer || !QueueSubmit || !WaitForFences || !QueuePresentKHR) {
        event("radv.cube.loop.function.missing", 0);
        return 1;
    }
    if (image_count == 0 || image_count > 8) { event("radv.cube.loop.count.invalid", 0); return 1; }
    VkSemaphore done[8] = {0};
    VkSemaphoreCreateInfo semaphore_info = { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    for (uint32_t i = 0; i < image_count; ++i) {
        if (event("radv.cube.loop.semaphore.begin", (int)i) != 0) return 1;
        VkResult result = CreateSemaphore(device, &semaphore_info, NULL, &done[i]);
        if (event("radv.cube.loop.semaphore.end", result) != 0 || result != VK_SUCCESS) return 1;
        if (!done[i]) { event("radv.cube.loop.semaphore.null", (int)i); return 1; }
    }
    EchoRadvPad pad;
    if (echo_radv_pad_open(&pad, event) != 0) return 1;
    if (event("radv.cube.loop.start", 0) != 0) return 1;
    int first = 1;
    int heartbeat = 0;
    for (;;) {
        if (echo_radv_pad_update(&pad, cube->angles, event) != 0) return 1;
        VkResult result = ResetFences(device, 1, &fence);
        if (step_event(event, "radv.cube.loop.fence.reset", result, first) != 0 || result != VK_SUCCESS) return 1;
        result = ResetCommandPool(device, pool, 0);
        if (step_event(event, "radv.cube.loop.pool.reset", result, first) != 0 || result != VK_SUCCESS) return 1;
        uint32_t index = 0;
        if (first && event("radv.cube.loop.acquire.begin", 0) != 0) return 1;
        result = AcquireNextImageKHR(device, swapchain, UINT64_C(10000000000), acquired, VK_NULL_HANDLE, &index);
        if (step_event(event, "radv.cube.loop.acquire.end", result, first) != 0 ||
            (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)) return 1;
        if (index >= image_count) { event("radv.cube.loop.index.invalid", 0); return 1; }
        VkCommandBufferBeginInfo begin = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT };
        result = BeginCommandBuffer(command, &begin);
        if (step_event(event, "radv.cube.loop.record.begin", result, first) != 0 || result != VK_SUCCESS) return 1;
        if (echo_radv_cube_record(cube, command, index, first ? event : quiet_event) != 0) {
            event("radv.cube.loop.draw.error", 0);
            return 1;
        }
        result = EndCommandBuffer(command);
        if (step_event(event, "radv.cube.loop.record.end", result, first) != 0 || result != VK_SUCCESS) return 1;
        VkPipelineStageFlags stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo submit = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .waitSemaphoreCount = 1, .pWaitSemaphores = &acquired, .pWaitDstStageMask = &stage,
            .commandBufferCount = 1, .pCommandBuffers = &command,
            .signalSemaphoreCount = 1, .pSignalSemaphores = &done[index] };
        if (first && event("radv.cube.loop.submit.begin", 0) != 0) return 1;
        result = QueueSubmit(queue, 1, &submit, fence);
        if (step_event(event, "radv.cube.loop.submit.end", result, first) != 0 || result != VK_SUCCESS) return 1;
        if (first && event("radv.cube.loop.fence.wait.begin", 0) != 0) return 1;
        result = WaitForFences(device, 1, &fence, VK_TRUE, UINT64_C(10000000000));
        if (step_event(event, "radv.cube.loop.fence.wait.end", result, first) != 0 || result != VK_SUCCESS) return 1;
        VkPresentInfoKHR present = { .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
            .waitSemaphoreCount = 1, .pWaitSemaphores = &done[index],
            .swapchainCount = 1, .pSwapchains = &swapchain, .pImageIndices = &index };
        if (first && event("radv.cube.loop.present.begin", 0) != 0) return 1;
        result = QueuePresentKHR(queue, &present);
        if (step_event(event, "radv.cube.loop.present.end", result, first) != 0 ||
            (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)) return 1;
        first = 0;
        if (++heartbeat == 300) {
            if (event("radv.cube.loop.heartbeat", 0) != 0) return 1;
            heartbeat = 0;
        }
        int sleep_result = sceKernelUsleep(16667);
        if (sleep_result != 0) { event("radv.cube.loop.sleep.error", sleep_result); return 1; }
    }
}
