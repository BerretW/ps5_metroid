#include "platform/sdl/canvas_assets.hpp"
#include "platform/sdl/assets.hpp"
#include "platform/sdl/sprite_sheets.hpp"

#include <algorithm>
#include <array>
#include <cstdio>

namespace echo::platform {
namespace {

bool sameMaterial(const framework::WorldMaterialSpec& left,
                  const framework::WorldMaterialSpec& right) noexcept {
    return std::string_view(left.normalSheet.data()) ==
               std::string_view(right.normalSheet.data()) &&
           std::string_view(left.emissiveSheet.data()) ==
               std::string_view(right.emissiveSheet.data()) &&
           std::string_view(left.emissiveCircuit.data()) ==
               std::string_view(right.emissiveCircuit.data()) &&
           left.emissiveColor == right.emissiveColor &&
           left.emissiveIntensity == right.emissiveIntensity;
}

bool loadMaterialMap(rendering::IRenderBackend& backend, const char* assetRoot,
                     std::string_view relativePath, CanvasSprite& texture,
                     const CanvasSprite& base) noexcept {
    if (relativePath.empty()) return true;
    std::array<char, 1200> fullPath{};
    const int written = std::snprintf(fullPath.data(), fullPath.size(), "%s%.*s", assetRoot,
                                      static_cast<int>(relativePath.size()), relativePath.data());
    if (written <= 0 || written >= static_cast<int>(fullPath.size()) ||
        !uploadCanvasTexture(backend, fullPath.data(), texture.texture,
                             texture.width, texture.height,
                             rendering::TextureColorSpace::Linear)) {
        return false;
    }
    if (texture.width != base.width || texture.height != base.height) {
        SDL_Log("Canvas material map dimensions must match its base sprite sheet: %s",
                fullPath.data());
        return false;
    }
    return true;
}

bool addWorldSprite(rendering::IRenderBackend& backend, const char* assetRoot,
                    std::string_view sheetPath,
                    const framework::WorldMaterialSpec& material,
                    CanvasWorldSprites& sprites) noexcept {
    if (sheetPath.empty() || !framework::WorldModule::validSpritePath(sheetPath)) {
        SDL_Log("Canvas world sprite path is invalid");
        return false;
    }
    for (std::size_t index = 0; index < sprites.count; ++index) {
        if (sheetPath == sprites.sheets[index].path.data() &&
            sameMaterial(material, sprites.sheets[index].material)) return true;
    }
    if (sprites.count == sprites.sheets.size() ||
        sheetPath.size() >= CanvasWorldSprite{}.path.size()) {
        SDL_Log("Canvas world sprite capacity or path length exceeded");
        return false;
    }
    std::array<char, 1200> fullPath{};
    const int written = std::snprintf(fullPath.data(), fullPath.size(), "%s%.*s", assetRoot,
                                      static_cast<int>(sheetPath.size()), sheetPath.data());
    if (written <= 0 || written >= static_cast<int>(fullPath.size())) {
        SDL_Log("Canvas world sprite asset path is too long");
        return false;
    }
    CanvasWorldSprite& entry = sprites.sheets[sprites.count];
    if (!uploadCanvasTexture(backend, fullPath.data(), entry.image.texture,
                             entry.image.width, entry.image.height)) return false;
    std::copy(sheetPath.begin(), sheetPath.end(), entry.path.begin());
    ++sprites.count;
    entry.material = material;
    if (!loadMaterialMap(backend, assetRoot, material.normalSheet.data(),
                         entry.normal, entry.image) ||
        !loadMaterialMap(backend, assetRoot, material.emissiveSheet.data(),
                         entry.emissive, entry.image)) {
        return false;
    }
    return true;
}

} // namespace

bool uploadCanvasTexture(rendering::IRenderBackend& backend, const char* path,
                         rendering::TextureHandle& texture,
                         std::uint32_t& width, std::uint32_t& height,
                         rendering::TextureColorSpace colorSpace) noexcept {
    texture = {};
    width = height = 0;
    RgbaImage image{};
    if (!loadPngRgba(path, image)) return false;
    if (static_cast<std::uint64_t>(image.width) * image.height * 4U > 16U * 1024U * 1024U) {
        SDL_Log("Canvas PNG exceeds the GPU upload limit: %s", path);
        freePngRgba(image);
        return false;
    }
    width = static_cast<std::uint32_t>(image.width);
    height = static_cast<std::uint32_t>(image.height);
    const auto byteCount = static_cast<std::size_t>(width) * height * 4U;
    const auto status = backend.createTexture(width, height,
        {reinterpret_cast<const std::byte*>(image.pixels), byteCount}, texture, colorSpace);
    freePngRgba(image);
    if (!status) {
        SDL_Log("Canvas texture upload failed [%u] %.*s: %.*s",
                static_cast<unsigned>(status.code), static_cast<int>(status.subject.size()),
                status.subject.data(), static_cast<int>(status.detail.size()), status.detail.data());
        width = height = 0;
        return false;
    }
    return true;
}

bool loadCanvasSprites(rendering::IRenderBackend& backend, const char* assetRoot,
                       CanvasSprites& sprites) noexcept {
    if (!assetRoot || !assetRoot[0]) {
        SDL_Log("Canvas sprite asset root is missing");
        return false;
    }
    for (std::size_t index = 0; index < sprites.size(); ++index) {
        std::array<char, 1200> path{};
        const int written = std::snprintf(path.data(), path.size(), "%s%s",
                                          assetRoot, kSpriteSheets[index].file);
        if (written <= 0 || written >= static_cast<int>(path.size())) {
            SDL_Log("Canvas sprite asset path is too long");
            return false;
        }
        CanvasSprite& sprite = sprites[index];
        if (!uploadCanvasTexture(backend, path.data(), sprite.texture, sprite.width, sprite.height))
            return false;
    }
    return true;
}

bool loadCanvasWorldSprites(rendering::IRenderBackend& backend, const char* assetRoot,
                            const WorldLayout& world, CanvasWorldSprites& sprites) noexcept {
    if (!assetRoot || !assetRoot[0]) {
        SDL_Log("Canvas world asset root is missing");
        return false;
    }
    const auto addObject = [&](const framework::ObjectDefinition* object) {
        return object == nullptr ||
               addWorldSprite(backend, assetRoot, object->sprite().sheet.data(),
                              object->sprite().material, sprites);
    };
    const WorldArchetypes& archetypes = world.archetypes;
    for (const framework::ObjectDefinition* object :
         {archetypes.player, archetypes.playerIso, archetypes.engineer, archetypes.pickup,
          archetypes.projectile, archetypes.enemyProjectile, archetypes.loot}) {
        if (!addObject(object)) return false;
    }
    for (const EnemySpawn& enemy : world.enemies) if (!addObject(enemy.archetype)) return false;
    for (const EnemySpawn& enemy : world.roamerList()) if (!addObject(enemy.archetype)) return false;
    for (const DoorLayout& door : world.doorList()) {
        if (door.visible && !addObject(door.archetype)) return false;
    }
    for (const PropLayout& prop : world.propList()) if (!addObject(prop.archetype)) return false;
    for (const RoomLayout& room : world.rooms) {
        if (room.definition == nullptr) continue;
        for (const framework::TilesetSpec& tileset : room.definition->tilesets()) {
            if (!addWorldSprite(backend, assetRoot, tileset.sheet.data(),
                                tileset.material, sprites)) return false;
        }
    }
    if (sprites.count == 0) {
        SDL_Log("Canvas world has no sprite sheets");
        return false;
    }
    return true;
}

} // namespace echo::platform
