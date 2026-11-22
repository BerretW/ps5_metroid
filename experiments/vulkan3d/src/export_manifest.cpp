#include "echo/framework/rendering_data.hpp"
#include "echo/framework/capability_manifest.hpp"
#include "echo/framework/vfx.hpp"
#include <array>
#include <cstdio>

int main(int argc, char** argv) {
    if (argc!=2) { std::fprintf(stderr,"Usage: echo_rendering_manifest <output.json>\n"); return 2; }
    using namespace echo::framework;
    ComponentTypeRegistry types;
    auto status=echo::rendering::registerRenderingComponentTypes(types);
    const std::array<ManifestCapabilityDescriptor,5> capabilities{{
        {"echo.rendering.meshes"},{"echo.rendering.sprites"},{"echo.rendering.depth"},
        {"echo.rendering.materials"},{"echo.vfx.cpu_particles"}}};
    echo::vfx::VfxModule vfx;
    const auto& info=vfx.info();
    const std::array<ManifestModuleDescriptor,1> modules{{
        {info.id,info.name,info.version,info.required,info.optional,info.provides,info.initializeAfter}}};
    const std::array<std::string_view,1> constraints{"Native Windows Vulkan only; PS5 RADV linkage blocked"};
    const std::array<ManifestFeatureDescriptor,1> features{{
        {"echo.rendering.vulkan.windows","Experimental mesh, depth, sprite and material backend",constraints}}};
    CapabilityManifestFragment rendering{};
    rendering.modules=modules; rendering.capabilities=capabilities;
    rendering.runtimeFeatures=features;
    const std::array<CapabilityManifestFragment,2> fragments{frameworkCapabilityManifestFragment(),rendering};
    std::array<char,131072> json{}; std::size_t written{};
    if (status) status=exportCapabilityManifest(types,fragments,json.data(),json.size(),written);
    if (!status) { std::fprintf(stderr,"Manifest failed: %.*s\n",static_cast<int>(status.detail.size()),status.detail.data()); return 1; }
    FILE* file{}; fopen_s(&file,argv[1],"wb");
    if (!file) { std::fprintf(stderr,"Manifest output open failed\n"); return 1; }
    bool success=std::fwrite(json.data(),1,written,file)==written;
    if (std::fclose(file)!=0) success=false;
    if (!success) { std::fprintf(stderr,"Manifest output write failed\n"); return 1; }
    return 0;
}
