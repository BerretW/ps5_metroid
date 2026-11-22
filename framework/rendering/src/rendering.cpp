#include "echo/framework/rendering.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

namespace echo::rendering {
namespace {

constexpr float kEpsilon = 1.0e-6F;
constexpr float kMaxVertexPosition = 1000000.0F;
constexpr float kMaxVertexAttribute = 100000.0F;
constexpr float kMaxEmission = 100000.0F;
constexpr std::size_t kMaxSubmeshes = 256;

[[nodiscard]] bool finite(Vec2 value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y);
}

[[nodiscard]] bool finite(Vec3 value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) &&
           std::isfinite(value.z);
}

[[nodiscard]] bool finite(Vec4 value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) &&
           std::isfinite(value.z) && std::isfinite(value.w);
}

[[nodiscard]] Vec3 normalized(Vec3 value) noexcept {
    const float lengthSquared = dot(value, value);
    if (!std::isfinite(lengthSquared) || lengthSquared <= kEpsilon * kEpsilon) {
        return {};
    }
    return value * (1.0F / std::sqrt(lengthSquared));
}

} // namespace

Mat4 Mat4::identity() noexcept {
    Mat4 result{};
    result.values[0] = 1.0F;
    result.values[5] = 1.0F;
    result.values[10] = 1.0F;
    result.values[15] = 1.0F;
    return result;
}

Vec3 operator+(Vec3 a, Vec3 b) noexcept {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

Vec3 operator-(Vec3 a, Vec3 b) noexcept {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

Vec3 operator*(Vec3 a, float b) noexcept {
    return {a.x * b, a.y * b, a.z * b};
}

float dot(Vec3 a, Vec3 b) noexcept {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

Vec3 cross(Vec3 a, Vec3 b) noexcept {
    return {a.y * b.z - a.z * b.y,
            a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x};
}

Mat4 operator*(const Mat4& a, const Mat4& b) noexcept {
    Mat4 result{};
    for (std::size_t column = 0; column < 4; ++column) {
        for (std::size_t row = 0; row < 4; ++row) {
            float value = 0.0F;
            for (std::size_t inner = 0; inner < 4; ++inner) {
                value += a.values[inner * 4 + row] *
                         b.values[column * 4 + inner];
            }
            result.values[column * 4 + row] = value;
        }
    }
    return result;
}

Vec4 operator*(const Mat4& a, Vec4 b) noexcept {
    return {
        a.values[0] * b.x + a.values[4] * b.y + a.values[8] * b.z + a.values[12] * b.w,
        a.values[1] * b.x + a.values[5] * b.y + a.values[9] * b.z + a.values[13] * b.w,
        a.values[2] * b.x + a.values[6] * b.y + a.values[10] * b.z + a.values[14] * b.w,
        a.values[3] * b.x + a.values[7] * b.y + a.values[11] * b.z + a.values[15] * b.w};
}

Mat4 translation(Vec3 position) noexcept {
    Mat4 result = Mat4::identity();
    result.values[12] = position.x;
    result.values[13] = position.y;
    result.values[14] = position.z;
    return result;
}

Mat4 rotation(Vec3 radians) noexcept {
    const float cx = std::cos(radians.x);
    const float sx = std::sin(radians.x);
    const float cy = std::cos(radians.y);
    const float sy = std::sin(radians.y);
    const float cz = std::cos(radians.z);
    const float sz = std::sin(radians.z);

    Mat4 result = Mat4::identity();
    // Column-major Rz * Ry * Rx (intrinsic XYZ Euler angles).
    result.values[0] = cz * cy;
    result.values[1] = sz * cy;
    result.values[2] = -sy;
    result.values[4] = cz * sy * sx - sz * cx;
    result.values[5] = sz * sy * sx + cz * cx;
    result.values[6] = cy * sx;
    result.values[8] = cz * sy * cx + sz * sx;
    result.values[9] = sz * sy * cx - cz * sx;
    result.values[10] = cy * cx;
    return result;
}

Mat4 scaling(Vec3 scale) noexcept {
    Mat4 result{};
    result.values[0] = scale.x;
    result.values[5] = scale.y;
    result.values[10] = scale.z;
    result.values[15] = 1.0F;
    return result;
}

bool finite(const Mat4& matrix) noexcept {
    return std::all_of(matrix.values.begin(), matrix.values.end(),
                       [](float value) { return std::isfinite(value); });
}

Status lookAt(Vec3 eye, Vec3 target, Vec3 up, Mat4& output) noexcept {
    output = Mat4::identity();
    if (!finite(eye) || !finite(target) || !finite(up)) {
        return {ErrorCode::InvalidValue, "lookAt",
                "Camera vectors must contain only finite values."};
    }
    const Vec3 forward = normalized(target - eye);
    const Vec3 side = normalized(cross(forward, up));
    if (dot(forward, forward) == 0.0F || dot(side, side) == 0.0F) {
        return {ErrorCode::InvalidValue, "lookAt",
                "Camera direction and up vector must define a valid basis."};
    }
    const Vec3 correctedUp = cross(side, forward);
    output.values[0] = side.x;
    output.values[4] = side.y;
    output.values[8] = side.z;
    output.values[1] = correctedUp.x;
    output.values[5] = correctedUp.y;
    output.values[9] = correctedUp.z;
    output.values[2] = -forward.x;
    output.values[6] = -forward.y;
    output.values[10] = -forward.z;
    output.values[12] = -dot(side, eye);
    output.values[13] = -dot(correctedUp, eye);
    output.values[14] = dot(forward, eye);
    return Status::success();
}

Status perspective(float fovRadians, float aspect, float nearPlane, float farPlane,
                   Mat4& output) noexcept {
    output = {};
    if (!std::isfinite(fovRadians) || !std::isfinite(aspect) ||
        !std::isfinite(nearPlane) || !std::isfinite(farPlane) ||
        fovRadians <= 0.0F || fovRadians >= 3.14159265358979323846F ||
        aspect <= 0.0F || nearPlane <= 0.0F || farPlane <= nearPlane) {
        return {ErrorCode::InvalidValue, "perspective",
                "Perspective requires finite FOV, positive aspect and near, and far > near."};
    }
    const float focal = 1.0F / std::tan(fovRadians * 0.5F);
    if (!std::isfinite(focal)) {
        return {ErrorCode::InvalidValue, "perspective", "Perspective FOV is not representable."};
    }
    output.values[0] = focal / aspect;
    output.values[5] = -focal;
    output.values[10] = farPlane / (nearPlane - farPlane);
    output.values[11] = -1.0F;
    output.values[14] = (farPlane * nearPlane) / (nearPlane - farPlane);
    if (!finite(output)) {
        output = {};
        return {ErrorCode::InvalidValue, "perspective",
                "Perspective parameters produce non-finite matrix values."};
    }
    return Status::success();
}

Status orthographic(float width, float height, float nearPlane, float farPlane,
                    Mat4& output) noexcept {
    output = {};
    if (!std::isfinite(width) || !std::isfinite(height) ||
        !std::isfinite(nearPlane) || !std::isfinite(farPlane) ||
        width <= 0.0F || height <= 0.0F || nearPlane < 0.0F ||
        farPlane <= nearPlane) {
        return {ErrorCode::InvalidValue, "orthographic",
                "Orthographic projection requires positive dimensions and far > near >= 0."};
    }
    output = Mat4::identity();
    output.values[0] = 2.0F / width;
    output.values[5] = -2.0F / height;
    output.values[10] = 1.0F / (nearPlane - farPlane);
    output.values[14] = nearPlane / (nearPlane - farPlane);
    if (!finite(output)) {
        output = {};
        return {ErrorCode::InvalidValue, "orthographic",
                "Orthographic parameters produce non-finite matrix values."};
    }
    return Status::success();
}

Status validateMesh(MeshView mesh) noexcept {
    if (mesh.vertices.empty() || mesh.indices.empty()) {
        return {ErrorCode::InvalidValue, "mesh", "Mesh requires vertices and indices."};
    }
    if (mesh.vertices.size() > kMaxMeshVertices || mesh.indices.size() > kMaxMeshIndices ||
        mesh.submeshes.size() > kMaxSubmeshes) {
        return {ErrorCode::CapacityExceeded, "mesh", "Mesh exceeds bounded CPU resource limits."};
    }
    for (const Vertex& vertex : mesh.vertices) {
        if (!finite(vertex.position) || !finite(vertex.normal) ||
            !finite(vertex.uv) || !finite(vertex.color) ||
            std::abs(vertex.position.x) > kMaxVertexPosition ||
            std::abs(vertex.position.y) > kMaxVertexPosition ||
            std::abs(vertex.position.z) > kMaxVertexPosition ||
            std::abs(vertex.normal.x) > kMaxVertexAttribute ||
            std::abs(vertex.normal.y) > kMaxVertexAttribute ||
            std::abs(vertex.normal.z) > kMaxVertexAttribute ||
            std::abs(vertex.uv.x) > kMaxVertexAttribute ||
            std::abs(vertex.uv.y) > kMaxVertexAttribute ||
            vertex.color.x < 0.0F || vertex.color.x > 1.0F ||
            vertex.color.y < 0.0F || vertex.color.y > 1.0F ||
            vertex.color.z < 0.0F || vertex.color.z > 1.0F ||
            vertex.color.w < 0.0F || vertex.color.w > 1.0F) {
            return {ErrorCode::InvalidValue, "mesh",
                    "Vertex attributes must be finite and within supported bounds."};
        }
    }
    if (mesh.indices.size() % 3 != 0) {
        return {ErrorCode::InvalidValue, "mesh",
                "Triangle index count must be divisible by three."};
    }
    for (const std::uint32_t index : mesh.indices) {
        if (index >= mesh.vertices.size()) {
            return {ErrorCode::InvalidValue, "mesh", "Mesh index references a missing vertex."};
        }
    }
    for (const Submesh& submesh : mesh.submeshes) {
        const std::uint64_t end = static_cast<std::uint64_t>(submesh.firstIndex) +
                                  submesh.indexCount;
        if (submesh.indexCount == 0 || submesh.indexCount % 3 != 0 ||
            end > mesh.indices.size()) {
            return {ErrorCode::InvalidValue, "submesh",
                    "Submesh ranges must be non-empty triangles within the index buffer."};
        }
    }
    return Status::success();
}

Status validateMaterial(const Material& material) noexcept {
    const auto model = static_cast<std::uint8_t>(material.model);
    const auto alpha = static_cast<std::uint8_t>(material.alpha);
    if (model > static_cast<std::uint8_t>(MaterialModel::SimplePbr) ||
        alpha > static_cast<std::uint8_t>(AlphaMode::Additive) ||
        !finite(material.baseColor) || !finite(material.emission) ||
        !std::isfinite(material.metallic) || !std::isfinite(material.roughness) ||
        material.baseColor.x < 0.0F || material.baseColor.x > 1.0F ||
        material.baseColor.y < 0.0F || material.baseColor.y > 1.0F ||
        material.baseColor.z < 0.0F || material.baseColor.z > 1.0F ||
        material.baseColor.w < 0.0F || material.baseColor.w > 1.0F ||
        material.emission.x < 0.0F || material.emission.y < 0.0F ||
        material.emission.z < 0.0F || material.emission.x > kMaxEmission ||
        material.emission.y > kMaxEmission || material.emission.z > kMaxEmission ||
        material.metallic < 0.0F ||
        material.metallic > 1.0F || material.roughness < 0.0F ||
        material.roughness > 1.0F) {
        return {ErrorCode::InvalidValue, "material",
                "Material enums and finite color/PBR values are outside supported bounds."};
    }
    if (material.model == MaterialModel::Textured && !material.texture.valid()) {
        return {ErrorCode::InvalidValue, "material.texture",
                "Textured materials require a valid texture handle."};
    }
    return Status::success();
}

Status exportCapabilities(const Capabilities& capabilities, char* output,
                          std::size_t capacity, std::size_t& written) noexcept {
    written = 0;
    if (output == nullptr || capacity == 0) {
        return {ErrorCode::InvalidArgument, "capabilities",
                "Capability output buffer is invalid."};
    }
    const int count = std::snprintf(
        output, capacity,
        "{\"sprites\":%s,\"meshes\":%s,\"depth\":%s,\"shaders\":%s,"
        "\"renderToTexture\":%s,\"instancing\":%s,\"dynamicLights\":%s,"
        "\"shadowMaps\":%s,\"postprocessing\":%s,\"compute\":%s,"
        "\"maxMeshes\":%u,\"maxTextures\":%u,\"maxMaterials\":%u,"
        "\"maxDraws\":%u,\"maxPointLights\":%u}",
        capabilities.sprites ? "true" : "false",
        capabilities.meshes ? "true" : "false",
        capabilities.depth ? "true" : "false",
        capabilities.shaders ? "true" : "false",
        capabilities.renderToTexture ? "true" : "false",
        capabilities.instancing ? "true" : "false",
        capabilities.dynamicLights ? "true" : "false",
        capabilities.shadowMaps ? "true" : "false",
        capabilities.postprocessing ? "true" : "false",
        capabilities.compute ? "true" : "false",
        capabilities.maxMeshes, capabilities.maxTextures,
        capabilities.maxMaterials, capabilities.maxDraws,
        capabilities.maxPointLights);
    if (count < 0) {
        output[0] = '\0';
        return {ErrorCode::InvalidState, "capabilities",
                "Capability JSON formatting failed."};
    }
    if (static_cast<std::size_t>(count) >= capacity) {
        output[capacity - 1] = '\0';
        return {ErrorCode::BufferTooSmall, "capabilities",
                "Capability output buffer is too small."};
    }
    written = static_cast<std::size_t>(count);
    return Status::success();
}

bool drawBefore(const Draw& a, AlphaMode alphaA, const Draw& b,
                AlphaMode alphaB) noexcept {
    if (a.overlay != b.overlay) {
        return !a.overlay;
    }
    if (a.overlay) {
        return false;
    }
    const bool transparentA = alphaA != AlphaMode::Opaque;
    const bool transparentB = alphaB != AlphaMode::Opaque;
    if (transparentA != transparentB) {
        return !transparentA;
    }
    const float distanceA = std::isfinite(a.cameraDistance) ? a.cameraDistance : 0.0F;
    const float distanceB = std::isfinite(b.cameraDistance) ? b.cameraDistance : 0.0F;
    if (transparentA && distanceA != distanceB) {
        return distanceA > distanceB;
    }
    return false;
}

} // namespace echo::rendering
