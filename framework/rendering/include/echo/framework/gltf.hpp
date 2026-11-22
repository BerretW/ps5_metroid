#pragma once
#include "echo/framework/rendering.hpp"
#include "echo/framework/data.hpp"

namespace echo::rendering {

struct GltfMaterialImages {
    std::uint32_t baseColor{UINT32_MAX};
    std::uint32_t normal{UINT32_MAX};
    std::uint32_t metallicRoughness{UINT32_MAX};
};

// The caller resolves one glTF buffer; the importer never follows a URI or creates GPU resources.
class GltfMesh {
public:
    static constexpr std::size_t kMaxVertices = 2048, kMaxIndices = 6144;
    Status parse(std::string_view json, std::span<const std::byte> buffer,
                 framework::JsonError& error) noexcept;
    Status parseDocument(framework::JsonValue root, std::span<const std::byte> buffer) noexcept;
    MeshView mesh() const noexcept;
    std::span<const Material> materials() const noexcept { return {materials_.data(), loaded_ ? materialCount_ : 0}; }
    std::span<const GltfMaterialImages> materialImages() const noexcept { return {images_.data(), loaded_ ? materialCount_ : 0}; }
    std::span<const std::uint32_t> primitiveMaterials() const noexcept { return {primitiveMaterials_.data(), loaded_ ? submeshCount_ : 0}; }
private:
    std::array<Vertex, kMaxVertices> vertices_{};
    std::array<std::uint32_t, kMaxIndices> indices_{};
    std::array<Submesh, 32> submeshes_{};
    std::array<Material, 16> materials_{};
    std::array<GltfMaterialImages, 16> images_{};
    std::array<std::uint32_t, 32> primitiveMaterials_{};
    std::size_t vertexCount_{}, indexCount_{}, submeshCount_{}, materialCount_{};
    bool loaded_{};
};
}
