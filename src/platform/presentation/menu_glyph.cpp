#include "platform/presentation/menu_glyph.hpp"
#include <webp/decode.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <memory>
#include <new>
#include <string_view>

extern "C" {
unsigned char* echo_decode_png_rgba(const unsigned char* data, int length, int* width, int* height);
int echo_png_info(const unsigned char* data, int length, int* width, int* height);
void echo_free_png(unsigned char* pixels);
}

namespace echo::platform {
namespace {
using rendering::ErrorCode;
using rendering::Status;

inline constexpr std::size_t kMaxAssetBytes = 4 * 1024 * 1024;
inline constexpr std::uint64_t kMaxDecodedPngBytes = 64ULL * 1024ULL * 1024ULL;

struct RgbaImage {
    unsigned char* pixels{};
    int width{};
    int height{};
};

Status failure(ErrorCode code, std::string_view detail) noexcept {
    return {code, "menu glyph", detail};
}

bool decodePngRgba(std::span<const std::byte> bytes, RgbaImage& image) noexcept {
    echo_free_png(image.pixels);
    image = {};
    if (bytes.empty() || bytes.size() > kMaxAssetBytes) return false;
    int width{}, height{};
    if (!echo_png_info(reinterpret_cast<const unsigned char*>(bytes.data()),
                       static_cast<int>(bytes.size()), &width, &height) ||
        width <= 0 || height <= 0 ||
        static_cast<std::uint64_t>(width) * height * 4U > kMaxDecodedPngBytes)
        return false;
    image.pixels = echo_decode_png_rgba(
        reinterpret_cast<const unsigned char*>(bytes.data()),
        static_cast<int>(bytes.size()), &width, &height);
    if (!image.pixels || width <= 0 || height <= 0 ||
        static_cast<std::uint64_t>(width) * height * 4U > kMaxDecodedPngBytes) {
        echo_free_png(image.pixels);
        image = {};
        return false;
    }
    image.width = width;
    image.height = height;
    return true;
}

void freePngRgba(RgbaImage& image) noexcept {
    echo_free_png(image.pixels);
    image = {};
}

int digit(char value) noexcept {
    if (value >= 'A' && value <= 'Z') return value - 'A';
    if (value >= 'a' && value <= 'z') return value - 'a' + 26;
    if (value >= '0' && value <= '9') return value - '0' + 52;
    if (value == '+') return 62;
    if (value == '/') return 63;
    return -1;
}

struct Bytes {
    std::unique_ptr<std::byte[]> data{};
    std::size_t size{};
    [[nodiscard]] std::span<const std::byte> span() const noexcept { return {data.get(), size}; }
};

Status decode(std::string_view uri, std::string_view prefix, Bytes& output) noexcept {
    output = {};
    if (uri.size() < prefix.size() || !std::equal(prefix.begin(), prefix.end(), uri.begin()))
        return failure(ErrorCode::MissingCapability, "expected embedded base64 data URI");
    uri.remove_prefix(prefix.size());
    if (uri.empty() || uri.size() % 4 != 0 || uri.size() / 4 * 3 > kMaxAssetBytes + 2)
        return failure(ErrorCode::CapacityExceeded, "embedded data size or base64 length");
    const std::size_t capacity = uri.size() / 4 * 3;
    output.data.reset(new (std::nothrow) std::byte[capacity]);
    if (!output.data) return failure(ErrorCode::CapacityExceeded, "embedded data allocation");
    for (std::size_t i = 0; i < uri.size(); i += 4) {
        const int a = digit(uri[i]), b = digit(uri[i + 1]);
        const int c = uri[i + 2] == '=' ? -2 : digit(uri[i + 2]);
        const int d = uri[i + 3] == '=' ? -2 : digit(uri[i + 3]);
        const bool last = i + 4 == uri.size();
        if (a < 0 || b < 0 || c == -1 || d == -1 ||
            (c == -2 && d != -2) || ((c == -2 || d == -2) && !last) ||
            (c == -2 && (b & 15) != 0) || (d == -2 && c >= 0 && (c & 3) != 0))
            return failure(ErrorCode::CorruptData, "invalid base64 data");
        output.data[output.size++] = std::byte((a << 2) | (b >> 4));
        if (c >= 0) output.data[output.size++] = std::byte(((b & 15) << 4) | (c >> 2));
        if (d >= 0) output.data[output.size++] = std::byte(((c & 3) << 6) | d);
    }
    if (output.size > static_cast<std::size_t>(kMaxAssetBytes))
        return failure(ErrorCode::CapacityExceeded, "embedded data exceeds limit");
    return Status::success();
}

Status readAsset(const char* root, const char* name, Bytes& output) noexcept {
    std::array<char, 512> path{};
    const std::string_view rootPath{root};
    const bool separated = !rootPath.empty() && (rootPath.back() == '/' || rootPath.back() == '\\');
    const int written = std::snprintf(path.data(), path.size(), "%s%smenu/%s",
                                      root, separated ? "" : "/", name);
    if (written <= 0 || written >= static_cast<int>(path.size()))
        return failure(ErrorCode::InvalidValue, "asset path too long");
    errno = 0;
    FILE* file = std::fopen(path.data(), "rb");
    if (!file) {
        const int error = errno;
        if (error != ENOENT) std::fprintf(stderr, "Menu glyph unavailable: %s (errno %d)\n", path.data(), error);
        return failure(error == ENOENT ? ErrorCode::NotFound : ErrorCode::StorageFailure, "glyph asset unavailable");
    }
    if (std::fseek(file, 0, SEEK_END) != 0) {
        std::fclose(file);
        return failure(ErrorCode::StorageFailure, "glyph asset seek failed");
    }
    const long size = std::ftell(file);
    if (size <= 0 || size > static_cast<long>(framework::kMaxAssetJsonBytes)) {
        std::fclose(file);
        return failure(ErrorCode::CapacityExceeded, "glyph asset size");
    }
    if (std::fseek(file, 0, SEEK_SET) != 0) {
        std::fclose(file);
        return failure(ErrorCode::StorageFailure, "glyph asset seek failed");
    }
    output.data.reset(new (std::nothrow) std::byte[static_cast<std::size_t>(size)]);
    if (!output.data) {
        std::fclose(file);
        return failure(ErrorCode::CapacityExceeded, "glyph asset allocation");
    }
    output.size = static_cast<std::size_t>(size);
    const auto read = std::fread(output.data.get(), 1, output.size, file);
    const auto closed = std::fclose(file);
    if (read != output.size || closed != 0)
        return failure(ErrorCode::StorageFailure, "glyph asset read or close failed");
    return Status::success();
}

std::uint32_t little32(const std::byte* p) noexcept {
    return std::to_integer<std::uint32_t>(p[0]) | (std::to_integer<std::uint32_t>(p[1]) << 8) |
           (std::to_integer<std::uint32_t>(p[2]) << 16) | (std::to_integer<std::uint32_t>(p[3]) << 24);
}

bool unsignedField(framework::JsonValue object, std::string_view name, std::uint32_t& result,
                   bool optional = false) noexcept {
    const auto value = object.find(name);
    if (!value.valid() && optional) { result = 0; return true; }
    std::int64_t number{};
    if (!value.integer(number) || number < 0 || number > UINT32_MAX) return false;
    result = static_cast<std::uint32_t>(number);
    return true;
}
} // namespace

Status MenuGlyph::load(const char* root) noexcept {
    if (!root || ready_ || mesh_.valid() || textureCount_ || materialCount_)
        return failure(ErrorCode::InvalidState, "invalid asset root or glyph already loaded");
    Bytes file{};
    auto status = readAsset(root, "glyph.glb", file);
    bool glb = static_cast<bool>(status);
    if (status.code == ErrorCode::NotFound) status = readAsset(root, "glyph.gltf", file);
    if (!status) return status;
    std::span<const std::byte> json = file.span(), binarySpan{};
    if (glb) {
        if (file.size < 28 || little32(file.data.get()) != 0x46546c67 ||
            little32(file.data.get() + 4) != 2 || little32(file.data.get() + 8) != file.size)
            return failure(ErrorCode::CorruptData, "invalid GLB header");
        const auto jsonSize = little32(file.data.get() + 12);
        if (jsonSize % 4 || jsonSize > file.size - 28 || little32(file.data.get() + 16) != 0x4e4f534a)
            return failure(ErrorCode::CorruptData, "invalid GLB JSON chunk");
        const auto binaryOffset = std::size_t{20} + jsonSize;
        const auto binarySize = little32(file.data.get() + binaryOffset);
        if (binarySize % 4 || binarySize != file.size - binaryOffset - 8 ||
            little32(file.data.get() + binaryOffset + 4) != 0x004e4942)
            return failure(ErrorCode::CorruptData, "invalid GLB binary chunk");
        json = {file.data.get() + 20, jsonSize};
        binarySpan = {file.data.get() + binaryOffset + 8, binarySize};
    }
    framework::JsonDocument document{};
    framework::JsonError error{};
    status = framework::JsonDocument::parseAsset(
        reinterpret_cast<const char*>(json.data()), json.size(), document, error);
    if (!status) return status;
    const auto rootJson = document.root();
    const auto buffers = rootJson.find("buffers");
    Bytes binary{};
    if (!buffers.isArray() || buffers.size() != 1)
        return failure(ErrorCode::InvalidValue, "one embedded buffer is required");
    if (glb) {
        if (buffers.at(0).find("uri").valid()) return failure(ErrorCode::InvalidValue, "GLB buffer URI unsupported");
    } else {
        std::string_view uri{};
        if (!buffers.at(0).find("uri").string(uri))
            return failure(ErrorCode::InvalidValue, "embedded buffer URI required");
        constexpr std::string_view generic = "data:application/octet-stream;base64,";
        constexpr std::string_view gltf = "data:application/gltf-buffer;base64,";
        status = decode(uri, uri.size() >= gltf.size() && std::equal(gltf.begin(), gltf.end(), uri.begin())
                                 ? gltf : generic, binary);
        if (!status) return status;
        binarySpan = binary.span();
    }
    std::unique_ptr<rendering::GltfMesh> parsed{new (std::nothrow) rendering::GltfMesh{}};
    if (!parsed) return failure(ErrorCode::CapacityExceeded, "glTF mesh allocation");
    status = parsed->parseDocument(rootJson, binarySpan);
    if (!status) return status;

    const auto mesh = parsed->mesh();
    rendering::Vec3 low = mesh.vertices[0].position, high = low;
    for (const auto& vertex : mesh.vertices) {
        low.x = std::min(low.x, vertex.position.x); low.y = std::min(low.y, vertex.position.y);
        low.z = std::min(low.z, vertex.position.z);
        high.x = std::max(high.x, vertex.position.x); high.y = std::max(high.y, vertex.position.y);
        high.z = std::max(high.z, vertex.position.z);
    }
    const float extent = std::max({high.x - low.x, high.y - low.y, high.z - low.z});
    if (!std::isfinite(extent) || extent <= 0.00001F)
        return failure(ErrorCode::InvalidValue, "glyph bounds are degenerate");
    center_ = (low + high) * 0.5F;
    scale_ = 1.0F / extent;
    rendering::Mat4 view{}, projection{};
    status = rendering::lookAt({0, 0, 4}, {0, 0, 0}, {0, 1, 0}, view);
    if (status) status = rendering::perspective(0.9F, 640.0F / 360.0F, 0.1F, 20.0F, projection);
    if (!status) return status;
    frame_.cameraPosition = {0, 0, 4, 1};
    frame_.viewProjection = rendering::translation({0.415F, 0.278F, 0}) * projection * view;
    frame_.lightDirectionAmbient = {-0.4F, -0.5F, -1.0F, 0.30F};
    frame_.lightColorIntensity = {1, 1, 1, 1.5F};

    status = backend_.createMesh(mesh, mesh_);
    if (!status) return status;
    const auto fail = [&](Status errorStatus) {
        const auto cleanup = shutdown();
        if (!cleanup) return cleanup;
        return errorStatus;
    };
    constexpr std::array<std::byte, 4> white{std::byte{255}, std::byte{255},
                                             std::byte{255}, std::byte{255}};
    status = backend_.createTexture(1, 1, white, textures_[textureCount_]);
    if (!status) return fail(status);
    const auto whiteHandle = textures_[textureCount_++];
    struct ImageRef { std::uint32_t index{UINT32_MAX}; rendering::TextureColorSpace space{}; rendering::TextureHandle handle{}; };
    std::array<ImageRef, 48> imageRefs{};
    std::size_t imageCount{};
    const auto upload = [&](std::uint32_t index, rendering::TextureColorSpace space,
                            rendering::TextureHandle& handle) -> Status {
        if (index == UINT32_MAX) { handle = {}; return Status::success(); }
        for (std::size_t i = 0; i < imageCount; ++i) {
            if (imageRefs[i].index == index && imageRefs[i].space == space) {
                handle = imageRefs[i].handle;
                return Status::success();
            }
        }
        if (imageCount == imageRefs.size() || textureCount_ == textures_.size())
            return failure(ErrorCode::CapacityExceeded, "glyph texture capacity");
        const auto imageJson = rootJson.find("images").at(index);
        Bytes imageData{};
        std::span<const std::byte> imageBytes{};
        std::string_view imageUri{}, mime{};
        Status result{};
        if (imageJson.find("uri").string(imageUri)) {
            constexpr std::string_view webpPrefix = "data:image/webp;base64,";
            const bool webp = imageUri.size() >= webpPrefix.size() &&
                              std::equal(webpPrefix.begin(), webpPrefix.end(), imageUri.begin());
            mime = webp ? "image/webp" : "image/png";
            result = decode(imageUri, webp ? webpPrefix : "data:image/png;base64,", imageData);
            if (!result) return result;
            imageBytes = imageData.span();
        } else {
            std::uint32_t viewIndex{}, buffer{}, offset{}, length{}, bufferLength{};
            if (!imageJson.find("mimeType").string(mime) || (mime != "image/png" && mime != "image/webp") ||
                !unsignedField(imageJson, "bufferView", viewIndex))
                return failure(ErrorCode::MissingCapability, "embedded PNG or WebP image required");
            const auto view = rootJson.find("bufferViews").at(viewIndex);
            if (!view.isObject() || !unsignedField(view, "buffer", buffer) || buffer != 0 ||
                !unsignedField(view, "byteOffset", offset, true) ||
                !unsignedField(view, "byteLength", length) ||
                !unsignedField(buffers.at(0), "byteLength", bufferLength) ||
                bufferLength > binarySpan.size() || offset > bufferLength || length > bufferLength - offset)
                return failure(ErrorCode::CorruptData, "image buffer view outside buffer");
            imageBytes = binarySpan.subspan(offset, length);
        }
        RgbaImage image{};
        std::unique_ptr<unsigned char[]> webpPixels{};
        if (mime == "image/webp") {
            WebPBitstreamFeatures features{};
            if (imageBytes.empty() || imageBytes.size() > static_cast<std::size_t>(kMaxAssetBytes) ||
                WebPGetFeatures(reinterpret_cast<const std::uint8_t*>(imageBytes.data()), imageBytes.size(), &features) != VP8_STATUS_OK ||
                features.has_animation || features.width <= 0 || features.height <= 0 ||
                static_cast<std::uint64_t>(features.width) * static_cast<std::uint64_t>(features.height) * 4U > kMaxDecodedPngBytes)
                return failure(ErrorCode::CorruptData, "WebP features or dimensions rejected");
            const auto size = static_cast<std::size_t>(features.width) * static_cast<std::size_t>(features.height) * 4;
            webpPixels.reset(new (std::nothrow) unsigned char[size]);
            if (!webpPixels) return failure(ErrorCode::CapacityExceeded, "WebP image allocation");
            if (!WebPDecodeRGBAInto(reinterpret_cast<const std::uint8_t*>(imageBytes.data()), imageBytes.size(),
                                    webpPixels.get(), size, features.width * 4))
                return failure(ErrorCode::CorruptData, "embedded WebP decoding failed");
            image = {webpPixels.get(), features.width, features.height};
        } else if (!decodePngRgba(imageBytes, image)) {
            return failure(ErrorCode::CorruptData, "embedded PNG decoding failed");
        }
        result = backend_.createTexture(static_cast<std::uint32_t>(image.width),
            static_cast<std::uint32_t>(image.height),
            {reinterpret_cast<const std::byte*>(image.pixels),
             static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height) * 4},
            textures_[textureCount_], space);
        if (webpPixels) image = {};
        freePngRgba(image);
        if (!result) return result;
        handle = textures_[textureCount_++];
        imageRefs[imageCount++] = {index, space, handle};
        return Status::success();
    };
    rendering::Material fallback{};
    fallback.model = rendering::MaterialModel::SimplePbr;
    fallback.texture = whiteHandle;
    status = backend_.createMaterial(fallback, materials_[materialCount_]);
    if (!status) return fail(status);
    ++materialCount_;
    for (std::size_t i = 0; i < parsed->materials().size(); ++i) {
        auto material = parsed->materials()[i];
        const auto images = parsed->materialImages()[i];
        status = upload(images.baseColor, rendering::TextureColorSpace::Srgb, material.texture);
        if (!status) return fail(status);
        if (!material.texture.valid()) material.texture = whiteHandle;
        status = upload(images.normal, rendering::TextureColorSpace::Linear, material.normalTexture);
        if (!status) return fail(status);
        status = upload(images.metallicRoughness, rendering::TextureColorSpace::Linear, material.metallicRoughnessTexture);
        if (!status) return fail(status);
        status = backend_.createMaterial(material, materials_[materialCount_]);
        if (!status) return fail(status);
        ++materialCount_;
    }
    submeshCount_ = mesh.submeshes.size();
    for (std::size_t i = 0; i < submeshCount_; ++i) {
        submeshes_[i] = mesh.submeshes[i];
        const auto material = parsed->primitiveMaterials()[i];
        primitiveMaterials_[i] = material == UINT32_MAX ? 0 : material + 1;
    }
    ready_ = true;
    return Status::success();
}

Status MenuGlyph::submit(float seconds) noexcept {
    if (!ready_ || !std::isfinite(seconds))
        return failure(ErrorCode::InvalidState, "glyph is not ready or time is invalid");
    const auto model = rendering::rotation({0.25F, 0.6F + seconds * 0.4F, 0}) *
                       rendering::scaling({scale_, scale_, scale_}) *
                       rendering::translation(center_ * -1.0F);
    for (std::size_t i = 0; i < submeshCount_; ++i) {
        rendering::Draw draw{};
        draw.mesh = mesh_;
        draw.material = materials_[primitiveMaterials_[i]];
        draw.model = model;
        draw.firstIndex = submeshes_[i].firstIndex;
        draw.indexCount = submeshes_[i].indexCount;
        const auto status = backend_.submit(draw);
        if (!status) return status;
    }
    return Status::success();
}

Status MenuGlyph::shutdown() noexcept {
    ready_ = false;
    while (materialCount_) {
        const auto status = backend_.destroyMaterial(materials_[materialCount_ - 1]);
        if (!status) return status;
        materials_[--materialCount_] = {};
    }
    while (textureCount_) {
        const auto status = backend_.destroyTexture(textures_[textureCount_ - 1]);
        if (!status) return status;
        textures_[--textureCount_] = {};
    }
    if (mesh_.valid()) {
        const auto status = backend_.destroyMesh(mesh_);
        if (!status) return status;
        mesh_ = {};
    }
    submeshCount_ = 0;
    return Status::success();
}

} // namespace echo::platform
