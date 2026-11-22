#pragma once

#include "echo/framework/core.hpp"
#include <array>
#include <span>

namespace echo::rendering {

using framework::Status;
using framework::ErrorCode;
struct Vec2 { float x{}, y{}; };
struct Vec3 { float x{}, y{}, z{}; };
struct Vec4 { float x{}, y{}, z{}, w{}; };
struct Mat4 {
    std::array<float, 16> values{};
    static Mat4 identity() noexcept;
};
Vec3 operator+(Vec3 a, Vec3 b) noexcept;
Vec3 operator-(Vec3 a, Vec3 b) noexcept;
Vec3 operator*(Vec3 a, float b) noexcept;
float dot(Vec3 a, Vec3 b) noexcept;
Vec3 cross(Vec3 a, Vec3 b) noexcept;
Mat4 operator*(const Mat4& a, const Mat4& b) noexcept;
Vec4 operator*(const Mat4& a, Vec4 b) noexcept;
Mat4 translation(Vec3 position) noexcept;
Mat4 rotation(Vec3 radians) noexcept;
Mat4 scaling(Vec3 scale) noexcept;
bool finite(const Mat4& matrix) noexcept;
Status lookAt(Vec3 eye, Vec3 target, Vec3 up, Mat4& output) noexcept;
Status perspective(float fovRadians, float aspect, float nearPlane, float farPlane,
                   Mat4& output) noexcept;
Status orthographic(float width, float height, float nearPlane, float farPlane,
                    Mat4& output) noexcept;

struct Vertex {
    Vec3 position{};
    Vec3 normal{0, 1, 0};
    Vec2 uv{};
    Vec4 color{1, 1, 1, 1};
};
inline constexpr std::size_t kMaxMeshVertices = 65536;
inline constexpr std::size_t kMaxMeshIndices = 196608;
struct Submesh { std::uint32_t firstIndex{}, indexCount{}; };
struct MeshView {
    std::span<const Vertex> vertices;
    std::span<const std::uint32_t> indices;
    std::span<const Submesh> submeshes{};
};
Status validateMesh(MeshView mesh) noexcept;

template<class Tag> struct Handle {
    std::uint32_t index{UINT32_MAX}, generation{};
    bool valid() const noexcept { return index != UINT32_MAX && generation != 0; }
    friend bool operator==(Handle, Handle) noexcept = default;
};
using MeshHandle = Handle<struct MeshTag>;
using TextureHandle = Handle<struct TextureTag>;
using MaterialHandle = Handle<struct MaterialTag>;
using TargetHandle = Handle<struct TargetTag>;

enum class MaterialModel : std::uint8_t { Unlit, VertexColor, Textured, SimplePbr };
enum class AlphaMode : std::uint8_t { Opaque, Blend, Additive };
struct Material {
    MaterialModel model{MaterialModel::Textured};
    AlphaMode alpha{AlphaMode::Opaque};
    Vec4 baseColor{1, 1, 1, 1};
    Vec3 emission{};
    float metallic{}, roughness{0.5F};
    TextureHandle texture{};
};
Status validateMaterial(const Material& material) noexcept;

struct Capabilities {
    bool sprites{}, meshes{}, depth{}, shaders{}, renderToTexture{}, instancing{};
    bool dynamicLights{}, shadowMaps{}, postprocessing{}, compute{};
    std::uint32_t maxMeshes{}, maxTextures{}, maxMaterials{}, maxDraws{}, maxPointLights{};
};
Status exportCapabilities(const Capabilities&, char*, std::size_t, std::size_t&) noexcept;

struct PointLight { Vec4 positionRadius{}; Vec4 colorIntensity{}; };
struct FrameState {
    Mat4 viewProjection{Mat4::identity()};
    Vec4 cameraPosition{0, 0, 0, 1};
    Vec4 lightDirectionAmbient{-0.4F, -1, -0.3F, 0.18F};
    Vec4 lightColorIntensity{1, 1, 1, 1};
    std::array<PointLight, 4> pointLights{};
    Vec4 clearColor{0.025F, 0.04F, 0.07F, 1};
};
struct Draw {
    MeshHandle mesh{};
    MaterialHandle material{};
    Mat4 model{Mat4::identity()};
    std::uint32_t firstIndex{}, indexCount{};
    float cameraDistance{};
    bool overlay{};
    Vec4 tint{1, 1, 1, 1};
    Vec4 uvTransform{0, 0, 1, 1};
};
struct SpriteRegion {
    std::uint32_t x{}, y{}, width{}, height{};
    bool flipX{}, flipY{};
};
inline Status spriteUvTransform(std::uint32_t textureWidth, std::uint32_t textureHeight,
                                SpriteRegion region, Vec4& output) noexcept {
    if (!textureWidth || !textureHeight || !region.width || !region.height ||
        region.x > textureWidth || region.width > textureWidth - region.x ||
        region.y > textureHeight || region.height > textureHeight - region.y) {
        return {ErrorCode::InvalidArgument, "sprite region", "Region must fit inside the texture."};
    }
    const float x = static_cast<float>(region.x) / static_cast<float>(textureWidth);
    const float y = static_cast<float>(region.y) / static_cast<float>(textureHeight);
    const float width = static_cast<float>(region.width) / static_cast<float>(textureWidth);
    const float height = static_cast<float>(region.height) / static_cast<float>(textureHeight);
    output = {region.flipX ? x + width : x, region.flipY ? y + height : y,
              region.flipX ? -width : width, region.flipY ? -height : height};
    return Status::success();
}
bool drawBefore(const Draw& a, AlphaMode alphaA, const Draw& b, AlphaMode alphaB) noexcept;
using RenderLogCallback = void (*)(void* context, std::string_view message) noexcept;
struct BackendInit {
    void* nativeWindow{};
    std::uint32_t width{1280}, height{720};
    const char* shaderDirectory{};
    bool validation{};
    std::uint32_t logicalWidth{}, logicalHeight{};
    RenderLogCallback log{};
    void* logContext{};
};
struct Diagnostics {
    std::uint64_t frames{}, allocatedGpuBytes{};
    std::uint32_t drawCalls{}, triangles{}, validationErrors{};
    double cpuFrameMilliseconds{};
};

class IRenderBackend {
public:
    virtual ~IRenderBackend() = default;
    virtual Status initialize(const BackendInit&) noexcept = 0;
    virtual Capabilities capabilities() const noexcept = 0;
    virtual Status createMesh(MeshView, MeshHandle&) noexcept = 0;
    virtual Status createDynamicMesh(std::size_t vertexCapacity, std::size_t indexCapacity,
                                     MeshHandle&) noexcept = 0;
    virtual Status updateDynamicMesh(MeshHandle, MeshView) noexcept = 0;
    virtual Status createTexture(std::uint32_t width, std::uint32_t height,
                                 std::span<const std::byte> rgba, TextureHandle&) noexcept = 0;
    virtual Status createMaterial(const Material&, MaterialHandle&) noexcept = 0;
    virtual Status destroyMesh(MeshHandle) noexcept = 0;
    virtual Status destroyTexture(TextureHandle) noexcept = 0;
    virtual Status destroyMaterial(MaterialHandle) noexcept = 0;
    virtual Status createTarget(std::uint32_t, std::uint32_t, TargetHandle&) noexcept = 0;
    virtual Status beginFrame(const FrameState&) noexcept = 0;
    virtual Status submit(const Draw&) noexcept = 0;
    virtual Status endFrame() noexcept = 0;
    virtual Status resize(std::uint32_t, std::uint32_t) noexcept = 0;
    virtual Diagnostics diagnostics() const noexcept = 0;
    virtual void shutdown() noexcept = 0;
};

} // namespace echo::rendering
