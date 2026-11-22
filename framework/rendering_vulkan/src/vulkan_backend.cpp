#include "echo/framework/vulkan_backend.hpp"
#include "platform_surface.hpp"
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <new>

namespace echo::rendering {
namespace {
constexpr std::uint32_t kResources = 64, kImages = 16, kDraws = 512, kTargets = 8;
#ifdef ECHO_PS5_VULKAN
constexpr std::uint64_t kGpuTimeout = 10000000000ULL;
#else
constexpr std::uint64_t kGpuTimeout = UINT64_MAX;
#endif
struct Buffer { VkBuffer buffer{}; VkDeviceMemory memory{}; VkDeviceSize bytes{}; void* mapped{}; };
struct Image {
    VkImage image{};
    VkDeviceMemory memory{};
    VkImageView view{};
    VkSampler sampler{};
    VkDeviceSize bytes{};
    VkFormat format{};
};
struct MeshSlot {
    Buffer vertices{}, indices{};
    std::uint32_t count{}, generation{1};
    std::size_t vertexCapacity{}, indexCapacity{};
    bool active{}, dynamic{};
};
struct TextureSlot { Image image{}; std::uint32_t generation{1}, references{}; bool active{}; TextureColorSpace colorSpace{}; };
struct MaterialSlot { Material material{}; VkDescriptorSet descriptor{}; std::uint32_t generation{1}; bool active{}; };
struct TargetSlot {
    Image color{}, depth{};
    VkFramebuffer framebuffer{};
    std::uint32_t width{}, height{}, generation{1}, references{};
    bool active{}, contentsInitialized{};
};
struct alignas(16) Uniforms {
    Mat4 vp;
    Vec4 camera, directionAmbient, lightColor;
    std::array<PointLight, 4> points;
};
struct Push { Mat4 model; Vec4 color; Vec4 emissionMetallic; Vec4 parameters; Vec4 uvTransform; };
static_assert(sizeof(Push) == 128 && sizeof(Uniforms) == 240 && sizeof(Vertex) == 64);
Status error(ErrorCode code, std::string_view detail) noexcept {
    return {code, "echo.rendering.vulkan", detail};
}
void transition(VkCommandBuffer command, VkImage image, VkImageLayout from, VkImageLayout to,
                VkAccessFlags src, VkAccessFlags dst, VkPipelineStageFlags srcStage,
                VkPipelineStageFlags dstStage) noexcept {
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.oldLayout = from; barrier.newLayout = to;
    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image; barrier.srcAccessMask = src; barrier.dstAccessMask = dst;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(command, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}
}

struct VulkanBackend::State {
    VkInstance instance{}; VkDebugUtilsMessengerEXT debug{};
    VkPhysicalDevice physical{}; VkDevice device{}; VkSurfaceKHR surface{}; VkQueue queue{};
    std::uint32_t family{}, width{}, height{}, logicalWidth{}, logicalHeight{}, imageCount{}, currentImage{};
    VkPhysicalDeviceMemoryProperties memoryProperties{};
    VkSwapchainKHR swapchain{}; VkFormat format{}, depthFormat{}; VkExtent2D extent{};
    std::array<VkImage, kImages> images{};
    std::array<VkImageView, kImages> views{};
    std::array<VkFramebuffer, kImages> framebuffers{};
    std::array<VkSemaphore, kImages> finished{};
    Image depth{};
    VkRenderPass renderPass{}; VkDescriptorSetLayout descriptorLayout{};
    VkDescriptorPool descriptorPool{}; VkPipelineLayout pipelineLayout{};
    std::array<VkPipeline, 4> pipelines{};
    VkCommandPool commandPool{}; VkCommandBuffer command{};
    VkSemaphore acquired{}; VkFence fence{};
    Buffer uniform{}, readback{};
    std::array<MeshSlot, kResources> meshes{};
    std::array<TextureSlot, kResources> textures{};
    std::array<MaterialSlot, kResources> materials{};
    std::array<TargetSlot, kTargets> targets{};
    std::array<Draw, kDraws> draws{};
    std::uint32_t drawCount{};
    std::atomic<std::uint32_t> validationErrors{};
    Diagnostics stats{};
    bool initialized{}, recording{}, offscreen{}, recreate{}, lost{}, transferSupported{};
    std::uint32_t activeTarget{};
    RenderLogCallback diagnosticLog{};
    void* diagnosticContext{};
    std::array<char, 512> shaderDirectory{}, failure{}, capturePath{};
    std::chrono::steady_clock::time_point frameStart{};

    Status check(VkResult result, const char* step) noexcept {
        if (result == VK_SUCCESS) return Status::success();
        if (result == VK_ERROR_DEVICE_LOST) lost = true;
        std::snprintf(failure.data(), failure.size(), "%s: VkResult=%d", step, static_cast<int>(result));
        std::fprintf(stderr, "{\"stage\":\"%s\",\"result\":%d}\n", step, static_cast<int>(result));
        if (diagnosticLog) diagnosticLog(diagnosticContext, failure.data());
        return error(ErrorCode::InitializationFailed, failure.data());
    }
    void log(const char* stage) noexcept {
        std::printf("{\"stage\":\"%s\",\"result\":\"ok\"}\n", stage);
        if (diagnosticLog) diagnosticLog(diagnosticContext, stage);
    }
    std::uint32_t memoryType(std::uint32_t bits, VkMemoryPropertyFlags properties) noexcept {
        for (std::uint32_t i = 0; i < memoryProperties.memoryTypeCount; ++i)
            if ((bits & (1U << i)) && (memoryProperties.memoryTypes[i].propertyFlags & properties) == properties) return i;
        return UINT32_MAX;
    }
    void freeBuffer(Buffer& b) noexcept {
        if (b.mapped) vkUnmapMemory(device, b.memory);
        if (b.buffer) vkDestroyBuffer(device, b.buffer, nullptr);
        if (b.memory) { vkFreeMemory(device, b.memory, nullptr); stats.allocatedGpuBytes -= b.bytes; }
        b = {};
    }
    Status buffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties,
                  Buffer& output) noexcept {
        VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        info.size = size; info.usage = usage; info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        auto status = check(vkCreateBuffer(device, &info, nullptr, &output.buffer), "buffer.create");
        if (!status) return status;
        VkMemoryRequirements req{}; vkGetBufferMemoryRequirements(device, output.buffer, &req);
        auto type = memoryType(req.memoryTypeBits, properties);
        if (type == UINT32_MAX) { freeBuffer(output); return error(ErrorCode::MissingCapability, "buffer memory type"); }
        VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        alloc.allocationSize = req.size; alloc.memoryTypeIndex = type;
        status = check(vkAllocateMemory(device, &alloc, nullptr, &output.memory), "buffer.allocate");
        if (!status) { freeBuffer(output); return status; }
        output.bytes = req.size; stats.allocatedGpuBytes += req.size;
        status = check(vkBindBufferMemory(device, output.buffer, output.memory, 0), "buffer.bind");
        if (status && (properties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT))
            status = check(vkMapMemory(device, output.memory, 0, size, 0, &output.mapped), "buffer.map");
        if (!status) freeBuffer(output);
        return status;
    }
    void freeImage(Image& image) noexcept {
        if (image.sampler) vkDestroySampler(device, image.sampler, nullptr);
        if (image.view) vkDestroyImageView(device, image.view, nullptr);
        if (image.image) vkDestroyImage(device, image.image, nullptr);
        if (image.memory) { vkFreeMemory(device, image.memory, nullptr); stats.allocatedGpuBytes -= image.bytes; }
        image = {};
    }
    void freeTarget(TargetSlot& target) noexcept {
        if (target.framebuffer) vkDestroyFramebuffer(device, target.framebuffer, nullptr);
        target.framebuffer = {};
        freeImage(target.color);
        freeImage(target.depth);
        target.width = target.height = 0;
        target.active = target.contentsInitialized = false;
    }
    Status makeTargetFramebuffer(TargetSlot& target) noexcept {
        std::array<VkImageView, 2> views{target.color.view, target.depth.view};
        VkFramebufferCreateInfo framebuffer{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        framebuffer.renderPass = renderPass;
        framebuffer.attachmentCount = static_cast<std::uint32_t>(views.size());
        framebuffer.pAttachments = views.data();
        framebuffer.width = target.width;
        framebuffer.height = target.height;
        framebuffer.layers = 1;
        return check(vkCreateFramebuffer(device, &framebuffer, nullptr, &target.framebuffer),
                     "target.framebuffer");
    }
    Status image(std::uint32_t w, std::uint32_t h, VkFormat imageFormat, VkImageUsageFlags usage,
                 VkImageAspectFlags aspect, Image& output) noexcept {
        VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        info.imageType = VK_IMAGE_TYPE_2D; info.format = imageFormat; info.extent = {w, h, 1};
        info.mipLevels = 1; info.arrayLayers = 1; info.samples = VK_SAMPLE_COUNT_1_BIT;
        info.tiling = VK_IMAGE_TILING_OPTIMAL; info.usage = usage;
        auto status = check(vkCreateImage(device, &info, nullptr, &output.image), "image.create");
        if (!status) return status;
        VkMemoryRequirements req{}; vkGetImageMemoryRequirements(device, output.image, &req);
        auto type = memoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (type == UINT32_MAX) { freeImage(output); return error(ErrorCode::MissingCapability, "image memory type"); }
        VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        alloc.allocationSize = req.size; alloc.memoryTypeIndex = type;
        status = check(vkAllocateMemory(device, &alloc, nullptr, &output.memory), "image.allocate");
        if (!status) { freeImage(output); return status; }
        output.bytes = req.size; output.format = imageFormat; stats.allocatedGpuBytes += req.size;
        status = check(vkBindImageMemory(device, output.image, output.memory, 0), "image.bind");
        if (status) {
            VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
            view.image = output.image; view.viewType = VK_IMAGE_VIEW_TYPE_2D; view.format = imageFormat;
            view.subresourceRange = {aspect, 0, 1, 0, 1};
            status = check(vkCreateImageView(device, &view, nullptr, &output.view), "image.view");
        }
        if (!status) freeImage(output);
        return status;
    }
    void updateMaterialDescriptor(const MaterialSlot& slot, const Image* baseOverride = nullptr) noexcept {
        const auto& material = slot.material;
        const Image& base = baseOverride ? *baseOverride :
            (material.targetTexture.valid()
                ? targets[material.targetTexture.index].color
                : textures[material.texture.index].image);
        const Image& normal = material.normalTexture.valid()
            ? textures[material.normalTexture.index].image : base;
        const Image& pbr = material.metallicRoughnessTexture.valid()
            ? textures[material.metallicRoughnessTexture.index].image : base;
        VkDescriptorBufferInfo bufferInfo{uniform.buffer, 0, sizeof(Uniforms)};
        VkDescriptorImageInfo baseInfo{base.sampler, base.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkDescriptorImageInfo normalInfo{normal.sampler, normal.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkDescriptorImageInfo pbrInfo{pbr.sampler, pbr.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        std::array<VkWriteDescriptorSet, 4> writes{};
        writes[0] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, slot.descriptor, 0, 0, 1,
                     VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, nullptr, &bufferInfo, nullptr};
        writes[1] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, slot.descriptor, 1, 0, 1,
                     VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &baseInfo, nullptr, nullptr};
        writes[2] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, slot.descriptor, 2, 0, 1,
                     VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &normalInfo, nullptr, nullptr};
        writes[3] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, slot.descriptor, 3, 0, 1,
                     VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &pbrInfo, nullptr, nullptr};
        vkUpdateDescriptorSets(device, static_cast<std::uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }
    void updateTargetDescriptors(std::uint32_t targetIndex, const Image& replacement) noexcept {
        for (const auto& material : materials)
            if (material.active && material.material.targetTexture.valid() &&
                material.material.targetTexture.index == targetIndex)
                updateMaterialDescriptor(material, &replacement);
    }
    Status immediateBegin(VkCommandBuffer& cmd) noexcept {
        VkCommandBufferAllocateInfo alloc{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        alloc.commandPool = commandPool; alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; alloc.commandBufferCount = 1;
        auto s = check(vkAllocateCommandBuffers(device, &alloc, &cmd), "upload.command.allocate");
        if (!s) return s;
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        s = check(vkBeginCommandBuffer(cmd, &begin), "upload.command.begin");
        if (!s) { vkFreeCommandBuffers(device, commandPool, 1, &cmd); cmd = {}; }
        return s;
    }
    Status immediateEnd(VkCommandBuffer cmd) noexcept {
        auto s = check(vkEndCommandBuffer(cmd), "upload.command.end");
        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submit.commandBufferCount = 1; submit.pCommandBuffers = &cmd;
        if (s) s = check(vkQueueSubmit(queue, 1, &submit, VK_NULL_HANDLE), "upload.submit");
        if (s) s = check(vkQueueWaitIdle(queue), "upload.wait");
        vkFreeCommandBuffers(device, commandPool, 1, &cmd);
        return s;
    }
    Status upload(const void* data, VkDeviceSize size, VkBufferUsageFlags usage, Buffer& output) noexcept {
        Buffer staging{};
        auto s = buffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, staging);
        if (!s) return s;
        std::memcpy(staging.mapped, data, static_cast<std::size_t>(size));
        s = buffer(size, usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, output);
        VkCommandBuffer cmd{};
        if (s) s = immediateBegin(cmd);
        if (s) {
            VkBufferCopy copy{0, 0, size}; vkCmdCopyBuffer(cmd, staging.buffer, output.buffer, 1, &copy);
            VkBufferMemoryBarrier barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_INDEX_READ_BIT;
            barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.buffer = output.buffer; barrier.size = VK_WHOLE_SIZE;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
                                 0, 0, nullptr, 1, &barrier, 0, nullptr);
            s = immediateEnd(cmd);
        }
        freeBuffer(staging);
        if (!s) freeBuffer(output);
        return s;
    }
    Status shader(const char* name, VkShaderModule& module) noexcept {
        std::array<char, 1024> path{};
#ifdef ECHO_PS5_VULKAN
        std::snprintf(path.data(), path.size(), "%s/%s", shaderDirectory.data(), name);
        errno = 0;
        FILE* file = std::fopen(path.data(), "rb");
        const auto openResult = errno;
#else
        std::snprintf(path.data(), path.size(), "%s\\%s", shaderDirectory.data(), name);
        FILE* file = nullptr; const auto openResult=fopen_s(&file, path.data(), "rb");
#endif
        if (!file) {
            std::fprintf(stderr,"{\"stage\":\"shader.open\",\"shader\":\"%s\",\"result\":\"unavailable\",\"errno\":%d}\n",
                         name,static_cast<int>(openResult));
            std::snprintf(failure.data(),failure.size(),"shader.open: SPIR-V file unavailable (%s), errno=%d",
                          name,static_cast<int>(openResult));
            return error(ErrorCode::NotFound,failure.data());
        }
        std::array<std::uint32_t, 16384> words{};
        auto size = std::fread(words.data(), 1, sizeof(words), file);
        const auto trailing=std::fgetc(file);
        bool valid = !std::ferror(file) && trailing == EOF && size >= 20 &&
                     size % 4 == 0 && words[0] == 0x07230203;
        const auto closeResult=std::fclose(file);
        if (closeResult!=0) return error(ErrorCode::StorageFailure,"shader.close: SPIR-V file close failed");
        if (!valid) {
            std::fprintf(stderr,"{\"stage\":\"shader.validate\",\"shader\":\"%s\",\"result\":\"invalid\"}\n",name);
            return error(ErrorCode::CorruptData, "shader.validate: SPIR-V length, magic or read error");
        }
        VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        info.codeSize = size; info.pCode = words.data();
        return check(vkCreateShaderModule(device, &info, nullptr, &module), "shader.module");
    }
    Status makePipelines() noexcept {
        VkShaderModule vertex{}, fragment{};
        auto s = shader("scene.vert.spv", vertex);
        if (s) s = shader("scene.frag.spv", fragment);
        if (!s) { if (vertex) vkDestroyShaderModule(device, vertex, nullptr); return s; }
        std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
        stages[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vertex, "main", nullptr};
        stages[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fragment, "main", nullptr};
        VkVertexInputBindingDescription binding{0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX};
        std::array<VkVertexInputAttributeDescription, 5> attributes{{
            {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, position)},
            {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal)},
            {2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, uv)},
            {3, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Vertex, color)},
            {4, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Vertex, tangent)}}};
        VkPipelineVertexInputStateCreateInfo input{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        input.vertexBindingDescriptionCount = 1; input.pVertexBindingDescriptions = &binding;
        input.vertexAttributeDescriptionCount = static_cast<std::uint32_t>(attributes.size()); input.pVertexAttributeDescriptions = attributes.data();
        VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
        viewport.viewportCount = viewport.scissorCount = 1;
        VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        raster.polygonMode = VK_POLYGON_MODE_FILL; raster.cullMode = VK_CULL_MODE_NONE;
        raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE; raster.lineWidth = 1;
        VkPipelineMultisampleStateCreateInfo samples{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        samples.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineDepthStencilStateCreateInfo depthState{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
        depthState.depthTestEnable = depthState.depthWriteEnable = VK_TRUE; depthState.depthCompareOp = VK_COMPARE_OP_LESS;
        VkPipelineColorBlendAttachmentState attachment{}; attachment.colorWriteMask = 0xf;
        VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        blend.attachmentCount = 1; blend.pAttachments = &attachment;
        std::array<VkDynamicState, 2> dynamics{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
        dynamic.dynamicStateCount = 2; dynamic.pDynamicStates = dynamics.data();
        VkGraphicsPipelineCreateInfo pipeline{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        pipeline.stageCount = 2; pipeline.pStages = stages.data(); pipeline.pVertexInputState = &input;
        pipeline.pInputAssemblyState = &assembly; pipeline.pViewportState = &viewport;
        pipeline.pRasterizationState = &raster; pipeline.pMultisampleState = &samples;
        pipeline.pDepthStencilState = &depthState; pipeline.pColorBlendState = &blend;
        pipeline.pDynamicState = &dynamic; pipeline.layout = pipelineLayout; pipeline.renderPass = renderPass;
        for (std::uint32_t i = 0; i < pipelines.size() && s; ++i) {
            depthState.depthTestEnable = i == 3 ? VK_FALSE : VK_TRUE;
            depthState.depthWriteEnable = i == 0 ? VK_TRUE : VK_FALSE;
            attachment.blendEnable = i != 0;
            attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            attachment.dstColorBlendFactor = i == 2 ? VK_BLEND_FACTOR_ONE : VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            attachment.colorBlendOp = attachment.alphaBlendOp = VK_BLEND_OP_ADD;
            attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            s = check(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeline, nullptr, &pipelines[i]), "pipeline.create");
        }
        vkDestroyShaderModule(device, vertex, nullptr); vkDestroyShaderModule(device, fragment, nullptr);
        if (s) log("pipeline.ready");
        return s;
    }
    void freeSwapchain() noexcept {
        for (auto& pipeline : pipelines) { if (pipeline) vkDestroyPipeline(device, pipeline, nullptr); pipeline = {}; }
        for (auto& target : targets) {
            if (target.framebuffer) vkDestroyFramebuffer(device, target.framebuffer, nullptr);
            target.framebuffer = {};
        }
        for (std::uint32_t i = 0; i < imageCount; ++i) {
            if (framebuffers[i]) vkDestroyFramebuffer(device, framebuffers[i], nullptr);
            if (views[i]) vkDestroyImageView(device, views[i], nullptr);
            if (finished[i]) vkDestroySemaphore(device, finished[i], nullptr);
            framebuffers[i] = {}; views[i] = {}; finished[i] = {};
        }
        freeImage(depth); freeBuffer(readback);
        if (renderPass) vkDestroyRenderPass(device, renderPass, nullptr);
        if (swapchain) vkDestroySwapchainKHR(device, swapchain, nullptr);
        renderPass = {}; swapchain = {}; imageCount = 0;
    }
    Status makeSwapchain() noexcept {
        VkSurfaceCapabilitiesKHR caps{};
        auto s = check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical, surface, &caps), "surface.capabilities");
        if (!s) return s;
#ifdef ECHO_PS5_VULKAN
        if (!(caps.supportedUsageFlags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) ||
            !(caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR) ||
            !(caps.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR) ||
            caps.maxImageArrayLayers < 1)
            return error(ErrorCode::MissingCapability, "PS5 display surface capabilities");
        std::array<VkPresentModeKHR, 16> modes{};
        std::uint32_t modeCount = static_cast<std::uint32_t>(modes.size());
        s = check(vkGetPhysicalDeviceSurfacePresentModesKHR(physical, surface, &modeCount, modes.data()),
                  "surface.present_modes");
        if (!s) return s;
        if (!modeCount || modeCount > modes.size() ||
            std::find(modes.begin(), modes.begin() + modeCount, VK_PRESENT_MODE_FIFO_KHR) ==
                modes.begin() + modeCount)
            return error(ErrorCode::MissingCapability, "PS5 FIFO present mode");
#endif
        std::uint32_t count{};
        s = check(vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &count, nullptr), "surface.formats");
        if (!s) return s;
        if (count == 0 || count > 64) return error(ErrorCode::CapacityExceeded, "surface format count");
        std::array<VkSurfaceFormatKHR, 64> formats{};
        s = check(vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &count, formats.data()), "surface.formats");
        if (!s) return s;
        auto selected = formats[0];
        bool found{};
#ifdef ECHO_PS5_VULKAN
        for (std::uint32_t i = 0; i < count; ++i) {
            if ((formats[i].format == VK_FORMAT_B8G8R8A8_UNORM ||
                 (count == 1 && formats[i].format == VK_FORMAT_UNDEFINED)) &&
                formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
                selected = {VK_FORMAT_B8G8R8A8_UNORM, formats[i].colorSpace};
                found = true;
                break;
            }
        }
        if (!found) return error(ErrorCode::MissingCapability, "PS5 BGRA UNORM swapchain format required");
#else
        for (std::uint32_t i = 0; i < count; ++i)
            if ((formats[i].format == VK_FORMAT_B8G8R8A8_SRGB || formats[i].format == VK_FORMAT_R8G8B8A8_SRGB) &&
                formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) { selected = formats[i]; found = true; break; }
        if (!found) return error(ErrorCode::MissingCapability, "8-bit sRGB swapchain format required");
#endif
        format = selected.format;
        extent = caps.currentExtent.width != UINT32_MAX ? caps.currentExtent : VkExtent2D{
            std::clamp(width, caps.minImageExtent.width, caps.maxImageExtent.width),
            std::clamp(height, caps.minImageExtent.height, caps.maxImageExtent.height)};
        if (!extent.width || !extent.height) return error(ErrorCode::InvalidState, "window minimized; defer frame");
#ifdef ECHO_PS5_VULKAN
        if (extent.width != width || extent.height != height)
            return error(ErrorCode::MissingCapability, "PS5 display mode and swapchain extent differ");
#endif
        auto wanted = caps.minImageCount + 1;
#ifdef ECHO_PS5_VULKAN
        if (wanted < 3 && (!caps.maxImageCount || caps.maxImageCount >= 3)) wanted = 3;
#endif
        if (caps.maxImageCount && wanted > caps.maxImageCount) wanted = caps.maxImageCount;
        if (wanted > kImages) return error(ErrorCode::CapacityExceeded, "swapchain image capacity");
        transferSupported = (caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) != 0;
        VkSwapchainCreateInfoKHR info{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
        info.surface = surface; info.minImageCount = wanted; info.imageFormat = format;
        info.imageColorSpace = selected.colorSpace; info.imageExtent = extent;
        info.imageArrayLayers = 1; info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        if (transferSupported) info.imageUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        info.preTransform = caps.currentTransform; info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        if (!(caps.supportedCompositeAlpha & info.compositeAlpha))
            info.compositeAlpha = static_cast<VkCompositeAlphaFlagBitsKHR>(caps.supportedCompositeAlpha & (~caps.supportedCompositeAlpha + 1));
        info.presentMode = VK_PRESENT_MODE_FIFO_KHR; info.clipped = VK_TRUE;
        s = check(vkCreateSwapchainKHR(device, &info, nullptr, &swapchain), "swapchain.create");
        if (!s) return s;
        s = check(vkGetSwapchainImagesKHR(device, swapchain, &imageCount, nullptr), "swapchain.images");
        if (!s) return s;
        if (imageCount > kImages) { imageCount = 0; return error(ErrorCode::CapacityExceeded, "swapchain image count"); }
        s = check(vkGetSwapchainImagesKHR(device, swapchain, &imageCount, images.data()), "swapchain.images");
        if (!s) return s;
        for (auto candidate : {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D16_UNORM}) {
            VkFormatProperties properties{}; vkGetPhysicalDeviceFormatProperties(physical, candidate, &properties);
            if (properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) { depthFormat = candidate; break; }
        }
        if (!depthFormat) return error(ErrorCode::MissingCapability, "depth format");
        s = image(extent.width, extent.height, depthFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                  VK_IMAGE_ASPECT_DEPTH_BIT, depth);
        if (!s) return s;
        std::array<VkAttachmentDescription, 2> attachments{};
        attachments[0].format = format; attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
        attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; attachments[0].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        attachments[1] = attachments[0]; attachments[1].format = depthFormat;
        attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        VkAttachmentReference color{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkAttachmentReference depthRef{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
        VkSubpassDescription subpass{}; subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1; subpass.pColorAttachments = &color; subpass.pDepthStencilAttachment = &depthRef;
        VkSubpassDependency dependency{};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL; dependency.dstSubpass = 0;
        dependency.srcStageMask = dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                                           VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                                                           VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        dependency.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        VkRenderPassCreateInfo pass{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
        pass.attachmentCount = 2; pass.pAttachments = attachments.data(); pass.subpassCount = 1; pass.pSubpasses = &subpass;
        pass.dependencyCount = 1; pass.pDependencies = &dependency;
        s = check(vkCreateRenderPass(device, &pass, nullptr, &renderPass), "renderpass.create");
        for (std::uint32_t i = 0; i < imageCount && s; ++i) {
            VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
            view.image = images[i]; view.viewType = VK_IMAGE_VIEW_TYPE_2D; view.format = format;
            view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            s = check(vkCreateImageView(device, &view, nullptr, &views[i]), "swapchain.view");
            if (!s) break;
            std::array<VkImageView, 2> fbViews{views[i], depth.view};
            VkFramebufferCreateInfo fb{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
            fb.renderPass = renderPass; fb.attachmentCount = 2; fb.pAttachments = fbViews.data();
            fb.width = extent.width; fb.height = extent.height; fb.layers = 1;
            s = check(vkCreateFramebuffer(device, &fb, nullptr, &framebuffers[i]), "framebuffer.create");
            if (!s) break;
            VkSemaphoreCreateInfo sem{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
            s = check(vkCreateSemaphore(device, &sem, nullptr, &finished[i]), "present.semaphore");
        }
        if (s) s = makePipelines();
        for (std::uint32_t targetIndex = 0; targetIndex < targets.size(); ++targetIndex) {
            auto& target = targets[targetIndex];
            if (!s || !target.active) continue;
            if (target.color.format != format || target.depth.format != depthFormat) {
                TargetSlot replacement{};
                replacement.generation = target.generation;
                replacement.references = target.references;
                s = makeTarget(target.width, target.height, replacement);
                if (s) {
                    updateTargetDescriptors(targetIndex, replacement.color);
                    freeTarget(target);
                    target = replacement;
                }
            } else {
                s = makeTargetFramebuffer(target);
            }
        }
        if (s) { recreate = false; std::printf("{\"stage\":\"swapchain.ready\",\"width\":%u,\"height\":%u,\"images\":%u}\n", extent.width, extent.height, imageCount); }
        return s;
    }
    Status makeTarget(std::uint32_t w, std::uint32_t h, TargetSlot& output) noexcept {
        if (!w || !h || w > 4096 || h > 4096)
            return error(ErrorCode::InvalidArgument, "render target dimensions");
        VkImageFormatProperties imageProperties{};
        const auto usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                           VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        auto result = vkGetPhysicalDeviceImageFormatProperties(
            physical, format, VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL, usage, 0, &imageProperties);
        if (result != VK_SUCCESS || w > imageProperties.maxExtent.width || h > imageProperties.maxExtent.height)
            return error(ErrorCode::MissingCapability, "render target color format or extent");
        result = vkGetPhysicalDeviceImageFormatProperties(
            physical, depthFormat, VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, 0, &imageProperties);
        if (result != VK_SUCCESS || w > imageProperties.maxExtent.width || h > imageProperties.maxExtent.height)
            return error(ErrorCode::MissingCapability, "render target depth format or extent");
        output.width = w;
        output.height = h;
        auto status = image(w, h, format, usage, VK_IMAGE_ASPECT_COLOR_BIT, output.color);
        if (status) {
            VkSamplerCreateInfo sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
            sampler.magFilter = sampler.minFilter = VK_FILTER_LINEAR;
            sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
            sampler.addressModeU = sampler.addressModeV = sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            status = check(vkCreateSampler(device, &sampler, nullptr, &output.color.sampler), "target.sampler");
        }
        if (status) status = image(w, h, depthFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                                   VK_IMAGE_ASPECT_DEPTH_BIT, output.depth);
        if (status) status = makeTargetFramebuffer(output);
        if (!status) {
            freeTarget(output);
            return status;
        }
        output.active = true;
        return Status::success();
    }
    Status idle() noexcept {
        if (recording) return error(ErrorCode::InvalidState, "resource change during frame");
        if (lost) return error(ErrorCode::InvalidState, "device lost; shutdown required");
        return check(vkDeviceWaitIdle(device), "device.idle");
    }
};

static VKAPI_ATTR VkBool32 VKAPI_CALL validationCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT, const VkDebugUtilsMessengerCallbackDataEXT* data, void* user) {
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
        static_cast<std::atomic<std::uint32_t>*>(user)->fetch_add(1);
    std::fprintf(stderr, "{\"stage\":\"validation\",\"severity\":%u}\nVULKAN VALIDATION: %s\n",
                 static_cast<unsigned>(severity), data->pMessage);
    return VK_FALSE;
}
VulkanBackend::VulkanBackend() noexcept : state_(new(std::nothrow) State) {}
VulkanBackend::~VulkanBackend() { shutdown(); delete state_; }
Status VulkanBackend::initialize(const BackendInit& init) noexcept {
    if (!state_) return error(ErrorCode::CapacityExceeded, "backend host allocation");
    auto& s = *state_;
    if (s.instance) return error(ErrorCode::InvalidState, "already initialized");
    if (!init.shaderDirectory || std::strlen(init.shaderDirectory) >= s.shaderDirectory.size() ||
        !init.width || !init.height || (!!init.logicalWidth != !!init.logicalHeight))
        return error(ErrorCode::InvalidArgument, "dimensions, logical viewport or shader path");
#ifndef ECHO_PS5_VULKAN
    if (!init.nativeWindow) return error(ErrorCode::InvalidArgument, "window is required");
#else
    if (init.validation) return error(ErrorCode::MissingCapability, "PS5 validation layers are unavailable");
    s.diagnosticLog = init.log; s.diagnosticContext = init.logContext;
    const char* missing{};
    if (!loadGlobalDispatch(missing)) {
        std::fprintf(stderr, "PS5 Vulkan global function unavailable: %s\n", missing);
        std::snprintf(s.failure.data(), s.failure.size(), "PS5 Vulkan global function missing: %s", missing);
        return error(ErrorCode::MissingCapability, s.failure.data());
    }
    std::array<VkExtensionProperties, 128> availableExtensions{};
    std::uint32_t extensionCount = static_cast<std::uint32_t>(availableExtensions.size());
    auto extensionStatus = s.check(vkEnumerateInstanceExtensionProperties(
        nullptr, &extensionCount, availableExtensions.data()), "instance.extensions");
    if (!extensionStatus) return extensionStatus;
    if (extensionCount > availableExtensions.size())
        return error(ErrorCode::CapacityExceeded, "PS5 instance extension count");
    bool hasSurface{}, hasDisplay{};
    for (std::uint32_t i = 0; i < extensionCount; ++i) {
        hasSurface |= std::strcmp(availableExtensions[i].extensionName, VK_KHR_SURFACE_EXTENSION_NAME) == 0;
        hasDisplay |= std::strcmp(availableExtensions[i].extensionName, VK_KHR_DISPLAY_EXTENSION_NAME) == 0;
    }
    if (!hasSurface || !hasDisplay)
        return error(ErrorCode::MissingCapability, "PS5 display instance extensions");
#endif
    s.diagnosticLog = init.log; s.diagnosticContext = init.logContext;
    std::memcpy(s.shaderDirectory.data(), init.shaderDirectory, std::strlen(init.shaderDirectory) + 1);
    s.width = init.width; s.height = init.height;
    s.logicalWidth = init.logicalWidth; s.logicalHeight = init.logicalHeight;
    std::array<const char*, 3> extensions{VK_KHR_SURFACE_EXTENSION_NAME, platformSurfaceExtension(), VK_EXT_DEBUG_UTILS_EXTENSION_NAME};
    const char* layer = "VK_LAYER_KHRONOS_validation";
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "PROJECT ECHO"; app.applicationVersion = 1;
#ifdef ECHO_PS5_VULKAN
    app.apiVersion = VK_API_VERSION_1_0;
#else
    app.apiVersion = VK_API_VERSION_1_1;
#endif
    VkDebugUtilsMessengerCreateInfoEXT debug{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
    debug.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    debug.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                        VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    debug.pfnUserCallback = validationCallback; debug.pUserData = &s.validationErrors;
    VkInstanceCreateInfo instance{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    instance.pApplicationInfo = &app; instance.enabledExtensionCount = init.validation ? 3 : 2;
    instance.ppEnabledExtensionNames = extensions.data();
    if (init.validation) { instance.enabledLayerCount = 1; instance.ppEnabledLayerNames = &layer; instance.pNext = &debug; }
    s.log("instance.create.begin");
    auto result = s.check(vkCreateInstance(&instance, nullptr, &s.instance), "instance.create");
    if (!result) { shutdown(); return result; }
    if (!s.instance) return error(ErrorCode::InitializationFailed, "Vulkan instance was null");
#ifdef ECHO_PS5_VULKAN
    if (!loadInstanceDispatch(s.instance, missing)) {
        std::fprintf(stderr, "PS5 Vulkan instance function unavailable: %s\n", missing);
        if (vkDestroyInstance) vkDestroyInstance(s.instance, nullptr);
        else std::fprintf(stderr, "PS5 Vulkan instance could not be destroyed\n");
        s.instance = {};
        std::snprintf(s.failure.data(), s.failure.size(), "PS5 Vulkan instance function missing: %s", missing);
        return error(ErrorCode::MissingCapability, s.failure.data());
    }
#endif
    if (init.validation) {
        auto create = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(s.instance, "vkCreateDebugUtilsMessengerEXT"));
        if (!create) { shutdown(); return error(ErrorCode::MissingCapability, "debug messenger extension"); }
        result = s.check(create(s.instance, &debug, nullptr, &s.debug), "validation.create");
        if (!result) { shutdown(); return result; }
    }
    s.log("instance.ready");
#ifndef ECHO_PS5_VULKAN
    VkExtent2D preferredExtent{s.width, s.height};
    result = s.check(createPlatformSurface(s.instance, VK_NULL_HANDLE, init.nativeWindow,
                                           &s.surface, &preferredExtent), "surface.create");
    if (!result) { shutdown(); return result; }
#endif
    std::uint32_t count{};
    result = s.check(vkEnumeratePhysicalDevices(s.instance, &count, nullptr), "gpu.enumerate");
    std::array<VkPhysicalDevice, 16> devices{};
    if (!result || !count || count > devices.size()) { shutdown(); return result ? error(ErrorCode::MissingCapability, "GPU count") : result; }
    result = s.check(vkEnumeratePhysicalDevices(s.instance, &count, devices.data()), "gpu.enumerate");
    if (!result) { shutdown(); return result; }
    s.log("gpu.enumerated");
    int best = -1;
    for (std::uint32_t i = 0; i < count; ++i) {
        VkPhysicalDeviceProperties properties{}; vkGetPhysicalDeviceProperties(devices[i], &properties);
#ifdef ECHO_PS5_VULKAN
        if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU || properties.apiVersion < VK_API_VERSION_1_0) continue;
#else
        if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU || properties.apiVersion < VK_API_VERSION_1_1) continue;
#endif
        std::uint32_t extCount{};
        result = s.check(vkEnumerateDeviceExtensionProperties(devices[i], nullptr, &extCount, nullptr), "gpu.extensions");
        if (!result) { shutdown(); return result; }
        if (extCount > 512) continue;
        std::array<VkExtensionProperties, 512> ext{};
        result = s.check(vkEnumerateDeviceExtensionProperties(devices[i], nullptr, &extCount, ext.data()), "gpu.extensions");
        if (!result) { shutdown(); return result; }
        bool swap = false;
        for (std::uint32_t e = 0; e < extCount; ++e) if (!std::strcmp(ext[e].extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME)) swap = true;
        if (!swap) continue;
        std::uint32_t queues{}; vkGetPhysicalDeviceQueueFamilyProperties(devices[i], &queues, nullptr);
        if (queues > 64) continue;
        std::array<VkQueueFamilyProperties, 64> families{};
        vkGetPhysicalDeviceQueueFamilyProperties(devices[i], &queues, families.data());
#ifdef ECHO_PS5_VULKAN
        VkSurfaceKHR candidateSurface{};
        VkExtent2D candidateExtent{s.width, s.height};
        s.log("surface.create.begin");
        result = s.check(createPlatformSurface(s.instance, devices[i], init.nativeWindow,
                                               &candidateSurface, &candidateExtent), "surface.create");
        if (!result) continue;
#else
        VkSurfaceKHR candidateSurface = s.surface;
#endif
        for (std::uint32_t q = 0; q < queues; ++q) {
            VkBool32 present{};
            result = s.check(vkGetPhysicalDeviceSurfaceSupportKHR(devices[i], q, candidateSurface, &present), "gpu.presentation");
            if (!result) {
#ifdef ECHO_PS5_VULKAN
                if (candidateSurface != s.surface) vkDestroySurfaceKHR(s.instance, candidateSurface, nullptr);
#endif
                shutdown(); return result;
            }
            int score = properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 2 : 1;
            if (families[q].queueCount && (families[q].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present && score > best) {
                s.physical = devices[i]; s.family = q; best = score;
#ifdef ECHO_PS5_VULKAN
                if (s.surface && s.surface != candidateSurface) vkDestroySurfaceKHR(s.instance, s.surface, nullptr);
                s.surface = candidateSurface;
                s.width = candidateExtent.width; s.height = candidateExtent.height;
#endif
            }
        }
#ifdef ECHO_PS5_VULKAN
        if (candidateSurface != s.surface) vkDestroySurfaceKHR(s.instance, candidateSurface, nullptr);
#endif
    }
    if (!s.physical) { shutdown(); return error(ErrorCode::MissingCapability, "no hardware graphics/present queue"); }
    VkPhysicalDeviceProperties properties{}; vkGetPhysicalDeviceProperties(s.physical, &properties);
    std::printf("{\"stage\":\"gpu.selected\",\"device\":\"%s\",\"api\":%u,\"driver\":%u,\"queue\":%u,\"extension\":\"VK_KHR_swapchain\",\"optionalFeaturesEnabled\":false}\n",
                properties.deviceName, properties.apiVersion, properties.driverVersion, s.family);
    vkGetPhysicalDeviceMemoryProperties(s.physical, &s.memoryProperties);
    float priority = 1;
    VkDeviceQueueCreateInfo queue{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queue.queueFamilyIndex = s.family; queue.queueCount = 1; queue.pQueuePriorities = &priority;
    const char* swapExtension = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
    VkDeviceCreateInfo device{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    device.queueCreateInfoCount = 1; device.pQueueCreateInfos = &queue;
    device.enabledExtensionCount = 1; device.ppEnabledExtensionNames = &swapExtension;
    s.log("device.create.begin");
    result = s.check(vkCreateDevice(s.physical, &device, nullptr, &s.device), "device.create");
    if (!result) { shutdown(); return result; }
    if (!s.device) { shutdown(); return error(ErrorCode::InitializationFailed, "Vulkan device was null"); }
#ifdef ECHO_PS5_VULKAN
    if (!loadDeviceDispatch(s.device, missing)) {
        std::fprintf(stderr, "PS5 Vulkan device function unavailable: %s\n", missing);
        if (vkDestroyDevice) vkDestroyDevice(s.device, nullptr);
        else std::fprintf(stderr, "PS5 Vulkan device could not be destroyed\n");
        s.device = {};
        shutdown();
        std::snprintf(s.failure.data(), s.failure.size(), "PS5 Vulkan device function missing: %s", missing);
        return error(ErrorCode::MissingCapability, s.failure.data());
    }
#endif
    vkGetDeviceQueue(s.device, s.family, 0, &s.queue); s.log("device.ready");
    VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; pool.queueFamilyIndex = s.family;
    result = s.check(vkCreateCommandPool(s.device, &pool, nullptr, &s.commandPool), "command.pool");
    if (result) {
        VkCommandBufferAllocateInfo alloc{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        alloc.commandPool = s.commandPool; alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; alloc.commandBufferCount = 1;
        result = s.check(vkAllocateCommandBuffers(s.device, &alloc, &s.command), "command.allocate");
    }
    VkSemaphoreCreateInfo sem{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    if (result) result = s.check(vkCreateSemaphore(s.device, &sem, nullptr, &s.acquired), "acquire.semaphore");
    VkFenceCreateInfo fence{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO}; fence.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    if (result) result = s.check(vkCreateFence(s.device, &fence, nullptr, &s.fence), "frame.fence");
    if (result) result = s.buffer(sizeof(Uniforms), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, s.uniform);
    std::array<VkDescriptorSetLayoutBinding, 4> bindings{{
        {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}}};
    VkDescriptorSetLayoutCreateInfo layout{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layout.bindingCount = static_cast<std::uint32_t>(bindings.size()); layout.pBindings = bindings.data();
    if (result) result = s.check(vkCreateDescriptorSetLayout(s.device, &layout, nullptr, &s.descriptorLayout), "descriptor.layout");
    std::array<VkDescriptorPoolSize, 2> sizes{{{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, kResources}, {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kResources * 3}}};
    VkDescriptorPoolCreateInfo descriptors{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    descriptors.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT; descriptors.maxSets = kResources;
    descriptors.poolSizeCount = 2; descriptors.pPoolSizes = sizes.data();
    if (result) result = s.check(vkCreateDescriptorPool(s.device, &descriptors, nullptr, &s.descriptorPool), "descriptor.pool");
    VkPushConstantRange push{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(Push)};
    VkPipelineLayoutCreateInfo pipeline{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pipeline.setLayoutCount = 1; pipeline.pSetLayouts = &s.descriptorLayout; pipeline.pushConstantRangeCount = 1; pipeline.pPushConstantRanges = &push;
    if (result) result = s.check(vkCreatePipelineLayout(s.device, &pipeline, nullptr, &s.pipelineLayout), "pipeline.layout");
    if (result) { s.log("swapchain.create.begin"); result = s.makeSwapchain(); }
    if (!result) { shutdown(); return result; }
    s.initialized = true;
    return result;
}
Capabilities VulkanBackend::capabilities() const noexcept {
    if (!state_ || !state_->initialized) return {};
    return {true, true, true, true, true, false, true, false, false, false, true,
            kResources, kResources, kResources, kDraws, 4, kTargets};
}
Status VulkanBackend::createMesh(MeshView mesh, MeshHandle& handle) noexcept {
    handle = {};
    auto valid = validateMesh(mesh); if (!valid) return valid;
    if (!state_ || !state_->initialized) return error(ErrorCode::InvalidState, "backend not initialized");
    auto& s = *state_;
    auto status = s.idle(); if (!status) return status;
    for (std::uint32_t i = 0; i < kResources; ++i) if (!s.meshes[i].active) {
        auto& slot = s.meshes[i];
        status = s.upload(mesh.vertices.data(), mesh.vertices.size_bytes(), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, slot.vertices);
        if (status) status = s.upload(mesh.indices.data(), mesh.indices.size_bytes(), VK_BUFFER_USAGE_INDEX_BUFFER_BIT, slot.indices);
        if (!status) { s.freeBuffer(slot.vertices); s.freeBuffer(slot.indices); return status; }
        slot.active = true; slot.dynamic = false;
        slot.vertexCapacity = slot.indexCapacity = 0;
        slot.count = static_cast<std::uint32_t>(mesh.indices.size());
        handle = {i, slot.generation}; s.log("mesh.ready"); return status;
    }
    return error(ErrorCode::CapacityExceeded, "mesh capacity");
}
Status VulkanBackend::createDynamicMesh(std::size_t vertexCapacity, std::size_t indexCapacity,
                                        MeshHandle& handle) noexcept {
    handle = {};
    if (!vertexCapacity || !indexCapacity || vertexCapacity > kMaxMeshVertices ||
        indexCapacity > kMaxMeshIndices)
        return error(ErrorCode::InvalidArgument, "dynamic mesh capacity");
    if (!state_ || !state_->initialized) return error(ErrorCode::InvalidState, "backend not initialized");
    auto& s = *state_;
    auto status = s.idle(); if (!status) return status;
    for (std::uint32_t i = 0; i < kResources; ++i) if (!s.meshes[i].active) {
        auto& slot = s.meshes[i];
        constexpr auto properties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        status = s.buffer(vertexCapacity * sizeof(Vertex), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, properties, slot.vertices);
        if (status) status = s.buffer(indexCapacity * sizeof(std::uint32_t), VK_BUFFER_USAGE_INDEX_BUFFER_BIT, properties, slot.indices);
        if (!status) { s.freeBuffer(slot.vertices); s.freeBuffer(slot.indices); return status; }
        slot.count = 0; slot.vertexCapacity = vertexCapacity; slot.indexCapacity = indexCapacity;
        slot.dynamic = true; slot.active = true; handle = {i, slot.generation};
        return Status::success();
    }
    return error(ErrorCode::CapacityExceeded, "mesh capacity");
}
Status VulkanBackend::updateDynamicMesh(MeshHandle handle, MeshView mesh) noexcept {
    if (!state_ || !state_->initialized || state_->recording || state_->lost)
        return error(ErrorCode::InvalidState, "dynamic mesh update outside idle frame boundary");
    auto& s = *state_;
    if (handle.index >= kResources || !s.meshes[handle.index].active ||
        s.meshes[handle.index].generation != handle.generation)
        return error(ErrorCode::NotFound, "stale mesh handle");
    auto& slot = s.meshes[handle.index];
    if (!slot.dynamic) return error(ErrorCode::InvalidArgument, "mesh is not dynamic");
    auto status = validateMesh(mesh); if (!status) return status;
    if (mesh.vertices.size() > slot.vertexCapacity || mesh.indices.size() > slot.indexCapacity)
        return error(ErrorCode::CapacityExceeded, "dynamic mesh capacity");
    status = s.check(vkWaitForFences(s.device, 1, &s.fence, VK_TRUE, kGpuTimeout), "dynamic.mesh.wait");
    if (!status) return status;
    std::memcpy(slot.vertices.mapped, mesh.vertices.data(), mesh.vertices.size_bytes());
    std::memcpy(slot.indices.mapped, mesh.indices.data(), mesh.indices.size_bytes());
    slot.count = static_cast<std::uint32_t>(mesh.indices.size());
    return Status::success();
}
Status VulkanBackend::createTexture(std::uint32_t width, std::uint32_t height,
                                   std::span<const std::byte> rgba, TextureHandle& handle,
                                   TextureColorSpace colorSpace) noexcept {
    handle = {};
    if (!width || !height || width > 4096 || height > 4096 || rgba.size() != std::uint64_t(width) * height * 4 ||
        (colorSpace != TextureColorSpace::Srgb && colorSpace != TextureColorSpace::Linear))
        return error(ErrorCode::InvalidArgument, "texture dimensions or RGBA size");
    if (!state_ || !state_->initialized) return error(ErrorCode::InvalidState, "backend not initialized");
    auto& s = *state_; auto status = s.idle(); if (!status) return status;
    for (std::uint32_t i = 0; i < kResources; ++i) if (!s.textures[i].active) {
        auto& texture = s.textures[i]; Buffer staging{};
        status = s.buffer(rgba.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, staging);
        if (!status) return status;
        std::memcpy(staging.mapped, rgba.data(), rgba.size());
        const auto format = colorSpace == TextureColorSpace::Srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
        status = s.image(width, height, format, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                         VK_IMAGE_ASPECT_COLOR_BIT, texture.image);
        VkCommandBuffer cmd{};
        if (status) status = s.immediateBegin(cmd);
        if (status) {
            transition(cmd, texture.image.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                       0, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
            VkBufferImageCopy copy{}; copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}; copy.imageExtent = {width, height, 1};
            vkCmdCopyBufferToImage(cmd, staging.buffer, texture.image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
            transition(cmd, texture.image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                       VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
            status = s.immediateEnd(cmd);
        }
        s.freeBuffer(staging);
        VkSamplerCreateInfo sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        sampler.magFilter = sampler.minFilter = VK_FILTER_NEAREST; sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        sampler.addressModeU = sampler.addressModeV = sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        if (status) status = s.check(vkCreateSampler(s.device, &sampler, nullptr, &texture.image.sampler), "texture.sampler");
        if (!status) { s.freeImage(texture.image); return status; }
        texture.active = true; texture.colorSpace = colorSpace; handle = {i, texture.generation}; s.log("texture.ready"); return status;
    }
    return error(ErrorCode::CapacityExceeded, "texture capacity");
}
Status VulkanBackend::createMaterial(const Material& material, MaterialHandle& handle) noexcept {
    handle = {}; auto valid = validateMaterial(material); if (!valid) return valid;
    if (!state_ || !state_->initialized) return error(ErrorCode::InvalidState, "backend not initialized");
    auto& s = *state_; auto status = s.idle(); if (!status) return status;
    auto t = material.texture;
    const bool targetSource = material.targetTexture.valid();
    const Image* baseImage{};
    if (targetSource) {
        const auto target = material.targetTexture;
        if (target.index >= s.targets.size() || !s.targets[target.index].active ||
            s.targets[target.index].generation != target.generation)
            return error(ErrorCode::NotFound, "material render-target handle");
        baseImage = &s.targets[target.index].color;
    } else {
        if (t.index >= kResources || !s.textures[t.index].active ||
            s.textures[t.index].generation != t.generation)
            return error(ErrorCode::NotFound, "material texture handle");
        baseImage = &s.textures[t.index].image;
    }
    const auto normal = material.normalTexture.valid() ? material.normalTexture : t;
    if (material.normalTexture.valid() &&
        (normal.index >= kResources || !s.textures[normal.index].active ||
         s.textures[normal.index].generation != normal.generation))
        return error(ErrorCode::NotFound, "material normal texture handle");
    if (material.normalTexture.valid() && s.textures[normal.index].colorSpace != TextureColorSpace::Linear)
        return error(ErrorCode::InvalidValue, "normal texture must use linear color space");
    const auto pbr = material.metallicRoughnessTexture.valid() ? material.metallicRoughnessTexture : t;
    if (material.metallicRoughnessTexture.valid() &&
        (pbr.index >= kResources || !s.textures[pbr.index].active ||
         s.textures[pbr.index].generation != pbr.generation))
        return error(ErrorCode::NotFound, "material metallic-roughness texture handle");
    if (material.metallicRoughnessTexture.valid() && s.textures[pbr.index].colorSpace != TextureColorSpace::Linear)
        return error(ErrorCode::InvalidValue, "metallic-roughness texture must use linear color space");
    for (std::uint32_t i = 0; i < kResources; ++i) if (!s.materials[i].active) {
        auto& slot = s.materials[i];
        VkDescriptorSetAllocateInfo alloc{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        alloc.descriptorPool = s.descriptorPool; alloc.descriptorSetCount = 1; alloc.pSetLayouts = &s.descriptorLayout;
        status = s.check(vkAllocateDescriptorSets(s.device, &alloc, &slot.descriptor), "material.descriptor");
        if (!status) return status;
        slot.material = material;
        s.updateMaterialDescriptor(slot, baseImage);
        slot.active = true;
        if (targetSource) ++s.targets[material.targetTexture.index].references;
        else ++s.textures[t.index].references;
        if (material.normalTexture.valid()) ++s.textures[normal.index].references;
        if (material.metallicRoughnessTexture.valid()) ++s.textures[pbr.index].references;
        handle = {i, slot.generation}; return status;
    }
    return error(ErrorCode::CapacityExceeded, "material capacity");
}
Status VulkanBackend::destroyMesh(MeshHandle h) noexcept {
    if (!state_ || !state_->initialized) return error(ErrorCode::InvalidState, "backend not initialized");
    auto& s = *state_;
    if (h.index >= kResources || !s.meshes[h.index].active || s.meshes[h.index].generation != h.generation)
        return error(ErrorCode::NotFound, "stale mesh handle");
    auto status = s.idle(); if (!status) return status;
    auto& mesh = s.meshes[h.index]; s.freeBuffer(mesh.vertices); s.freeBuffer(mesh.indices);
    mesh.active = false; mesh.dynamic = false; mesh.count = 0;
    mesh.vertexCapacity = 0; mesh.indexCapacity = 0;
    if (++mesh.generation == 0) ++mesh.generation; return status;
}
Status VulkanBackend::destroyTexture(TextureHandle h) noexcept {
    if (!state_ || !state_->initialized) return error(ErrorCode::InvalidState, "backend not initialized");
    auto& s = *state_;
    if (h.index >= kResources || !s.textures[h.index].active || s.textures[h.index].generation != h.generation)
        return error(ErrorCode::NotFound, "stale texture handle");
    if (s.textures[h.index].references) return error(ErrorCode::InvalidState, "texture referenced by materials");
    auto status = s.idle(); if (!status) return status;
    auto& texture = s.textures[h.index]; s.freeImage(texture.image); texture.active = false;
    if (++texture.generation == 0) ++texture.generation; return status;
}
Status VulkanBackend::destroyMaterial(MaterialHandle h) noexcept {
    if (!state_ || !state_->initialized) return error(ErrorCode::InvalidState, "backend not initialized");
    auto& s = *state_;
    if (h.index >= kResources || !s.materials[h.index].active || s.materials[h.index].generation != h.generation)
        return error(ErrorCode::NotFound, "stale material handle");
    auto status = s.idle(); if (!status) return status;
    auto& material = s.materials[h.index];
    status = s.check(vkFreeDescriptorSets(s.device, s.descriptorPool, 1, &material.descriptor), "material.free");
    if (!status) return status;
    if (material.material.targetTexture.valid())
        --s.targets[material.material.targetTexture.index].references;
    else
        --s.textures[material.material.texture.index].references;
    if (material.material.normalTexture.valid()) --s.textures[material.material.normalTexture.index].references;
    if (material.material.metallicRoughnessTexture.valid()) --s.textures[material.material.metallicRoughnessTexture.index].references;
    material.active = false; material.descriptor = {};
    if (++material.generation == 0) ++material.generation; return status;
}
Status VulkanBackend::createTarget(std::uint32_t width, std::uint32_t height, TargetHandle& handle) noexcept {
    handle = {};
    if (!width || !height || width > 4096 || height > 4096)
        return error(ErrorCode::InvalidArgument, "render target dimensions");
    if (!state_ || !state_->initialized) return error(ErrorCode::InvalidState, "backend not initialized");
    auto& s = *state_;
    auto status = s.idle();
    if (!status) return status;
    for (std::uint32_t i = 0; i < kTargets; ++i) {
        auto& target = s.targets[i];
        if (target.active) continue;
        status = s.makeTarget(width, height, target);
        if (!status) return status;
        handle = {i, target.generation};
        s.log("target.ready");
        return Status::success();
    }
    return error(ErrorCode::CapacityExceeded, "render target capacity");
}
Status VulkanBackend::resizeTarget(TargetHandle handle, std::uint32_t width, std::uint32_t height) noexcept {
    if (!state_ || !state_->initialized) return error(ErrorCode::InvalidState, "backend not initialized");
    auto& s = *state_;
    if (handle.index >= s.targets.size() || !s.targets[handle.index].active ||
        s.targets[handle.index].generation != handle.generation)
        return error(ErrorCode::NotFound, "stale render target handle");
    if (!width || !height || width > 4096 || height > 4096)
        return error(ErrorCode::InvalidArgument, "render target dimensions");
    auto status = s.idle();
    if (!status) return status;
    auto& target = s.targets[handle.index];
    TargetSlot replacement{};
    replacement.generation = target.generation;
    replacement.references = target.references;
    status = s.makeTarget(width, height, replacement);
    if (!status) return status;
    s.updateTargetDescriptors(handle.index, replacement.color);
    s.freeTarget(target);
    target = replacement;
    s.log("target.resized");
    return Status::success();
}
Status VulkanBackend::destroyTarget(TargetHandle handle) noexcept {
    if (!state_ || !state_->initialized) return error(ErrorCode::InvalidState, "backend not initialized");
    auto& s = *state_;
    if (handle.index >= s.targets.size() || !s.targets[handle.index].active ||
        s.targets[handle.index].generation != handle.generation)
        return error(ErrorCode::NotFound, "stale render target handle");
    if (s.targets[handle.index].references)
        return error(ErrorCode::InvalidState, "render target referenced by materials");
    auto status = s.idle();
    if (!status) return status;
    auto& target = s.targets[handle.index];
    s.freeTarget(target);
    if (++target.generation == 0) ++target.generation;
    return Status::success();
}
Status VulkanBackend::beginFrame(const FrameState& frame) noexcept {
    return beginFrameTo(frame, {});
}
Status VulkanBackend::beginTargetFrame(TargetHandle target, const FrameState& frame) noexcept {
    if (!target.valid()) return error(ErrorCode::InvalidArgument, "render target handle");
    return beginFrameTo(frame, target);
}
Status VulkanBackend::beginFrameTo(const FrameState& frame, TargetHandle target) noexcept {
    if (!state_ || !state_->initialized || state_->recording || state_->lost)
        return error(ErrorCode::InvalidState, "frame lifecycle or device lost");
    if (!finite(frame.viewProjection)) return error(ErrorCode::InvalidValue, "camera matrix");
    auto validVector = [](Vec4 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z) && std::isfinite(v.w); };
    if (!validVector(frame.cameraPosition) || !validVector(frame.lightDirectionAmbient) ||
        !validVector(frame.lightColorIntensity) || !validVector(frame.clearColor))
        return error(ErrorCode::InvalidValue, "frame uniforms");
    for (auto light : frame.pointLights) if (!validVector(light.positionRadius) || !validVector(light.colorIntensity))
        return error(ErrorCode::InvalidValue, "point light uniforms");
    auto& s = *state_;
    const bool targetFrame = target.valid();
    if (targetFrame && (target.index >= s.targets.size() || !s.targets[target.index].active ||
                        s.targets[target.index].generation != target.generation))
        return error(ErrorCode::NotFound, "stale render target handle");
    auto status = s.check(vkWaitForFences(s.device, 1, &s.fence, VK_TRUE, kGpuTimeout), "frame.wait");
    if (!status) return status;
    if (!targetFrame && s.recreate) {
        status = s.idle(); if (!status) return status;
        s.freeSwapchain(); status = s.makeSwapchain(); if (!status) return status;
    }
    if (!targetFrame) {
        auto acquire = vkAcquireNextImageKHR(s.device, s.swapchain, kGpuTimeout, s.acquired, VK_NULL_HANDLE, &s.currentImage);
        if (acquire == VK_ERROR_OUT_OF_DATE_KHR) { s.recreate = true; return error(ErrorCode::InvalidState, "swapchain out of date; retry frame"); }
        if (acquire == VK_SUBOPTIMAL_KHR) s.recreate = true;
        else { status = s.check(acquire, "frame.acquire"); if (!status) return status; }
    } else {
        s.activeTarget = target.index;
    }
    s.offscreen = targetFrame;
    s.recording = true; s.frameStart = std::chrono::steady_clock::now(); s.drawCount = 0;
    Uniforms uniforms{frame.viewProjection, frame.cameraPosition, frame.lightDirectionAmbient, frame.lightColorIntensity, frame.pointLights};
    std::memcpy(s.uniform.mapped, &uniforms, sizeof(uniforms));
    status = s.check(vkResetCommandBuffer(s.command, 0), "frame.command.reset");
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    if (status) status = s.check(vkBeginCommandBuffer(s.command, &begin), "frame.command.begin");
    if (!status) { s.lost = true; return status; }
    std::array<VkClearValue, 2> clear{};
    clear[0].color = {{frame.clearColor.x, frame.clearColor.y, frame.clearColor.z, frame.clearColor.w}};
#ifdef ECHO_PS5_VULKAN
    const auto toSrgb = [](float value) noexcept {
        value = std::clamp(value, 0.0F, 1.0F);
        return value <= 0.0031308F ? value * 12.92F : 1.055F * std::pow(value, 1.0F / 2.4F) - 0.055F;
    };
    for (int i = 0; i < 3; ++i) clear[0].color.float32[i] = toSrgb(clear[0].color.float32[i]);
#endif
    clear[1].depthStencil = {1, 0};
    VkRenderPassBeginInfo pass{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    const VkExtent2D renderExtent = targetFrame
        ? VkExtent2D{s.targets[target.index].width, s.targets[target.index].height} : s.extent;
    pass.renderPass = s.renderPass;
    pass.framebuffer = targetFrame ? s.targets[target.index].framebuffer : s.framebuffers[s.currentImage];
    pass.renderArea = {{0, 0}, renderExtent}; pass.clearValueCount = 2; pass.pClearValues = clear.data();
    vkCmdBeginRenderPass(s.command, &pass, VK_SUBPASS_CONTENTS_INLINE);
    VkRect2D scissor{{0, 0}, renderExtent};
    if (s.logicalWidth && s.logicalHeight) {
        float scale = std::min(static_cast<float>(renderExtent.width) / s.logicalWidth,
                               static_cast<float>(renderExtent.height) / s.logicalHeight);
        if (scale >= 1.0F) scale = std::floor(scale);
        scissor.extent.width = static_cast<std::uint32_t>(s.logicalWidth * scale);
        scissor.extent.height = static_cast<std::uint32_t>(s.logicalHeight * scale);
        scissor.offset.x = static_cast<std::int32_t>((renderExtent.width - scissor.extent.width) / 2);
        scissor.offset.y = static_cast<std::int32_t>((renderExtent.height - scissor.extent.height) / 2);
    }
    VkViewport viewport{static_cast<float>(scissor.offset.x), static_cast<float>(scissor.offset.y),
                        static_cast<float>(scissor.extent.width), static_cast<float>(scissor.extent.height), 0, 1};
    vkCmdSetViewport(s.command, 0, 1, &viewport); vkCmdSetScissor(s.command, 0, 1, &scissor);
    return Status::success();
}
Status VulkanBackend::submit(const Draw& draw) noexcept {
    if (!state_ || !state_->recording || state_->lost) return error(ErrorCode::InvalidState, "submit outside frame");
    auto& s = *state_;
    if (s.drawCount == kDraws) return error(ErrorCode::CapacityExceeded, "draw capacity");
    if (!finite(draw.model) || !std::isfinite(draw.cameraDistance)) return error(ErrorCode::InvalidValue, "draw transform or distance");
    for (float component : {draw.tint.x, draw.tint.y, draw.tint.z, draw.tint.w})
        if (!std::isfinite(component) || component < 0 || component > 1)
            return error(ErrorCode::InvalidValue, "draw tint");
    for (float component : {draw.uvTransform.x, draw.uvTransform.y,
                            draw.uvTransform.z, draw.uvTransform.w})
        if (!std::isfinite(component)) return error(ErrorCode::InvalidValue, "draw UV transform");
    if (draw.mesh.index >= kResources || !s.meshes[draw.mesh.index].active || s.meshes[draw.mesh.index].generation != draw.mesh.generation ||
        draw.material.index >= kResources || !s.materials[draw.material.index].active || s.materials[draw.material.index].generation != draw.material.generation)
        return error(ErrorCode::NotFound, "stale draw resource");
    const auto& material = s.materials[draw.material.index].material;
    if (material.targetTexture.valid()) {
        const auto& source = s.targets[material.targetTexture.index];
        if (s.offscreen && material.targetTexture.index == s.activeTarget)
            return error(ErrorCode::InvalidState, "render-target feedback loop");
        if (!source.contentsInitialized)
            return error(ErrorCode::InvalidState, "render target has no completed contents");
    }
    auto count = draw.indexCount ? draw.indexCount : s.meshes[draw.mesh.index].count;
    if (!count || count % 3 || draw.firstIndex > s.meshes[draw.mesh.index].count ||
        count > s.meshes[draw.mesh.index].count - draw.firstIndex) return error(ErrorCode::InvalidArgument, "submesh draw range");
    if (!draw.overlay) {
        const auto& m = draw.model.values;
        float determinant = m[0]*(m[5]*m[10]-m[9]*m[6])-m[4]*(m[1]*m[10]-m[9]*m[2])+m[8]*(m[1]*m[6]-m[5]*m[2]);
        if (std::abs(determinant) < 1e-8F) return error(ErrorCode::InvalidValue, "singular normal transform");
    }
    s.draws[s.drawCount++] = draw; return Status::success();
}
Status VulkanBackend::endFrame() noexcept {
    if (!state_ || !state_->recording || state_->lost) return error(ErrorCode::InvalidState, "end outside frame");
    auto& s = *state_;
    // Stable bounded insertion sort preserves submission order for equal-depth alpha/UI.
    for (std::uint32_t i = 1; i < s.drawCount; ++i) {
        Draw draw = s.draws[i];
        std::uint32_t at = i;
        while (at && drawBefore(draw, s.materials[draw.material.index].material.alpha,
                               s.draws[at - 1], s.materials[s.draws[at - 1].material.index].material.alpha)) {
            s.draws[at] = s.draws[at - 1];
            --at;
        }
        s.draws[at] = draw;
    }
    s.stats.drawCalls = s.drawCount; s.stats.triangles = 0;
    for (std::uint32_t i = 0; i < s.drawCount; ++i) {
        const auto& draw = s.draws[i]; const auto& mesh = s.meshes[draw.mesh.index];
        const auto& material = s.materials[draw.material.index]; auto count = draw.indexCount ? draw.indexCount : mesh.count;
        auto pipeline = draw.overlay ? 3U : static_cast<unsigned>(material.material.alpha);
        vkCmdBindPipeline(s.command, VK_PIPELINE_BIND_POINT_GRAPHICS, s.pipelines[pipeline]);
        VkDeviceSize offset{}; vkCmdBindVertexBuffers(s.command, 0, 1, &mesh.vertices.buffer, &offset);
        vkCmdBindIndexBuffer(s.command, mesh.indices.buffer, 0, VK_INDEX_TYPE_UINT32);
        vkCmdBindDescriptorSets(s.command, VK_PIPELINE_BIND_POINT_GRAPHICS, s.pipelineLayout, 0, 1, &material.descriptor, 0, nullptr);
        const auto& m = material.material;
        Vec4 color{m.baseColor.x * draw.tint.x, m.baseColor.y * draw.tint.y,
                   m.baseColor.z * draw.tint.z, m.baseColor.w * draw.tint.w};
        Push push{draw.model, color, {m.emission.x, m.emission.y, m.emission.z, m.metallic},
                  {m.roughness, static_cast<float>(m.model),
                   (draw.overlay ? 1.0F : 0.0F) + (m.metallicRoughnessTexture.valid() ? 2.0F : 0.0F),
                   m.normalTexture.valid() ? m.normalScale : -1.0F},
                  draw.uvTransform};
        vkCmdPushConstants(s.command, s.pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);
        vkCmdDrawIndexed(s.command, count, 1, draw.firstIndex, 0, 0); s.stats.triangles += count / 3;
    }
    vkCmdEndRenderPass(s.command);
    const bool targetFrame = s.offscreen;
    bool capture = s.capturePath[0] != '\0';
    const VkExtent2D captureExtent = targetFrame
        ? VkExtent2D{s.targets[s.activeTarget].width, s.targets[s.activeTarget].height} : s.extent;
    VkImage outputImage = targetFrame ? s.targets[s.activeTarget].color.image : s.images[s.currentImage];
    Status status = Status::success();
    const auto readbackBytes = std::uint64_t(captureExtent.width) * captureExtent.height * 4;
    if (capture && s.readback.buffer && s.readback.bytes < readbackBytes) s.freeBuffer(s.readback);
    if (capture && !s.readback.buffer)
        status = s.buffer(readbackBytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, s.readback);
    if (!status) { s.lost = true; return status; }
    if (capture) {
        transition(s.command, outputImage, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
        VkBufferImageCopy copy{}; copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copy.imageExtent = {captureExtent.width, captureExtent.height, 1};
        vkCmdCopyImageToBuffer(s.command, outputImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, s.readback.buffer, 1, &copy);
        VkBufferMemoryBarrier barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.buffer = s.readback.buffer; barrier.size = VK_WHOLE_SIZE;
        vkCmdPipelineBarrier(s.command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 0, nullptr, 1, &barrier, 0, nullptr);
    }
    if (targetFrame) {
        transition(s.command, outputImage, capture ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                   capture ? VK_ACCESS_TRANSFER_READ_BIT : VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                   VK_ACCESS_SHADER_READ_BIT,
                   capture ? VK_PIPELINE_STAGE_TRANSFER_BIT : VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    } else {
        transition(s.command, outputImage,
                   capture ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                   VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                   capture ? VK_ACCESS_TRANSFER_READ_BIT : VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                   0, capture ? VK_PIPELINE_STAGE_TRANSFER_BIT : VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                   VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
    }
    status = s.check(vkEndCommandBuffer(s.command), "frame.command.end");
    if (status) status = s.check(vkResetFences(s.device, 1, &s.fence), "frame.fence.reset");
    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.waitSemaphoreCount = targetFrame ? 0U : 1U;
    submit.pWaitSemaphores = targetFrame ? nullptr : &s.acquired;
    submit.pWaitDstStageMask = targetFrame ? nullptr : &waitStage;
    submit.commandBufferCount = 1; submit.pCommandBuffers = &s.command;
    submit.signalSemaphoreCount = targetFrame ? 0U : 1U;
    submit.pSignalSemaphores = targetFrame ? nullptr : &s.finished[s.currentImage];
    if (status) status = s.check(vkQueueSubmit(s.queue, 1, &submit, s.fence), "frame.submit");
    if (!status) { s.lost = true; return status; }
    if (targetFrame) s.targets[s.activeTarget].contentsInitialized = true;
    if (!targetFrame && s.stats.frames == 0) s.log("frame.first.submit");
    s.recording = false;
    s.offscreen = false;
    if (!targetFrame) {
        VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
        present.waitSemaphoreCount = 1; present.pWaitSemaphores = &s.finished[s.currentImage];
        present.swapchainCount = 1; present.pSwapchains = &s.swapchain; present.pImageIndices = &s.currentImage;
        auto result = vkQueuePresentKHR(s.queue, &present);
        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) s.recreate = true;
        else { status = s.check(result, "frame.present"); if (!status) { s.lost = true; return status; } }
        if (s.stats.frames == 0) s.log("frame.first.present");
    }
    if (!targetFrame) ++s.stats.frames;
    s.stats.cpuFrameMilliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - s.frameStart).count();
    if (capture) {
        status = s.check(vkWaitForFences(s.device, 1, &s.fence, VK_TRUE, kGpuTimeout), "capture.wait");
        if (!status) return status;
#ifdef ECHO_PS5_VULKAN
        FILE* file = std::fopen(s.capturePath.data(), "wb");
#else
        FILE* file{}; fopen_s(&file, s.capturePath.data(), "wb");
#endif
        if (!file) return error(ErrorCode::StorageFailure, "capture file open");
        bool written = std::fprintf(file, "P6\n%u %u\n255\n", captureExtent.width, captureExtent.height) > 0;
        auto* pixels = static_cast<const unsigned char*>(s.readback.mapped);
        bool bgra = s.format == VK_FORMAT_B8G8R8A8_SRGB || s.format == VK_FORMAT_B8G8R8A8_UNORM;
        std::array<unsigned char, 3 * 4096> row{};
        if (captureExtent.width > 4096) written = false;
        for (std::uint32_t y = 0; y < captureExtent.height && written; ++y) {
            for (std::uint32_t x = 0; x < captureExtent.width; ++x) {
                auto p = pixels + (std::uint64_t(y) * captureExtent.width + x) * 4;
                row[x*3] = p[bgra ? 2 : 0]; row[x*3+1] = p[1]; row[x*3+2] = p[bgra ? 0 : 2];
            }
            written = std::fwrite(row.data(), 3, captureExtent.width, file) == captureExtent.width;
        }
        if (std::fclose(file) != 0) written = false;
        s.capturePath[0] = '\0';
        if (!written) return error(ErrorCode::StorageFailure, "capture write");
        s.log("capture.ready");
    }
    return Status::success();
}
Status VulkanBackend::resize(std::uint32_t w, std::uint32_t h) noexcept {
    if (!state_ || !state_->initialized || state_->recording) return error(ErrorCode::InvalidState, "resize lifecycle");
    if (!w || !h || w > 4096 || h > 4096) return error(ErrorCode::InvalidArgument, "resize dimensions");
    state_->width = w; state_->height = h; state_->recreate = true; return Status::success();
}
Status VulkanBackend::captureNextFrame(const char* path) noexcept {
    if (!state_ || !state_->initialized || !state_->transferSupported) return error(ErrorCode::MissingCapability, "swapchain readback unavailable");
    if (!path || !path[0] || std::strlen(path) >= state_->capturePath.size()) return error(ErrorCode::InvalidArgument, "capture path");
    if (state_->capturePath[0]) return error(ErrorCode::InvalidState, "capture already pending");
    std::memcpy(state_->capturePath.data(), path, std::strlen(path) + 1); return Status::success();
}
Diagnostics VulkanBackend::diagnostics() const noexcept {
    if (!state_) return {};
    auto result = state_->stats; result.validationErrors = state_->validationErrors.load(); return result;
}
void VulkanBackend::shutdown() noexcept {
    if (!state_) return;
    auto& s = *state_;
    if (!s.instance && !s.device) return;
    s.recording = false;
    if (s.device) {
        (void)s.check(vkDeviceWaitIdle(s.device), "shutdown.wait");
        s.freeSwapchain();
        if (s.descriptorPool) vkDestroyDescriptorPool(s.device, s.descriptorPool, nullptr);
        for (auto& material : s.materials) { material.active = false; material.descriptor = {}; if (++material.generation == 0) ++material.generation; }
        for (auto& mesh : s.meshes) { s.freeBuffer(mesh.vertices); s.freeBuffer(mesh.indices); mesh.active = false; if (++mesh.generation == 0) ++mesh.generation; }
        for (auto& texture : s.textures) { s.freeImage(texture.image); texture.active = false; texture.references = 0; if (++texture.generation == 0) ++texture.generation; }
        for (auto& target : s.targets) {
            s.freeTarget(target);
            target.references = 0;
            if (++target.generation == 0) ++target.generation;
        }
        s.freeBuffer(s.uniform);
        if (s.pipelineLayout) vkDestroyPipelineLayout(s.device, s.pipelineLayout, nullptr);
        if (s.descriptorLayout) vkDestroyDescriptorSetLayout(s.device, s.descriptorLayout, nullptr);
        if (s.fence) vkDestroyFence(s.device, s.fence, nullptr);
        if (s.acquired) vkDestroySemaphore(s.device, s.acquired, nullptr);
        if (s.commandPool) vkDestroyCommandPool(s.device, s.commandPool, nullptr);
        vkDestroyDevice(s.device, nullptr);
    }
    if (s.surface) vkDestroySurfaceKHR(s.instance, s.surface, nullptr);
    if (s.debug) {
        auto destroy = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(s.instance, "vkDestroyDebugUtilsMessengerEXT"));
        if (destroy) destroy(s.instance, s.debug, nullptr);
    }
    if (s.instance) vkDestroyInstance(s.instance, nullptr);
    s.instance = {}; s.device = {}; s.surface = {}; s.debug = {}; s.physical = {}; s.commandPool = {};
    s.descriptorPool = {}; s.descriptorLayout = {}; s.pipelineLayout = {}; s.fence = {}; s.acquired = {};
    s.initialized = false; s.lost = false; s.recreate = false; s.capturePath[0] = '\0'; s.stats.frames = 0;
    s.log("shutdown.complete");
}
}
