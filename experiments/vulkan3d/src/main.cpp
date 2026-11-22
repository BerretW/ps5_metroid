#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "echo/framework/vulkan_backend.hpp"
#include "echo/framework/frame_composition.hpp"
#include "echo/framework/rendering_data.hpp"
#include "echo/framework/hierarchy.hpp"
#include "echo/framework/vfx.hpp"
#include "stb/stb_image.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <limits>
#include <span>

using namespace echo::rendering;
namespace {
bool running = true, isometric{};
std::array<char,1024> assetPath{}, shaderPath{};
LRESULT CALLBACK windowProcedure(HWND window, UINT message, WPARAM key, LPARAM param) {
    if (message == WM_CLOSE) { running = false; return 0; }
    if (message == WM_KEYDOWN && key == VK_ESCAPE) { running = false; return 0; }
    if (message == WM_KEYDOWN && key == VK_SPACE) { isometric = !isometric; return 0; }
    return DefWindowProcW(window, message, key, param);
}
bool require(Status status) {
    if (status) return true;
    std::fprintf(stderr, "ECHO VULKAN ERROR [%u] %.*s: %.*s\n", static_cast<unsigned>(status.code),
        static_cast<int>(status.subject.size()), status.subject.data(),
        static_cast<int>(status.detail.size()), status.detail.data());
    return false;
}
struct TargetPassContext {
    VulkanBackend* backend{};
    TargetHandle target{};
    FrameState frame{};
    MeshHandle mesh{};
    MaterialHandle farMaterial{}, nearMaterial{}, sampledMaterial{};
    const char* capturePath{};
    bool checkFeedback{};
};
Status renderTargetPass(void* context) noexcept {
    auto& pass=*static_cast<TargetPassContext*>(context);
    auto status=pass.capturePath ? pass.backend->captureNextFrame(pass.capturePath) : Status::success();
    if (!require(status)) return status;
    status=pass.backend->beginTargetFrame(pass.target,pass.frame);
    if (!require(status)) return status;
    TargetHandle createdDuringFrame{};
    if (pass.backend->resizeTarget(pass.target,64,64).code != ErrorCode::InvalidState ||
        pass.backend->createTarget(16,16,createdDuringFrame).code != ErrorCode::InvalidState ||
        createdDuringFrame.valid()) {
        std::fprintf(stderr,"Render-target mutation was accepted during recording\n");
        return {ErrorCode::InvalidState,"render target","Resource mutation was accepted during recording."};
    }
    if (pass.checkFeedback) {
        Draw feedback{pass.mesh,pass.sampledMaterial};
        feedback.indexCount=6;
        if (pass.backend->submit(feedback).code != ErrorCode::InvalidState) {
            std::fprintf(stderr,"Render-target feedback loop was accepted\n");
            return {ErrorCode::InvalidState,"render target","Feedback loop was accepted."};
        }
    }
    Draw farDraw{pass.mesh,pass.farMaterial,translation({0,0,0.8F})*scaling({0.7F,0.7F,1})};
    farDraw.indexCount=6;
    Draw nearDraw{pass.mesh,pass.nearMaterial,translation({0,0,0.2F})*scaling({0.7F,0.7F,1})};
    nearDraw.indexCount=6;
    status=pass.backend->submit(farDraw);
    if (!require(status)) return status;
    status=pass.backend->submit(nearDraw);
    if (!require(status)) return status;
    status=pass.backend->endFrame();
    if (!require(status)) return status;
    return Status::success();
}
struct TargetSamplePassContext {
    VulkanBackend* backend{};
    TargetHandle output{};
    FrameState frame{};
    MeshHandle mesh{};
    MaterialHandle material{};
    const char* capturePath{};
};
Status renderTargetSamplePass(void* context) noexcept {
    auto& pass=*static_cast<TargetSamplePassContext*>(context);
    auto status=pass.capturePath ? pass.backend->captureNextFrame(pass.capturePath) : Status::success();
    if (!require(status)) return status;
    status=pass.backend->beginTargetFrame(pass.output,pass.frame);
    if (!require(status)) return status;
    Draw draw{pass.mesh,pass.material,scaling({0.9F,0.9F,1})};
    draw.indexCount=6;
    status=pass.backend->submit(draw);
    if (!require(status)) return status;
    status=pass.backend->endFrame();
    if (!require(status)) return status;
    return Status::success();
}
bool digits(VulkanBackend& backend, MeshHandle quad, MaterialHandle material,
            unsigned value);
struct ScheduledSceneFrame {
    VulkanBackend* backend{};
    const SceneDefinition* scene{};
    EntityStore* hierarchy{};
    std::span<const EntityHandle> entities{};
    std::span<const MaterialHandle> materials{};
    std::span<const echo::vfx::Particle> particles{};
    FrameState frame{};
    Vec3 camera{};
    Vec4 spriteUv{};
    float time{};
    unsigned fps{};
    MeshHandle cubeMesh{}, quadMesh{}, dynamicMesh{};
    MaterialHandle particleMaterial{}, spriteMaterial{}, uiMaterial{};
    MaterialHandle glassMaterial{}, atlasMaterial{}, dynamicMaterial{};
    std::span<const Vertex> quadVertices{};
    std::span<const std::uint32_t> quadIndices{};
    const char* capturePath{};
    bool probe{};
};
Status beginSceneFrame(void* context) noexcept {
    auto& pass=*static_cast<ScheduledSceneFrame*>(context);
    auto status=pass.backend->beginFrame(pass.frame);
    if (!status) return status;
    for (std::size_t i=0;i<pass.scene->entities.size();++i) {
        const auto& entity=pass.scene->entities[i];
        auto transform=entity.transform;
        transform.rotation.y+=pass.time*entity.spinRadiansPerSecond;
        status=pass.hierarchy->setTransform(pass.entities[i],{
            transform.position,transform.rotation,transform.scale});
        if (!status) return status;
    }
    return Status::success();
}
Status submitSceneEntities(ScheduledSceneFrame& pass,bool opaque) noexcept {
    for (std::size_t i=0;i<pass.scene->entities.size();++i) {
        const auto& entity=pass.scene->entities[i];
        if (!entity.visible) continue;
        std::size_t materialIndex=pass.scene->materials.size();
        for (std::size_t m=0;m<pass.scene->materials.size();++m) {
            if (pass.scene->materials[m].id==entity.materialId) {
                materialIndex=m;
                break;
            }
        }
        if (materialIndex==pass.scene->materials.size())
            return {ErrorCode::NotFound,"scene material","Entity material is not registered."};
        const bool entityOpaque=
            pass.scene->materials[materialIndex].material.alpha==AlphaMode::Opaque;
        if (entityOpaque!=opaque) continue;
        MeshHandle mesh{};
        if (entity.meshId=="echo:mesh/reactor_core" ||
            entity.meshId=="echo:mesh/cargo_pod") mesh=pass.cubeMesh;
        else if (entity.meshId=="echo:mesh/floor") mesh=pass.quadMesh;
        else return {ErrorCode::NotFound,"scene mesh","Entity mesh has no GPU resource."};
        Mat4 model{};
        auto status=pass.hierarchy->worldMatrix(pass.entities[i],model);
        if (!status) return status;
        Draw draw{mesh,pass.materials[materialIndex],model};
        const Vec3 position{model.values[12],model.values[13],model.values[14]};
        draw.cameraDistance=dot(pass.camera-position,pass.camera-position);
        status=pass.backend->submit(draw);
        if (!status) return status;
    }
    return Status::success();
}
Status submitOpaquePass(void* context) noexcept {
    return submitSceneEntities(*static_cast<ScheduledSceneFrame*>(context),true);
}
Status submitTransparentPass(void* context) noexcept {
    auto& pass=*static_cast<ScheduledSceneFrame*>(context);
    auto status=submitSceneEntities(pass,false);
    if (!status) return status;
    Draw glass{pass.quadMesh,pass.glassMaterial,
        translation({-2,0,0})*scaling({1.5F,2,1})};
    glass.cameraDistance=9;
    return pass.backend->submit(glass);
}
Status submitParticlesPass(void* context) noexcept {
    auto& pass=*static_cast<ScheduledSceneFrame*>(context);
    for (const auto& particle:pass.particles) {
        Draw draw{pass.quadMesh,pass.particleMaterial,
            translation(particle.position)*rotation({0,0.6F,0})*
            scaling({particle.scale,particle.scale,1})};
        draw.tint=particle.color;
        draw.cameraDistance=dot(pass.camera-particle.position,
                                pass.camera-particle.position);
        auto status=pass.backend->submit(draw);
        if (!status) return status;
    }
    return Status::success();
}
Status submitUiPass(void* context) noexcept {
    auto& pass=*static_cast<ScheduledSceneFrame*>(context);
    Draw sprite{pass.quadMesh,pass.spriteMaterial,
        translation({0.8F,-0.74F,0})*scaling({0.13F,0.32F,1})};
    sprite.overlay=true;
    sprite.uvTransform=pass.spriteUv;
    auto status=pass.backend->submit(sprite);
    if (!status) return status;
    if (!digits(*pass.backend,pass.quadMesh,pass.uiMaterial,
                std::min(pass.fps,999U)))
        return {ErrorCode::InvalidState,"frame UI","FPS overlay submission failed."};
    if (!pass.probe) return Status::success();
    if (pass.backend->updateDynamicMesh(pass.dynamicMesh,
            {pass.quadVertices,pass.quadIndices}).code!=ErrorCode::InvalidState ||
        pass.backend->destroyMesh(pass.dynamicMesh).code!=ErrorCode::InvalidState)
        return {ErrorCode::InvalidState,"dynamic mesh",
                "A dynamic mesh mutation was accepted during frame recording."};
    Draw invalid{pass.quadMesh,pass.atlasMaterial};
    invalid.uvTransform.x=std::numeric_limits<float>::quiet_NaN();
    if (pass.backend->submit(invalid).code!=ErrorCode::InvalidValue)
        return {ErrorCode::InvalidValue,"sprite UV","Invalid UV transform was accepted."};
    for (unsigned i=0;i<4;++i) {
        Draw atlasDraw{pass.quadMesh,pass.atlasMaterial,
            translation({-0.75F+0.5F*i,0.58F,0})*scaling({0.36F,0.12F,1})};
        atlasDraw.overlay=true;
        status=spriteUvTransform(2,2,{0,i/2,2,1,(i%2)!=0,false},
                                 atlasDraw.uvTransform);
        if (!status) return status;
        status=pass.backend->submit(atlasDraw);
        if (!status) return status;
    }
    Draw verticalFlip{pass.quadMesh,pass.atlasMaterial,
        translation({0,0.9F,0})*scaling({0.12F,0.08F,1})};
    verticalFlip.overlay=true;
    status=spriteUvTransform(2,2,{0,0,2,2,false,true},
                             verticalFlip.uvTransform);
    if (!status) return status;
    status=pass.backend->submit(verticalFlip);
    if (!status) return status;
    Draw dynamic{pass.dynamicMesh,pass.dynamicMaterial};
    dynamic.overlay=true;
    dynamic.indexCount=6;
    status=pass.backend->submit(dynamic);
    if (!status) return status;
    dynamic.firstIndex=6;
    return pass.backend->submit(dynamic);
}
Status presentSceneFrame(void* context) noexcept {
    auto& pass=*static_cast<ScheduledSceneFrame*>(context);
    if (pass.capturePath) {
        const auto status=pass.backend->captureNextFrame(pass.capturePath);
        if (!status) return status;
    }
    return pass.backend->endFrame();
}
template<std::size_t Size>
bool readAsset(const char* name, std::array<char,Size>& data, std::size_t& size) {
    char path[1024]{};
    std::snprintf(path,sizeof(path),"%s\\%s",assetPath.data(),name);
    FILE* file{}; fopen_s(&file,path,"rb");
    if (!file) { std::fprintf(stderr,"Asset missing: %s\n",path); return false; }
    size=std::fread(data.data(),1,data.size(),file);
    bool valid=!std::ferror(file) && std::fgetc(file)==EOF && size>0;
    std::fclose(file);
    if (!valid) std::fprintf(stderr,"Asset read failed or exceeds bound: %s\n",path);
    return valid;
}
struct Geometry {
    std::array<Vertex, 24> vertices{};
    std::array<std::uint32_t, 36> indices{};
};
Geometry cubeGeometry() {
    Geometry g{};
    const std::array<Vec3, 8> corners{{{-1,-1,-1},{1,-1,-1},{1,1,-1},{-1,1,-1},
                                     {-1,-1,1},{1,-1,1},{1,1,1},{-1,1,1}}};
    const std::array<std::array<unsigned,4>,6> faces{{{4,5,6,7},{1,0,3,2},{0,4,7,3},{5,1,2,6},{7,6,2,3},{0,1,5,4}}};
    const std::array<Vec3,6> normals{{{0,0,1},{0,0,-1},{-1,0,0},{1,0,0},{0,1,0},{0,-1,0}}};
    const std::array<Vec2,4> uv{{{0,1},{1,1},{1,0},{0,0}}};
    for (unsigned f=0;f<6;++f) {
        for (unsigned v=0;v<4;++v) g.vertices[f*4+v] = {corners[faces[f][v]],normals[f],uv[v],{1,1,1,1}};
        const std::array<unsigned,6> order{0,1,2,2,3,0};
        for (unsigned v=0;v<6;++v) g.indices[f*6+v] = f*4+order[v];
    }
    return g;
}
std::array<Vertex,4> quadGeometry(float u = 1, float v = 1) {
    return {{{{-0.5F,-0.5F,0},{0,0,1},{0,v},{1,1,1,1}},
             {{0.5F,-0.5F,0},{0,0,1},{u,v},{1,1,1,1}},
             {{0.5F,0.5F,0},{0,0,1},{u,0},{1,1,1,1}},
             {{-0.5F,0.5F,0},{0,0,1},{0,0},{1,1,1,1}}}};
}
bool loadSprite(VulkanBackend& backend, TextureHandle& texture, int& width, int& height) {
    char path[1200]{};
    std::snprintf(path,sizeof(path),"%s\\player.png",assetPath.data());
    FILE* file{}; fopen_s(&file,path,"rb");
    if (!file) { std::fprintf(stderr,"Sprite file missing: %s\n",path); return false; }
    std::array<unsigned char,262144> encoded{};
    auto count = std::fread(encoded.data(),1,encoded.size(),file);
    bool complete = !std::ferror(file) && std::fgetc(file) == EOF; std::fclose(file);
    if (!complete || !count) { std::fprintf(stderr,"Sprite PNG exceeds input bound or read failed\n"); return false; }
    int channels{};
    auto* pixels = stbi_load_from_memory(encoded.data(),static_cast<int>(count),&width,&height,&channels,4);
    if (!pixels) { std::fprintf(stderr,"Sprite PNG decode failed: %s\n", stbi_failure_reason()); return false; }
    auto status = backend.createTexture(static_cast<unsigned>(width),static_cast<unsigned>(height),
        {reinterpret_cast<const std::byte*>(pixels),static_cast<std::size_t>(width)*height*4},texture);
    stbi_image_free(pixels); return require(status);
}
bool overlay(VulkanBackend& backend, MeshHandle quad, MaterialHandle material, float x, float y, float w, float h) {
    Draw draw{quad,material,translation({x,y,0})*scaling({w,h,1})};
    draw.overlay = true; return require(backend.submit(draw));
}
bool digits(VulkanBackend& backend, MeshHandle quad, MaterialHandle material, unsigned value) {
    const std::array<unsigned,10> masks{0x3f,0x06,0x5b,0x4f,0x66,0x6d,0x7d,0x07,0x7f,0x6f};
    for (int digit=2;digit>=0;--digit) {
        unsigned number = value%10; value/=10;
        float x = -0.91F+digit*0.075F, y=-0.86F;
        const std::array<std::array<float,4>,7> bars{{
            {x,y-0.06F,0.04F,0.01F},{x+0.025F,y-0.03F,0.009F,0.05F},
            {x+0.025F,y+0.03F,0.009F,0.05F},{x,y+0.06F,0.04F,0.01F},
            {x-0.025F,y+0.03F,0.009F,0.05F},{x-0.025F,y-0.03F,0.009F,0.05F},{x,y,0.04F,0.01F}}};
        for (unsigned i=0;i<7;++i) if (masks[number] & (1U<<i))
            if (!overlay(backend,quad,material,bars[i][0],bars[i][1],bars[i][2],bars[i][3])) return false;
    }
    return true;
}
}
int main(int argc, char** argv) {
    std::array<char,1024> executable{};
    const auto pathLength=GetModuleFileNameA(nullptr,executable.data(),static_cast<DWORD>(executable.size()));
    if (!pathLength || pathLength>=executable.size())
        { std::fprintf(stderr,"Executable path unavailable or exceeds bound\n"); return 1; }
    auto* slash=std::strrchr(executable.data(),'\\');
    if (!slash) { std::fprintf(stderr,"Executable directory unavailable\n"); return 1; }
    *slash='\0';
    if (std::snprintf(assetPath.data(),assetPath.size(),"%s\\assets",executable.data())>=static_cast<int>(assetPath.size()) ||
        std::snprintf(shaderPath.data(),shaderPath.size(),"%s\\assets\\shaders",executable.data())>=static_cast<int>(shaderPath.size()))
        { std::fprintf(stderr,"Asset paths exceed bound\n"); return 1; }
    unsigned frameLimit{};
    unsigned initialWidth=1280, initialHeight=720;
    bool validation{}, resizeTest{}, probe{};
    const char* capture{};
    const char* targetCapture{};
    const char* targetSampleCapture{};
    for (int i=1;i<argc;++i) {
        if (!std::strcmp(argv[i],"--frames") && i+1<argc) {
            char* end{}; unsigned long count = std::strtoul(argv[++i],&end,10);
            if (!count || count > 100000 || *end) { std::fprintf(stderr,"Invalid frame count\n"); return 2; }
            frameLimit = static_cast<unsigned>(count);
        } else if (!std::strcmp(argv[i],"--validation")) validation=true;
        else if (!std::strcmp(argv[i],"--resize-test")) resizeTest=true;
        else if (!std::strcmp(argv[i],"--probe")) probe=true;
        else if (!std::strcmp(argv[i],"--1080p")) { initialWidth=1920; initialHeight=1080; }
        else if (!std::strcmp(argv[i],"--isometric")) isometric=true;
        else if (!std::strcmp(argv[i],"--capture") && i+1<argc) capture=argv[++i];
        else if (!std::strcmp(argv[i],"--target-capture") && i+1<argc) targetCapture=argv[++i];
        else if (!std::strcmp(argv[i],"--target-sample-capture") && i+1<argc) targetSampleCapture=argv[++i];
        else { std::fprintf(stderr,"Unknown or incomplete argument: %s\n",argv[i]); return 2; }
    }
    if (capture && !frameLimit) { std::fprintf(stderr,"Capture requires a bounded --frames run\n"); return 2; }
    if (targetCapture && (!frameLimit || !probe)) {
        std::fprintf(stderr,"Offscreen capture requires a bounded --probe run\n"); return 2;
    }
    if (targetSampleCapture && (!frameLimit || !probe)) {
        std::fprintf(stderr,"Offscreen sample capture requires a bounded --probe run\n"); return 2;
    }
    WNDCLASSW cls{}; cls.lpfnWndProc=windowProcedure; cls.hInstance=GetModuleHandleW(nullptr); cls.lpszClassName=L"EchoVulkanExperiment";
    cls.hCursor=LoadCursor(nullptr,IDC_ARROW);
    if (!RegisterClassW(&cls)) { std::fprintf(stderr,"RegisterClass failed: %lu\n",GetLastError()); return 1; }
    RECT bounds{0,0,static_cast<LONG>(initialWidth),static_cast<LONG>(initialHeight)};
    AdjustWindowRect(&bounds,WS_OVERLAPPEDWINDOW,FALSE);
    HWND window=CreateWindowW(cls.lpszClassName,L"Echo Vulkan 3D - Space: perspective/isometric; Esc: quit",
        WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,bounds.right-bounds.left,bounds.bottom-bounds.top,nullptr,nullptr,cls.hInstance,nullptr);
    if (!window) { std::fprintf(stderr,"CreateWindow failed: %lu\n",GetLastError()); return 1; }
    ShowWindow(window,SW_SHOW);
    VulkanBackend backend;
    if (!require(backend.initialize({window,initialWidth,initialHeight,shaderPath.data(),validation}))) { DestroyWindow(window); return 1; }
    auto run = [&]() -> bool {
        auto cube=cubeGeometry(); auto quad=quadGeometry();
        constexpr std::array<std::uint32_t,6> quadIndices{0,1,2,2,3,0};
        MeshHandle cubeMesh{},quadMesh{};
        if (!require(backend.createMesh({cube.vertices,cube.indices},cubeMesh)) ||
            !require(backend.createMesh({quad,quadIndices},quadMesh))) return false;
        std::array<std::byte,16*16*4> checker{};
        for (unsigned y=0;y<16;++y) for (unsigned x=0;x<16;++x) {
            bool bright=((x/4+y/4)%2)==0;
            unsigned at=(y*16+x)*4;
            checker[at]=std::byte(bright?240:35); checker[at+1]=std::byte(bright?160:70);
            checker[at+2]=std::byte(bright?45:170); checker[at+3]=std::byte{255};
        }
        constexpr std::array<std::byte,4> white{std::byte{255},std::byte{255},std::byte{255},std::byte{255}};
        TextureHandle checkerTexture{},whiteTexture{},spriteTexture{};
        if (!require(backend.createTexture(16,16,checker,checkerTexture)) ||
            !require(backend.createTexture(1,1,white,whiteTexture))) return false;
        int spriteWidth{},spriteHeight{};
        if (!loadSprite(backend,spriteTexture,spriteWidth,spriteHeight)) return false;
        Vec4 spriteUv{};
        if (!require(spriteUvTransform(static_cast<unsigned>(spriteWidth),static_cast<unsigned>(spriteHeight),
                                       {0,0,16,24},spriteUv))) return false;
        echo::framework::ContentRegistry content{};
        const echo::framework::ResourceSpec resources[]{
            {"echo:mesh/reactor_core",echo::framework::ResourceType::Generic,false,{}},
            {"echo:mesh/cargo_pod",echo::framework::ResourceType::Generic,false,{}},
            {"echo:mesh/floor",echo::framework::ResourceType::Generic,false,{}},
            {"echo:texture/checker",echo::framework::ResourceType::Generic,false,{}}};
        for (auto& resource:resources) if (!require(content.add(resource))) return false;
        std::array<char,65536> input{}; std::size_t inputSize{};
        echo::framework::JsonError jsonError{};
        SceneDocument sceneDocument{};
        if (!readAsset("scene.json",input,inputSize) ||
            !require(sceneDocument.parse(input.data(),inputSize,jsonError,&content))) return false;
        const auto& scene=*sceneDocument.definition();
        std::array<MaterialHandle,kMaxSceneMaterials> sceneMaterials{};
        for (std::size_t i=0;i<scene.materials.size();++i) {
            auto material=scene.materials[i].material;
            if (!scene.materials[i].textureId.empty() && scene.materials[i].textureId!="echo:texture/checker")
                { std::fprintf(stderr,"No texture factory for scene resource\n"); return false; }
            material.texture=scene.materials[i].textureId.empty()?whiteTexture:checkerTexture;
            if (!require(backend.createMaterial(material,sceneMaterials[i]))) return false;
        }
        EntityStore hierarchy{};
        std::array<EntityHandle,kMaxSceneEntities> sceneEntities{};
        for (std::size_t i=0;i<scene.entities.size();++i) {
            auto t=scene.entities[i].transform;
            if (!require(hierarchy.create({t.position,t.rotation,t.scale},sceneEntities[i]))) return false;
        }
        for (std::size_t i=0;i<scene.entities.size();++i)
            if (scene.entities[i].parentIndex!=UINT32_MAX &&
                !require(hierarchy.setParent(sceneEntities[i],sceneEntities[scene.entities[i].parentIndex]))) return false;
        echo::vfx::EmitterDocument emitterDocument{};
        if (!readAsset("reactor_sparks.json",input,inputSize) ||
            !require(emitterDocument.parse(input.data(),inputSize,jsonError))) return false;
        echo::vfx::VfxModule vfx;
        echo::framework::ModuleRegistry modules;
        echo::framework::ModuleContext moduleContext{};
        if (!require(modules.add(vfx)) || !require(modules.initializeAll(moduleContext))) return false;
        echo::vfx::EmitterHandle emitter{};
        if (!require(vfx.system().createEmitter(emitterDocument.definition()->config,emitter)) ||
            !require(vfx.system().emitBurst(emitter,12))) return false;
        Material particleMaterial{}; particleMaterial.model=MaterialModel::Unlit;
        particleMaterial.alpha=AlphaMode::Additive; particleMaterial.texture=whiteTexture;
        Material spriteMaterial{}; spriteMaterial.model=MaterialModel::Unlit; spriteMaterial.alpha=AlphaMode::Blend; spriteMaterial.texture=spriteTexture;
        Material uiMaterial{}; uiMaterial.model=MaterialModel::Unlit; uiMaterial.texture=whiteTexture; uiMaterial.baseColor={0.3F,1,0.65F,1};
        Material glassMaterial=uiMaterial; glassMaterial.alpha=AlphaMode::Blend; glassMaterial.baseColor={0.1F,0.6F,1,0.25F};
        MaterialHandle particleMat{},spriteMat{},uiMat{},glassMat{};
        if (!require(backend.createMaterial(particleMaterial,particleMat)) ||
            !require(backend.createMaterial(spriteMaterial,spriteMat)) || !require(backend.createMaterial(uiMaterial,uiMat)) ||
            !require(backend.createMaterial(glassMaterial,glassMat))) return false;
        TextureHandle atlasTexture{};
        MaterialHandle atlasMat{};
        MaterialHandle dynamicMat{};
        MeshHandle dynamicMesh{};
        TargetHandle target{},sampleTarget{};
        MaterialHandle targetFarMaterial{},targetNearMaterial{},targetSampleMaterial{};
        if (probe) {
            constexpr std::array<std::byte,16> atlas{
                std::byte{255},std::byte{0},std::byte{0},std::byte{255},
                std::byte{0},std::byte{255},std::byte{0},std::byte{255},
                std::byte{0},std::byte{0},std::byte{255},std::byte{255},
                std::byte{255},std::byte{255},std::byte{0},std::byte{255}};
            Material atlasMaterial{}; atlasMaterial.model=MaterialModel::Unlit;
            if (!require(backend.createTexture(2,2,atlas,atlasTexture))) return false;
            atlasMaterial.texture=atlasTexture;
            if (!require(backend.createMaterial(atlasMaterial,atlasMat))) return false;
            Material dynamicMaterial{}; dynamicMaterial.model=MaterialModel::VertexColor;
            dynamicMaterial.texture=whiteTexture;
            if (!require(backend.createMaterial(dynamicMaterial,dynamicMat)) ||
                !require(backend.createDynamicMesh(8,12,dynamicMesh))) return false;
            MeshHandle invalidCapacity{};
            if (backend.createDynamicMesh(kMaxMeshVertices+1,6,invalidCapacity).code != ErrorCode::InvalidArgument ||
                invalidCapacity.valid() ||
                backend.updateDynamicMesh(quadMesh,{quad,quadIndices}).code != ErrorCode::InvalidArgument) {
                std::fprintf(stderr,"Dynamic mesh capacity or type validation failed\n"); return false;
            }
            auto status=backend.destroyTexture(checkerTexture);
            if (status.code != ErrorCode::InvalidState) { std::fprintf(stderr,"Referenced texture was not rejected\n"); return false; }
            MeshHandle temporary{};
            if (!require(backend.createMesh({quad,quadIndices},temporary)) || !require(backend.destroyMesh(temporary))) return false;
            if (backend.destroyMesh(temporary).code != ErrorCode::NotFound) { std::fprintf(stderr,"Stale handle accepted\n"); return false; }
            TargetHandle invalidTarget{};
            if (backend.createTarget(0,128,invalidTarget).code != ErrorCode::InvalidArgument || invalidTarget.valid() ||
                !backend.capabilities().renderToTexture || !backend.capabilities().targetSampling ||
                backend.capabilities().compute) {
                std::fprintf(stderr,"Render-target capability or dimension validation failed\n"); return false;
            }
            const auto bytesBeforeTarget=backend.diagnostics().allocatedGpuBytes;
            if (!require(backend.createTarget(128,128,target)) ||
                !require(backend.resizeTarget(target,256,128)) ||
                !require(backend.resizeTarget(target,128,128)) ||
                backend.resizeTarget(target,0,128).code != ErrorCode::InvalidArgument) {
                std::fprintf(stderr,"Render-target creation or resize failed\n"); return false;
            }
            const auto bytesWithTarget=backend.diagnostics().allocatedGpuBytes;
            if (bytesWithTarget <= bytesBeforeTarget) {
                std::fprintf(stderr,"Render-target memory was not included in GPU accounting\n"); return false;
            }
            TargetHandle temporaryTarget{};
            if (!require(backend.createTarget(32,32,temporaryTarget)) ||
                !require(backend.destroyTarget(temporaryTarget)) ||
                backend.destroyTarget(temporaryTarget).code != ErrorCode::NotFound ||
                backend.resizeTarget(temporaryTarget,64,64).code != ErrorCode::NotFound) {
                std::fprintf(stderr,"Render-target destruction or stale-handle validation failed\n"); return false;
            }
            if (backend.diagnostics().allocatedGpuBytes != bytesWithTarget) {
                std::fprintf(stderr,"Render-target destruction did not release its accounted memory\n"); return false;
            }
            if (!require(backend.createTarget(128,128,sampleTarget))) return false;
            Material farMaterial{}; farMaterial.model=MaterialModel::Unlit;
            farMaterial.texture=whiteTexture; farMaterial.baseColor={1,0,0,1};
            Material nearMaterial=farMaterial; nearMaterial.baseColor={0,0,1,1};
            if (!require(backend.createMaterial(farMaterial,targetFarMaterial)) ||
                !require(backend.createMaterial(nearMaterial,targetNearMaterial))) return false;
            Material targetMaterial{}; targetMaterial.model=MaterialModel::Unlit;
            targetMaterial.targetTexture=target;
            if (!require(backend.createMaterial(targetMaterial,targetSampleMaterial)) ||
                backend.destroyTarget(target).code != ErrorCode::InvalidState ||
                !require(backend.resizeTarget(target,256,128)) ||
                !require(backend.resizeTarget(target,128,128))) {
                std::fprintf(stderr,"Render-target sampling lifetime or resize failed\n"); return false;
            }
            FrameState emptyFrame{};
            if (!require(backend.beginTargetFrame(sampleTarget,emptyFrame))) return false;
            Draw prematureSample{quadMesh,targetSampleMaterial};
            prematureSample.indexCount=6;
            const auto prematureStatus=backend.submit(prematureSample);
            if (!require(backend.endFrame())) return false;
            if (prematureStatus.code != ErrorCode::InvalidState) {
                std::fprintf(stderr,"Uninitialized render target was sampled\n"); return false;
            }
        }
        std::array<char,4096> manifest{}; std::size_t manifestBytes{};
        if (!require(exportCapabilities(backend.capabilities(),manifest.data(),manifest.size(),manifestBytes))) return false;
        std::printf("%.*s\n",static_cast<int>(manifestBytes),manifest.data());
        auto start=std::chrono::steady_clock::now(), last=start, simulationClock=start;
        float simulationAccumulator{};
        unsigned frames{}, width=initialWidth,height=initialHeight, fps=0, periodFrames{};
        while (running && (!frameLimit || frames<frameLimit)) {
            MSG message{};
            while (PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
            if (!running) break;
            if (resizeTest && frames==20) {
                RECT next{0,0,960,540}; AdjustWindowRect(&next,WS_OVERLAPPEDWINDOW,FALSE);
                if (!SetWindowPos(window,nullptr,0,0,next.right-next.left,next.bottom-next.top,SWP_NOMOVE|SWP_NOZORDER))
                    { std::fprintf(stderr,"Resize failed: %lu\n",GetLastError()); return false; }
            }
            RECT client{}; GetClientRect(window,&client);
            auto nextWidth=static_cast<unsigned>(client.right),nextHeight=static_cast<unsigned>(client.bottom);
            if (!nextWidth || !nextHeight) { WaitMessage(); continue; }
            if (width!=nextWidth || height!=nextHeight) {
                width=nextWidth;height=nextHeight;
                if (!require(backend.resize(width,height))) return false;
            }
            FrameState frame{};
            Mat4 view{},projection{}; const auto& cameraData=scene.camera.camera;
            Vec3 camera=cameraData.position;
            if (!require(lookAt(camera,cameraData.target,cameraData.up,view))) return false;
            if (isometric) {
                if (!require(orthographic(8.0F*width/height,8,cameraData.nearPlane,cameraData.farPlane,projection))) return false;
            } else if (!require(perspective(cameraData.fovRadians,static_cast<float>(width)/height,
                cameraData.nearPlane,cameraData.farPlane,projection))) return false;
            frame.viewProjection=projection*view; frame.cameraPosition={camera.x,camera.y,camera.z,1};
            frame.lightDirectionAmbient=scene.directionalAmbient;
            frame.lightColorIntensity=scene.directionalColorIntensity;
            frame.pointLights=scene.points;
            if (probe && frames==0) {
                TargetPassContext targetPass{&backend,target,{},quadMesh,
                    targetFarMaterial,targetNearMaterial,targetSampleMaterial,targetCapture,false};
                targetPass.frame.clearColor={0.02F,0.03F,0.04F,1};
                TargetPassContext feedbackPass{&backend,target,{},quadMesh,
                    targetFarMaterial,targetNearMaterial,targetSampleMaterial,nullptr,true};
                feedbackPass.frame.clearColor=targetPass.frame.clearColor;
                TargetSamplePassContext samplePass{&backend,sampleTarget,{},quadMesh,
                    targetSampleMaterial,targetSampleCapture};
                samplePass.frame.clearColor={0.01F,0.02F,0.03F,1};
                FramePassScheduler scheduler{};
                RenderPassHandle targetPassHandle{},feedbackPassHandle{},samplePassHandle{};
                FrameResourceHandle targetResource{},sampleResource{};
                if (!require(scheduler.add(RenderPassStage::Opaque,renderTargetPass,
                                           &targetPass,targetPassHandle)) ||
                    !require(scheduler.add(RenderPassStage::Opaque,renderTargetPass,
                                           &feedbackPass,feedbackPassHandle)) ||
                    !require(scheduler.add(RenderPassStage::Postprocessing,renderTargetSamplePass,
                                           &samplePass,samplePassHandle)) ||
                    !require(scheduler.createResource(false,targetResource)) ||
                    !require(scheduler.createResource(false,sampleResource)) ||
                    !require(scheduler.useResource(targetPassHandle,targetResource,
                                                   FrameResourceAccess::Write)) ||
                    !require(scheduler.useResource(samplePassHandle,targetResource,
                                                   FrameResourceAccess::Read)) ||
                    !require(scheduler.useResource(samplePassHandle,sampleResource,
                                                   FrameResourceAccess::Write)) ||
                    !require(scheduler.dependsOn(feedbackPassHandle,targetPassHandle)) ||
                    !require(scheduler.dependsOn(samplePassHandle,feedbackPassHandle)) ||
                    !require(scheduler.execute())) return false;
            }
            auto simulationNow=std::chrono::steady_clock::now();
            simulationAccumulator+=frameLimit ? 1.0F/60.0F :
                std::min(0.25F,std::chrono::duration<float>(simulationNow-simulationClock).count());
            simulationClock=simulationNow;
            while (simulationAccumulator>=1.0F/60.0F) {
                if (!require(vfx.system().update(1.0F/60.0F))) return false;
                simulationAccumulator-=1.0F/60.0F;
            }
            if (probe) {
                std::array<Vertex,8> vertices{};
                for (unsigned i=0;i<8;++i) {
                    vertices[i]=quad[i%4];
                    vertices[i].position.x=vertices[i].position.x*0.12F+(i<4 ? -0.75F : -0.45F);
                    vertices[i].position.y=vertices[i].position.y*0.08F+0.9F;
                    vertices[i].color=(i<4 ? ((frames%2) ? Vec4{0,0,1,1} : Vec4{1,0,0,1})
                                            : Vec4{1,1,0,1});
                }
                constexpr std::array<std::uint32_t,12> indices{0,1,2,2,3,0,4,5,6,6,7,4};
                if (!require(backend.updateDynamicMesh(dynamicMesh,{vertices,indices}))) return false;
                if (frames==0 || frames+1==frameLimit) {
                    constexpr std::array<std::uint32_t,15> tooMany{0,1,2,2,3,0,4,5,6,6,7,4,0,1,2};
                    if (backend.updateDynamicMesh(dynamicMesh,{vertices,tooMany}).code != ErrorCode::CapacityExceeded) {
                        std::fprintf(stderr,"Dynamic mesh overflow accepted\n"); return false;
                    }
                }
            }
            const float time=frameLimit ? frames/60.0F :
                std::chrono::duration<float>(std::chrono::steady_clock::now()-start).count();
            ScheduledSceneFrame sceneFrame{};
            sceneFrame.backend=&backend;
            sceneFrame.scene=&scene;
            sceneFrame.hierarchy=&hierarchy;
            sceneFrame.entities={sceneEntities.data(),scene.entities.size()};
            sceneFrame.materials={sceneMaterials.data(),scene.materials.size()};
            sceneFrame.particles=vfx.system().particles(emitter);
            sceneFrame.frame=frame;
            sceneFrame.camera=camera;
            sceneFrame.spriteUv=spriteUv;
            sceneFrame.time=time;
            sceneFrame.fps=fps;
            sceneFrame.cubeMesh=cubeMesh;
            sceneFrame.quadMesh=quadMesh;
            sceneFrame.dynamicMesh=dynamicMesh;
            sceneFrame.particleMaterial=particleMat;
            sceneFrame.spriteMaterial=spriteMat;
            sceneFrame.uiMaterial=uiMat;
            sceneFrame.glassMaterial=glassMat;
            sceneFrame.atlasMaterial=atlasMat;
            sceneFrame.dynamicMaterial=dynamicMat;
            sceneFrame.quadVertices=quad;
            sceneFrame.quadIndices=quadIndices;
            sceneFrame.capturePath=capture && frames+1==frameLimit ? capture : nullptr;
            sceneFrame.probe=probe;
            FramePassScheduler scheduler{};
            RenderPassHandle beginPass{},opaquePass{},transparentPass{},particlesPass{},
                uiPass{},presentationPass{};
            if (!require(scheduler.add(RenderPassStage::BeginFrame,beginSceneFrame,
                                       &sceneFrame,beginPass)) ||
                !require(scheduler.add(RenderPassStage::Opaque,submitOpaquePass,
                                       &sceneFrame,opaquePass)) ||
                !require(scheduler.add(RenderPassStage::Transparent,submitTransparentPass,
                                       &sceneFrame,transparentPass)) ||
                !require(scheduler.add(RenderPassStage::Particles,submitParticlesPass,
                                       &sceneFrame,particlesPass)) ||
                !require(scheduler.add(RenderPassStage::Ui,submitUiPass,
                                       &sceneFrame,uiPass)) ||
                !require(scheduler.add(RenderPassStage::Presentation,presentSceneFrame,
                                       &sceneFrame,presentationPass))) return false;
            auto status=scheduler.execute();
            if (!status && status.code==ErrorCode::InvalidState &&
                status.detail=="swapchain out of date; retry frame") continue;
            if (!require(status)) return false;
            ++frames; ++periodFrames;
            auto now=std::chrono::steady_clock::now();
            double elapsed=std::chrono::duration<double>(now-last).count();
            if (elapsed>=1) {
                fps=static_cast<unsigned>(periodFrames/elapsed); periodFrames=0;last=now;
                auto stats=backend.diagnostics();
                std::printf("{\"stage\":\"frame.metrics\",\"fps\":%u,\"cpuMs\":%.3f,\"drawCalls\":%u,\"triangles\":%u,\"allocatedBytes\":%llu,\"cpuParticles\":%zu}\n",
                    fps,stats.cpuFrameMilliseconds,stats.drawCalls,stats.triangles,static_cast<unsigned long long>(stats.allocatedGpuBytes),
                    vfx.system().activeParticleCount(emitter));
            }
        }
        if (frameLimit && frames!=frameLimit) { std::fprintf(stderr,"Bounded run interrupted\n"); return false; }
        auto stats=backend.diagnostics();
        std::printf("{\"stage\":\"run.complete\",\"frames\":%llu,\"validationErrors\":%u,\"drawCalls\":%u,\"triangles\":%u}\n",
            static_cast<unsigned long long>(stats.frames),stats.validationErrors,stats.drawCalls,stats.triangles);
        if (stats.validationErrors) return false;
        modules.shutdownAll();
        for (std::size_t i=0;i<scene.materials.size();++i)
            if (!require(backend.destroyMaterial(sceneMaterials[i]))) return false;
        if (!require(backend.destroyMaterial(particleMat)) ||
            !require(backend.destroyMaterial(spriteMat)) || !require(backend.destroyMaterial(uiMat)) ||
            !require(backend.destroyMaterial(glassMat))) return false;
        if (probe && (!require(backend.destroyMaterial(targetSampleMaterial)) ||
                      !require(backend.destroyMaterial(targetNearMaterial)) ||
                      !require(backend.destroyMaterial(targetFarMaterial)) ||
                      !require(backend.destroyTarget(target)) ||
                      !require(backend.destroyTarget(sampleTarget)))) return false;
        if (probe && (!require(backend.destroyMaterial(atlasMat)) ||
                      !require(backend.destroyTexture(atlasTexture)) ||
                      !require(backend.destroyMaterial(dynamicMat)) ||
                      !require(backend.destroyMesh(dynamicMesh)))) return false;
        if (probe && backend.updateDynamicMesh(dynamicMesh,{quad,quadIndices}).code != ErrorCode::NotFound) {
            std::fprintf(stderr,"Stale dynamic mesh handle accepted\n"); return false;
        }
        if (!require(backend.destroyTexture(checkerTexture)) || !require(backend.destroyTexture(whiteTexture)) ||
            !require(backend.destroyTexture(spriteTexture)) || !require(backend.destroyMesh(cubeMesh)) ||
            !require(backend.destroyMesh(quadMesh))) return false;
        return true;
    };
    bool success=run(); backend.shutdown();
    if (backend.diagnostics().validationErrors) success=false;
    if (backend.diagnostics().allocatedGpuBytes != 0) { std::fprintf(stderr,"GPU allocation bookkeeping not empty after shutdown\n"); success=false; }
    if (!DestroyWindow(window)) { std::fprintf(stderr,"DestroyWindow failed\n"); success=false; }
    UnregisterClassW(cls.lpszClassName,cls.hInstance);
    return success ? 0 : 1;
}
