#include "platform_surface.hpp"

extern "C" VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
vk_icdGetInstanceProcAddr(VkInstance instance, const char* name);

extern "C" {
__attribute__((visibility("hidden"))) void* __dso_handle = &__dso_handle;
}

namespace echo::rendering {

PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr = vk_icdGetInstanceProcAddr;
#define ECHO_DEFINE_VK(name) PFN_##name name{};
ECHO_VK_GLOBAL_FUNCTIONS(ECHO_DEFINE_VK)
ECHO_VK_INSTANCE_FUNCTIONS(ECHO_DEFINE_VK)
ECHO_VK_DEVICE_FUNCTIONS(ECHO_DEFINE_VK)
#undef ECHO_DEFINE_VK

bool loadGlobalDispatch(const char*& missing) noexcept {
#define ECHO_LOAD_GLOBAL(name) \
    name = reinterpret_cast<PFN_##name>(vkGetInstanceProcAddr(VK_NULL_HANDLE, #name)); \
    if (!name) { missing = #name; return false; }
    ECHO_VK_GLOBAL_FUNCTIONS(ECHO_LOAD_GLOBAL)
#undef ECHO_LOAD_GLOBAL
    return true;
}

bool loadInstanceDispatch(VkInstance instance, const char*& missing) noexcept {
#define ECHO_LOAD_INSTANCE(name) \
    name = reinterpret_cast<PFN_##name>(vkGetInstanceProcAddr(instance, #name)); \
    if (!name) { missing = #name; return false; }
    ECHO_VK_INSTANCE_FUNCTIONS(ECHO_LOAD_INSTANCE)
#undef ECHO_LOAD_INSTANCE
    return true;
}

bool loadDeviceDispatch(VkDevice device, const char*& missing) noexcept {
#define ECHO_LOAD_DEVICE(name) \
    name = reinterpret_cast<PFN_##name>(vkGetDeviceProcAddr(device, #name)); \
    if (!name) { missing = #name; return false; }
    ECHO_VK_DEVICE_FUNCTIONS(ECHO_LOAD_DEVICE)
#undef ECHO_LOAD_DEVICE
    return true;
}

} // namespace echo::rendering
