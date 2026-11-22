#define VK_NO_PROTOTYPES
#include "cube_loop.h"
#include "cube_pad.h"

#include <string.h>

#define HANDLE(type, n) ((type)(uintptr_t)(n))
static int scenario, bad, frames, sleeps, sem_count, acquired_signal, fence_signaled, present_ready;
int echo_radv_pad_open(EchoRadvPad *pad, EchoRadvEvent event) { (void)pad; (void)event; return 0; }
int echo_radv_pad_update(EchoRadvPad *pad, float angles[2], EchoRadvEvent event)
{
    (void)pad; (void)event;
    if (scenario == 3) return 1;
    angles[0] += 0.04f;
    return 0;
}
int echo_radv_cube_record(EchoRadvCube *cube, VkCommandBuffer command, uint32_t index, EchoRadvEvent event)
{
    (void)cube; (void)command; (void)event;
    if (index != (uint32_t)(frames % 3)) bad = 1;
    return 0;
}
int sceKernelUsleep(uint32_t microseconds)
{
    if (microseconds != 16667) bad = 1;
    return ++sleeps == 5 ? -1 : 0;
}
static int event(const char *stage, int code) { (void)stage; (void)code; return 0; }
static VkResult VKAPI_CALL create_sem(VkDevice d, const VkSemaphoreCreateInfo *info,
                                      const VkAllocationCallbacks *a, VkSemaphore *out)
{
    *out = HANDLE(VkSemaphore, 20 + sem_count++);
    return VK_SUCCESS;
}
static VkResult VKAPI_CALL reset_fence(VkDevice d, uint32_t count, const VkFence *fence)
{
    if (!fence_signaled || count != 1) bad = 1;
    fence_signaled = 0;
    return VK_SUCCESS;
}
static VkResult VKAPI_CALL reset_pool(VkDevice d, VkCommandPool pool, VkCommandPoolResetFlags flags)
{
    if (fence_signaled) bad = 1;
    return VK_SUCCESS;
}
static VkResult VKAPI_CALL acquire(VkDevice d, VkSwapchainKHR s, uint64_t timeout, VkSemaphore sem,
                                   VkFence fence, uint32_t *index)
{
    if (acquired_signal || !timeout || sem != HANDLE(VkSemaphore, 1)) bad = 1;
    if (scenario == 1) return VK_TIMEOUT;
    acquired_signal = 1;
    *index = (uint32_t)(frames % 3);
    return VK_SUCCESS;
}
static VkResult VKAPI_CALL begin(VkCommandBuffer c, const VkCommandBufferBeginInfo *info) { return VK_SUCCESS; }
static VkResult VKAPI_CALL end(VkCommandBuffer c) { return VK_SUCCESS; }
static VkResult VKAPI_CALL submit(VkQueue q, uint32_t count, const VkSubmitInfo *info, VkFence fence)
{
    if (!acquired_signal || count != 1 || info->pWaitSemaphores[0] != HANDLE(VkSemaphore, 1) ||
        info->pSignalSemaphores[0] != HANDLE(VkSemaphore, 20 + frames % 3) ||
        info->pWaitDstStageMask[0] != VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT) bad = 1;
    acquired_signal = 0;
    fence_signaled = 1;
    return VK_SUCCESS;
}
static VkResult VKAPI_CALL wait(VkDevice d, uint32_t count, const VkFence *f, VkBool32 all, uint64_t timeout)
{
    if (!fence_signaled || !timeout) bad = 1;
    if (scenario == 2) return VK_TIMEOUT;
    present_ready = 1;
    return VK_SUCCESS;
}
static VkResult VKAPI_CALL present(VkQueue q, const VkPresentInfoKHR *info)
{
    if (!present_ready || info->pImageIndices[0] != (uint32_t)(frames % 3) ||
        info->pWaitSemaphores[0] != HANDLE(VkSemaphore, 20 + frames % 3)) bad = 1;
    if (scenario == 4) return VK_ERROR_DEVICE_LOST;
    present_ready = 0;
    ++frames;
    return VK_SUCCESS;
}
static PFN_vkVoidFunction VKAPI_CALL get_proc(VkDevice d, const char *name)
{
#define MATCH(key, fn) if (strcmp(name, "vk" key) == 0) return (PFN_vkVoidFunction)fn
    MATCH("CreateSemaphore", create_sem); MATCH("ResetFences", reset_fence);
    MATCH("ResetCommandPool", reset_pool); MATCH("AcquireNextImageKHR", acquire);
    MATCH("BeginCommandBuffer", begin); MATCH("EndCommandBuffer", end);
    MATCH("QueueSubmit", submit); MATCH("WaitForFences", wait); MATCH("QueuePresentKHR", present);
#undef MATCH
    return NULL;
}
int main(void)
{
    for (scenario = 0; scenario < 5; ++scenario) {
        bad = frames = sleeps = sem_count = acquired_signal = present_ready = 0;
        fence_signaled = 1;
        EchoRadvCube cube = {0};
        if (echo_radv_cube_loop(&cube, HANDLE(VkDevice, 1), get_proc, HANDLE(VkQueue, 1),
                                HANDLE(VkSwapchainKHR, 1), HANDLE(VkCommandPool, 1),
                                HANDLE(VkCommandBuffer, 1), HANDLE(VkFence, 1),
                                HANDLE(VkSemaphore, 1), 3, event) == 0 || bad || sem_count != 3 ||
            (scenario == 0 && (frames != 5 || sleeps != 5 || cube.angles[0] < 0.19f)) ||
            (scenario != 0 && frames != 0)) return scenario + 1;
    }
    return 0;
}
