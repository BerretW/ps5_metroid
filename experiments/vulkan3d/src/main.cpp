#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "echo/framework/vulkan_backend.hpp"
#include "echo/framework/rendering_data.hpp"
#include "echo/framework/hierarchy.hpp"
#include "echo/framework/vfx.hpp"
#include "stb/stb_image.h"
#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <cstdlib>

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
        else { std::fprintf(stderr,"Unknown or incomplete argument: %s\n",argv[i]); return 2; }
    }
    if (capture && !frameLimit) { std::fprintf(stderr,"Capture requires a bounded --frames run\n"); return 2; }
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
        MeshHandle cubeMesh{},quadMesh{},spriteMesh{};
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
        auto sprite=quadGeometry(16.0F/spriteWidth,24.0F/spriteHeight);
        if (!require(backend.createMesh({sprite,quadIndices},spriteMesh))) return false;
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
        if (probe) {
            auto status=backend.destroyTexture(checkerTexture);
            if (status.code != ErrorCode::InvalidState) { std::fprintf(stderr,"Referenced texture was not rejected\n"); return false; }
            MeshHandle temporary{};
            if (!require(backend.createMesh({quad,quadIndices},temporary)) || !require(backend.destroyMesh(temporary))) return false;
            if (backend.destroyMesh(temporary).code != ErrorCode::NotFound) { std::fprintf(stderr,"Stale handle accepted\n"); return false; }
            TargetHandle target{};
            if (backend.createTarget(128,128,target).code != ErrorCode::MissingCapability || backend.capabilities().compute)
                { std::fprintf(stderr,"Unsupported capability reported\n"); return false; }
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
            auto simulationNow=std::chrono::steady_clock::now();
            simulationAccumulator+=frameLimit ? 1.0F/60.0F :
                std::min(0.25F,std::chrono::duration<float>(simulationNow-simulationClock).count());
            simulationClock=simulationNow;
            while (simulationAccumulator>=1.0F/60.0F) {
                if (!require(vfx.system().update(1.0F/60.0F))) return false;
                simulationAccumulator-=1.0F/60.0F;
            }
            auto status=backend.beginFrame(frame);
            if (!status && status.code==ErrorCode::InvalidState && status.detail=="swapchain out of date; retry frame") continue;
            if (!require(status)) return false;
            float time=frameLimit ? frames/60.0F : std::chrono::duration<float>(std::chrono::steady_clock::now()-start).count();
            for (std::size_t i=0;i<scene.entities.size();++i) {
                const auto& entity=scene.entities[i];
                auto t=entity.transform;
                t.rotation.y+=time*scene.entities[i].spinRadiansPerSecond;
                if (!require(hierarchy.setTransform(sceneEntities[i],{t.position,t.rotation,t.scale}))) return false;
            }
            for (std::size_t i=0;i<scene.entities.size();++i) {
                const auto& entity=scene.entities[i];
                if (!entity.visible) continue;
                MeshHandle mesh{};
                if (entity.meshId=="echo:mesh/reactor_core" || entity.meshId=="echo:mesh/cargo_pod") mesh=cubeMesh;
                else if (entity.meshId=="echo:mesh/floor") mesh=quadMesh;
                else { std::fprintf(stderr,"No mesh factory for scene resource\n"); return false; }
                MaterialHandle material{};
                for (std::size_t m=0;m<scene.materials.size();++m)
                    if (scene.materials[m].id==entity.materialId) material=sceneMaterials[m];
                Mat4 model{}; if (!require(hierarchy.worldMatrix(sceneEntities[i],model))) return false;
                Draw draw{mesh,material,model};
                Vec3 position{model.values[12],model.values[13],model.values[14]};
                draw.cameraDistance=dot(camera-position,camera-position);
                if (!require(backend.submit(draw))) return false;
            }
            for (auto particle:vfx.system().particles(emitter)) {
                Draw draw{quadMesh,particleMat,translation(particle.position)*rotation({0,0.6F,0})*
                    scaling({particle.scale,particle.scale,1})};
                draw.tint=particle.color; draw.cameraDistance=dot(camera-particle.position,camera-particle.position);
                if (!require(backend.submit(draw))) return false;
            }
            Draw transparent{quadMesh,glassMat,translation({-2,0,0})*scaling({1.5F,2,1})};
            transparent.cameraDistance=9;
            if (!require(backend.submit(transparent))) return false;
            Draw spriteDraw{spriteMesh,spriteMat,translation({0.8F,-0.74F,0})*scaling({0.13F,0.32F,1})};
            spriteDraw.overlay=true;
            if (!require(backend.submit(spriteDraw)) || !digits(backend,quadMesh,uiMat,std::min(fps,999U))) return false;
            if (capture && frames+1==frameLimit && !require(backend.captureNextFrame(capture))) return false;
            if (!require(backend.endFrame())) return false;
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
        if (!require(backend.destroyTexture(checkerTexture)) || !require(backend.destroyTexture(whiteTexture)) ||
            !require(backend.destroyTexture(spriteTexture)) || !require(backend.destroyMesh(cubeMesh)) ||
            !require(backend.destroyMesh(quadMesh)) || !require(backend.destroyMesh(spriteMesh))) return false;
        return true;
    };
    bool success=run(); backend.shutdown();
    if (backend.diagnostics().validationErrors) success=false;
    if (backend.diagnostics().allocatedGpuBytes != 0) { std::fprintf(stderr,"GPU allocation bookkeeping not empty after shutdown\n"); success=false; }
    if (!DestroyWindow(window)) { std::fprintf(stderr,"DestroyWindow failed\n"); success=false; }
    UnregisterClassW(cls.lpszClassName,cls.hInstance);
    return success ? 0 : 1;
}
