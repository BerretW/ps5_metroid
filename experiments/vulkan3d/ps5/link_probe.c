#include <vulkan/vulkan.h>

/* Link-only feasibility probe, never packaged or executed. */
int echo_vulkan_link_probe(void) {
    VkInstance instance = VK_NULL_HANDLE;
    static const VkInstanceCreateInfo info = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    return vkCreateInstance(&info, 0, &instance);
}
