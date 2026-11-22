#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
vk_icdGetInstanceProcAddr(VkInstance instance, const char *name);

int echo_radv_link_probe(void)
{
    PFN_vkCreateInstance create_instance =
        (PFN_vkCreateInstance)vk_icdGetInstanceProcAddr(VK_NULL_HANDLE, "vkCreateInstance");
    if (create_instance == 0) {
        return -1;
    }
    const VkInstanceCreateInfo info = { .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
    VkInstance instance = VK_NULL_HANDLE;
    return create_instance(&info, 0, &instance);
}
