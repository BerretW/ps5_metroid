#include <vulkan/vulkan.h>
#include "cube_probe.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int fail_framebuffer;
static int framebuffer_calls;
static int fail_pipeline;

static VkResult VKAPI_CALL test_render_pass(VkDevice device, const VkRenderPassCreateInfo *info,
                                            const VkAllocationCallbacks *allocator, VkRenderPass *out)
{
    if (info->attachmentCount != 1) return VK_ERROR_INITIALIZATION_FAILED;
    VkRenderPassCreateInfo copy = *info;
    VkAttachmentDescription attachment = info->pAttachments[0];
    attachment.finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    copy.pAttachments = &attachment;
    return vkCreateRenderPass(device, &copy, allocator, out);
}

static VkResult VKAPI_CALL test_framebuffer(VkDevice device, const VkFramebufferCreateInfo *info,
                                            const VkAllocationCallbacks *allocator, VkFramebuffer *out)
{
    if (fail_framebuffer && ++framebuffer_calls == 2) return VK_ERROR_OUT_OF_HOST_MEMORY;
    return vkCreateFramebuffer(device, info, allocator, out);
}

static VkResult VKAPI_CALL test_pipeline(VkDevice device, VkPipelineCache cache, uint32_t count,
                                         const VkGraphicsPipelineCreateInfo *info,
                                         const VkAllocationCallbacks *allocator, VkPipeline *out)
{
    if (fail_pipeline) return VK_ERROR_OUT_OF_HOST_MEMORY;
    return vkCreateGraphicsPipelines(device, cache, count, info, allocator, out);
}

static PFN_vkVoidFunction VKAPI_CALL test_get_proc(VkDevice device, const char *name)
{
    if (strcmp(name, "vkCreateRenderPass") == 0) return (PFN_vkVoidFunction)test_render_pass;
    if (strcmp(name, "vkCreateFramebuffer") == 0) return (PFN_vkVoidFunction)test_framebuffer;
    if (strcmp(name, "vkCreateGraphicsPipelines") == 0) return (PFN_vkVoidFunction)test_pipeline;
    return vkGetDeviceProcAddr(device, name);
}

static int event(const char *stage, int code)
{
    (void)stage;
    (void)code;
    return 0;
}

static uint32_t memory_type(const VkPhysicalDeviceMemoryProperties *properties, uint32_t bits,
                            VkMemoryPropertyFlags flags)
{
    for (uint32_t i = 0; i < properties->memoryTypeCount; ++i) {
        if ((bits & (1u << i)) && (properties->memoryTypes[i].propertyFlags & flags) == flags) return i;
    }
    return UINT32_MAX;
}

int main(void)
{
    enum { SIDE = 256 };
    int status = 1, submitted = 0, completed = 0;
    VkInstance instance = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory image_memory = VK_NULL_HANDLE, buffer_memory = VK_NULL_HANDLE;
    VkBuffer buffer = VK_NULL_HANDLE;
    VkCommandPool pool = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    EchoRadvCube cube = {0};
    int cube_ready = 0;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    VkCommandBuffer command = VK_NULL_HANDLE;
    VkPhysicalDeviceMemoryProperties memory;
    VkMemoryRequirements requirements;
    uint32_t family = UINT32_MAX;
    VkResult result;
#define CHECK(call) do { result = (call); if (result != VK_SUCCESS) { \
    fprintf(stderr, "%s failed: %d\n", #call, (int)result); goto cleanup; } } while (0)
    VkApplicationInfo app = { .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .apiVersion = VK_API_VERSION_1_0 };
    VkInstanceCreateInfo instance_info = { .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &app };
    CHECK(vkCreateInstance(&instance_info, NULL, &instance));
    uint32_t count = 8;
    VkPhysicalDevice devices[8];
    CHECK(vkEnumeratePhysicalDevices(instance, &count, devices));
    for (uint32_t d = 0; d < count && family == UINT32_MAX; ++d) {
        uint32_t queue_count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(devices[d], &queue_count, NULL);
        if (queue_count > 32) continue;
        VkQueueFamilyProperties families[32];
        vkGetPhysicalDeviceQueueFamilyProperties(devices[d], &queue_count, families);
        for (uint32_t i = 0; i < queue_count; ++i) {
            if (families[i].queueCount && (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
                physical = devices[d]; family = i; break;
            }
        }
    }
    if (!physical) { fputs("No graphics queue\n", stderr); goto cleanup; }
    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties(physical, &properties);
    printf("Vulkan test device: %s\n", properties.deviceName);
    VkFormatProperties format;
    vkGetPhysicalDeviceFormatProperties(physical, VK_FORMAT_B8G8R8A8_UNORM, &format);
    if (!(format.optimalTilingFeatures & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT)) {
        fputs("Color attachment format unsupported\n", stderr); goto cleanup;
    }
    float priority = 1.0f;
    VkDeviceQueueCreateInfo queue_info = { .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = family, .queueCount = 1, .pQueuePriorities = &priority };
    VkDeviceCreateInfo device_info = { .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = 1, .pQueueCreateInfos = &queue_info };
    CHECK(vkCreateDevice(physical, &device_info, NULL, &device));
    vkGetDeviceQueue(device, family, 0, &queue);
    vkGetPhysicalDeviceMemoryProperties(physical, &memory);
    VkImageCreateInfo image_info = { .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D, .format = VK_FORMAT_B8G8R8A8_UNORM,
        .extent = {SIDE, SIDE, 1}, .mipLevels = 1, .arrayLayers = 1, .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL, .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE, .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED };
    CHECK(vkCreateImage(device, &image_info, NULL, &image));
    vkGetImageMemoryRequirements(device, image, &requirements);
    uint32_t type = memory_type(&memory, requirements.memoryTypeBits, 0);
    if (type == UINT32_MAX) { fputs("No image memory type\n", stderr); goto cleanup; }
    VkMemoryAllocateInfo allocation = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = requirements.size, .memoryTypeIndex = type };
    CHECK(vkAllocateMemory(device, &allocation, NULL, &image_memory));
    CHECK(vkBindImageMemory(device, image, image_memory, 0));
    VkBufferCreateInfo buffer_info = { .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = SIDE * SIDE * 4, .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT, .sharingMode = VK_SHARING_MODE_EXCLUSIVE };
    CHECK(vkCreateBuffer(device, &buffer_info, NULL, &buffer));
    vkGetBufferMemoryRequirements(device, buffer, &requirements);
    type = memory_type(&memory, requirements.memoryTypeBits,
                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (type == UINT32_MAX) { fputs("No coherent readback memory type\n", stderr); goto cleanup; }
    allocation.allocationSize = requirements.size;
    allocation.memoryTypeIndex = type;
    CHECK(vkAllocateMemory(device, &allocation, NULL, &buffer_memory));
    CHECK(vkBindBufferMemory(device, buffer, buffer_memory, 0));
    VkExtent2D extent = {SIDE, SIDE};
    VkImage duplicate_images[] = {image, image};
    fail_framebuffer = 1;
    if (echo_radv_cube_create(&cube, device, test_get_proc, VK_FORMAT_B8G8R8A8_UNORM,
                              extent, duplicate_images, 2, event) == 0 || cube.render_pass || cube.views[0]) {
        fputs("Framebuffer failure cleanup was not performed\n", stderr); goto cleanup;
    }
    fail_framebuffer = 0;
    fail_pipeline = 1;
    if (echo_radv_cube_create(&cube, device, test_get_proc, VK_FORMAT_B8G8R8A8_UNORM,
                              extent, &image, 1, event) == 0 || cube.render_pass || cube.vertex_shader || cube.layout) {
        fputs("Pipeline failure cleanup was not performed\n", stderr); goto cleanup;
    }
    fail_pipeline = 0;
    if (echo_radv_cube_create(&cube, device, test_get_proc, VK_FORMAT_B8G8R8A8_UNORM,
                              extent, &image, 1, event) != 0) {
        fputs("Cube setup failed\n", stderr); goto cleanup;
    }
    cube_ready = 1;
    if (echo_radv_cube_record(&cube, VK_NULL_HANDLE, 1, event) == 0) {
        fputs("Invalid framebuffer index accepted\n", stderr); goto cleanup;
    }
    VkCommandPoolCreateInfo pool_info = { .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .queueFamilyIndex = family };
    CHECK(vkCreateCommandPool(device, &pool_info, NULL, &pool));
    VkCommandBufferAllocateInfo command_info = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1 };
    CHECK(vkAllocateCommandBuffers(device, &command_info, &command));
    VkCommandBufferBeginInfo begin = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT };
    CHECK(vkBeginCommandBuffer(command, &begin));
    if (echo_radv_cube_record(&cube, command, 0, event) != 0) { fputs("Cube recording failed\n", stderr); goto cleanup; }
    VkImageMemoryBarrier barrier = { .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image, .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1} };
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, NULL, 0, NULL, 1, &barrier);
    VkBufferImageCopy copy = { .imageSubresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .layerCount = 1},
        .imageExtent = {SIDE, SIDE, 1} };
    vkCmdCopyImageToBuffer(command, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer, 1, &copy);
    CHECK(vkEndCommandBuffer(command));
    VkFenceCreateInfo fence_info = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    CHECK(vkCreateFence(device, &fence_info, NULL, &fence));
    VkSubmitInfo submit = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &command };
    CHECK(vkQueueSubmit(queue, 1, &submit, fence));
    submitted = 1;
    CHECK(vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_C(10000000000)));
    completed = 1;
    void *mapped = NULL;
    CHECK(vkMapMemory(device, buffer_memory, 0, SIDE * SIDE * 4, 0, &mapped));
    const uint8_t *pixels = (const uint8_t *)mapped;
#ifdef ECHO_RADV_CUBE_INTERACTIVE
    static uint8_t first_pixels[SIDE * SIDE * 4];
    memcpy(first_pixels, pixels, sizeof(first_pixels));
#endif
    uint32_t red = 0, yellow = 0, blue = 0, background = 0;
    uint32_t red_x = 0, red_y = 0, yellow_y = 0, blue_x = 0, blue_y = 0;
    for (uint32_t i = 0; i < SIDE * SIDE; ++i) {
        uint8_t b = pixels[i * 4], g = pixels[i * 4 + 1], r = pixels[i * 4 + 2];
        if (r > 180 && g > 30 && g < 110 && b < 70) { ++red; red_x += i % SIDE; red_y += i / SIDE; }
        if (r > 180 && g > 135 && b < 100) { ++yellow; yellow_y += i / SIDE; }
        if (b > 180 && g > 100 && r < 100) { ++blue; blue_x += i % SIDE; blue_y += i / SIDE; }
        background += r < 15 && g < 15 && b < 35;
    }
    vkUnmapMemory(device, buffer_memory);
    printf("Cube readback: red=%u yellow=%u blue=%u background=%u\n", red, yellow, blue, background);
    if (red < 500 || yellow < 500 || blue < 500 || background < 5000) {
        fputs("Cube pixel thresholds failed\n", stderr); goto cleanup;
    }
    if (red_x / red <= SIDE / 2 || blue_x / blue >= SIDE / 2 ||
        yellow_y / yellow >= red_y / red || yellow_y / yellow >= blue_y / blue) {
        fputs("Cube face positions failed\n", stderr); goto cleanup;
    }
#ifdef ECHO_RADV_CUBE_INTERACTIVE
    const float rotations[2][2] = {{1.0f, 0.4f}, {3.141593f, 0.4f}};
    for (int angle = 0; angle < 2; ++angle) {
        cube.angles[0] = rotations[angle][0];
        cube.angles[1] = rotations[angle][1];
        CHECK(vkResetFences(device, 1, &fence));
        CHECK(vkResetCommandPool(device, pool, 0));
        CHECK(vkBeginCommandBuffer(command, &begin));
        if (echo_radv_cube_record(&cube, command, 0, event) != 0) { fputs("Rotated cube recording failed\n", stderr); goto cleanup; }
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             0, 0, NULL, 0, NULL, 1, &barrier);
        vkCmdCopyImageToBuffer(command, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer, 1, &copy);
        CHECK(vkEndCommandBuffer(command));
        CHECK(vkQueueSubmit(queue, 1, &submit, fence));
        completed = 0;
        CHECK(vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_C(10000000000)));
        completed = 1;
        CHECK(vkMapMemory(device, buffer_memory, 0, SIDE * SIDE * 4, 0, &mapped));
        pixels = (const uint8_t *)mapped;
        uint32_t changed = 0, colorful = 0;
        for (uint32_t i = 0; i < SIDE * SIDE; ++i) {
            if (pixels[i * 4] != first_pixels[i * 4] || pixels[i * 4 + 1] != first_pixels[i * 4 + 1] ||
                pixels[i * 4 + 2] != first_pixels[i * 4 + 2]) ++changed;
            if (pixels[i * 4] > 100 || pixels[i * 4 + 1] > 100 || pixels[i * 4 + 2] > 100) ++colorful;
        }
        vkUnmapMemory(device, buffer_memory);
        printf("Rotated cube %d: changed=%u colorful=%u\n", angle, changed, colorful);
        if (changed < 3000 || colorful < 5000) { fputs("Cube rotation pixel thresholds failed\n", stderr); goto cleanup; }
    }
#endif
    status = 0;
cleanup:
    if (submitted && !completed) {
        fputs("GPU completion uncertain; leaving resources to process teardown\n", stderr);
        return 1;
    }
    if (fence) vkDestroyFence(device, fence, NULL);
    if (pool) vkDestroyCommandPool(device, pool, NULL);
    if (cube_ready) echo_radv_cube_destroy(&cube);
    if (buffer) vkDestroyBuffer(device, buffer, NULL);
    if (buffer_memory) vkFreeMemory(device, buffer_memory, NULL);
    if (image) vkDestroyImage(device, image, NULL);
    if (image_memory) vkFreeMemory(device, image_memory, NULL);
    if (device) vkDestroyDevice(device, NULL);
    if (instance) vkDestroyInstance(instance, NULL);
    return status;
#undef CHECK
}
