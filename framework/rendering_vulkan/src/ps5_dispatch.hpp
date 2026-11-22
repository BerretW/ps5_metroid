#pragma once

#define ECHO_VK_GLOBAL_FUNCTIONS(X) \
    X(vkCreateInstance) \
    X(vkEnumerateInstanceExtensionProperties)

#define ECHO_VK_INSTANCE_FUNCTIONS(X) \
    X(vkDestroyInstance) \
    X(vkEnumeratePhysicalDevices) \
    X(vkGetPhysicalDeviceProperties) \
    X(vkGetPhysicalDeviceMemoryProperties) \
    X(vkGetPhysicalDeviceFormatProperties) \
    X(vkGetPhysicalDeviceImageFormatProperties) \
    X(vkGetPhysicalDeviceQueueFamilyProperties) \
    X(vkEnumerateDeviceExtensionProperties) \
    X(vkCreateDevice) \
    X(vkGetDeviceProcAddr) \
    X(vkDestroySurfaceKHR) \
    X(vkGetPhysicalDeviceSurfaceSupportKHR) \
    X(vkGetPhysicalDeviceSurfaceCapabilitiesKHR) \
    X(vkGetPhysicalDeviceSurfaceFormatsKHR) \
    X(vkGetPhysicalDeviceSurfacePresentModesKHR) \
    X(vkGetPhysicalDeviceDisplayPropertiesKHR) \
    X(vkGetDisplayModePropertiesKHR) \
    X(vkGetPhysicalDeviceDisplayPlanePropertiesKHR) \
    X(vkGetDisplayPlaneSupportedDisplaysKHR) \
    X(vkGetDisplayPlaneCapabilitiesKHR) \
    X(vkCreateDisplayPlaneSurfaceKHR)

#define ECHO_VK_DEVICE_FUNCTIONS(X) \
    X(vkDestroyDevice) \
    X(vkDeviceWaitIdle) \
    X(vkGetDeviceQueue) \
    X(vkCreateSwapchainKHR) \
    X(vkDestroySwapchainKHR) \
    X(vkGetSwapchainImagesKHR) \
    X(vkAcquireNextImageKHR) \
    X(vkQueuePresentKHR) \
    X(vkQueueSubmit) \
    X(vkQueueWaitIdle) \
    X(vkCreateCommandPool) \
    X(vkDestroyCommandPool) \
    X(vkAllocateCommandBuffers) \
    X(vkFreeCommandBuffers) \
    X(vkBeginCommandBuffer) \
    X(vkEndCommandBuffer) \
    X(vkResetCommandBuffer) \
    X(vkCreateSemaphore) \
    X(vkDestroySemaphore) \
    X(vkCreateFence) \
    X(vkDestroyFence) \
    X(vkWaitForFences) \
    X(vkResetFences) \
    X(vkCreateBuffer) \
    X(vkDestroyBuffer) \
    X(vkGetBufferMemoryRequirements) \
    X(vkBindBufferMemory) \
    X(vkCreateImage) \
    X(vkDestroyImage) \
    X(vkGetImageMemoryRequirements) \
    X(vkBindImageMemory) \
    X(vkAllocateMemory) \
    X(vkFreeMemory) \
    X(vkMapMemory) \
    X(vkUnmapMemory) \
    X(vkCreateImageView) \
    X(vkDestroyImageView) \
    X(vkCreateSampler) \
    X(vkDestroySampler) \
    X(vkCreateRenderPass) \
    X(vkDestroyRenderPass) \
    X(vkCreateFramebuffer) \
    X(vkDestroyFramebuffer) \
    X(vkCreateShaderModule) \
    X(vkDestroyShaderModule) \
    X(vkCreatePipelineLayout) \
    X(vkDestroyPipelineLayout) \
    X(vkCreateGraphicsPipelines) \
    X(vkDestroyPipeline) \
    X(vkCreateDescriptorSetLayout) \
    X(vkDestroyDescriptorSetLayout) \
    X(vkCreateDescriptorPool) \
    X(vkDestroyDescriptorPool) \
    X(vkAllocateDescriptorSets) \
    X(vkFreeDescriptorSets) \
    X(vkUpdateDescriptorSets) \
    X(vkCmdPipelineBarrier) \
    X(vkCmdCopyBuffer) \
    X(vkCmdCopyBufferToImage) \
    X(vkCmdCopyImageToBuffer) \
    X(vkCmdBeginRenderPass) \
    X(vkCmdEndRenderPass) \
    X(vkCmdSetViewport) \
    X(vkCmdSetScissor) \
    X(vkCmdBindPipeline) \
    X(vkCmdBindVertexBuffers) \
    X(vkCmdBindIndexBuffer) \
    X(vkCmdBindDescriptorSets) \
    X(vkCmdPushConstants) \
    X(vkCmdDrawIndexed)

namespace echo::rendering {

extern PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr;
#define ECHO_DECLARE_VK(name) extern PFN_##name name;
ECHO_VK_GLOBAL_FUNCTIONS(ECHO_DECLARE_VK)
ECHO_VK_INSTANCE_FUNCTIONS(ECHO_DECLARE_VK)
ECHO_VK_DEVICE_FUNCTIONS(ECHO_DECLARE_VK)
#undef ECHO_DECLARE_VK

bool loadGlobalDispatch(const char*& missing) noexcept;
bool loadInstanceDispatch(VkInstance instance, const char*& missing) noexcept;
bool loadDeviceDispatch(VkDevice device, const char*& missing) noexcept;

} // namespace echo::rendering
