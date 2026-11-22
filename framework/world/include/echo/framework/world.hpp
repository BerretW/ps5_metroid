#pragma once

#include "echo/framework/capability_manifest.hpp"
#include "echo/framework/data.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace echo::framework {

// Object archetypes and rooms are loaded from one JSON document each and may
// inherit from a parent definition. All storage is fixed; the module should be
// heap-allocated once because a full library is a few hundred kilobytes.
inline constexpr std::size_t kMaxWorldObjects = 48;
inline constexpr std::size_t kMaxWorldRooms = 12;
inline constexpr std::size_t kMaxInheritanceDepth = 8;
inline constexpr std::size_t kMaxObjectAnimations = 12;
inline constexpr std::size_t kMaxObjectProperties = 16;
inline constexpr std::size_t kMaxPlacementProperties = 8;
inline constexpr std::size_t kMaxRoomTilesets = 16;
inline constexpr std::size_t kMaxRoomTileTypes = 64;
inline constexpr std::size_t kMaxRoomLayers = 4;
inline constexpr std::size_t kMaxRoomPlacements = 16;
inline constexpr int kMinRoomColumns = 20;
inline constexpr int kMaxRoomColumns = 64;
inline constexpr int kMaxRoomRows = 16;
inline constexpr std::size_t kMaxWorldJsonBytes = 32 * 1024;

enum class ObjectCategory : std::uint8_t {
    None, Player, Npc, Enemy, Projectile, Pickup, Door, Loot, Prop, Trigger
};
enum class Facing : std::uint8_t { Right, Left };
enum class ObjectPropertyType : std::uint8_t { Integer, Number, Boolean, String };
enum class TileCollision : std::uint8_t { None, Solid, Hazard };
// Side rooms are viewed in profile; isometric rooms are a top-down grid drawn as 2:1 diamonds.
enum class RoomProjection : std::uint8_t { Side, Isometric };

struct ObjectProperty {
    std::array<char, 32> key{};
    ObjectPropertyType type{ObjectPropertyType::Integer};
    std::int64_t integer{};
    double number{};
    bool boolean{};
    std::array<char, 64> text{};
};

template <std::size_t Capacity>
struct PropertyList {
    std::array<ObjectProperty, Capacity> items{};
    std::size_t count{};

    [[nodiscard]] const ObjectProperty* find(std::string_view key) const noexcept {
        for (std::size_t index = 0; index < count; ++index) {
            if (std::string_view(items[index].key.data()) == key) return &items[index];
        }
        return nullptr;
    }
    [[nodiscard]] std::span<const ObjectProperty> view() const noexcept {
        return {items.data(), count};
    }
};

struct SpriteSheetSpec {
    std::array<char, 96> sheet{};
    std::int32_t frameWidth{};
    std::int32_t frameHeight{};
    std::int32_t offsetX{};
    std::int32_t offsetY{};
};

// One animation reads `frames` consecutive cells from (`column`, `row`) of the sheet.
struct AnimationSpec {
    std::array<char, 24> name{};
    std::int32_t row{};
    std::int32_t column{};
    std::int32_t frames{};
    std::int32_t fps{};
    bool loop{true};
    std::uint8_t present{};
};

class ObjectDefinition {
public:
    [[nodiscard]] std::string_view id() const noexcept { return id_.data(); }
    [[nodiscard]] std::string_view parent() const noexcept { return parent_.data(); }
    [[nodiscard]] bool isAbstract() const noexcept { return abstract_; }
    [[nodiscard]] ObjectCategory category() const noexcept { return category_; }
    [[nodiscard]] const SpriteSheetSpec& sprite() const noexcept { return sprite_; }
    [[nodiscard]] float width() const noexcept { return width_; }
    [[nodiscard]] float height() const noexcept { return height_; }
    [[nodiscard]] float spawnX() const noexcept { return spawnX_; }
    [[nodiscard]] float spawnY() const noexcept { return spawnY_; }
    [[nodiscard]] Facing facing() const noexcept { return facing_; }
    [[nodiscard]] std::span<const AnimationSpec> animations() const noexcept {
        return {animations_.data(), animationCount_};
    }
    [[nodiscard]] const AnimationSpec* findAnimation(std::string_view name) const noexcept;
    // Returns the named animation, then `defaultAnimation`, then the first animation.
    [[nodiscard]] const AnimationSpec* animationOrDefault(std::string_view name) const noexcept;
    [[nodiscard]] std::string_view defaultAnimation() const noexcept {
        return defaultAnimation_.data();
    }
    [[nodiscard]] std::span<const ObjectProperty> properties() const noexcept {
        return properties_.view();
    }
    [[nodiscard]] const ObjectProperty* property(std::string_view key) const noexcept {
        return properties_.find(key);
    }

private:
    std::array<char, 96> id_{};
    std::array<char, 96> parent_{};
    std::array<char, 24> defaultAnimation_{};
    SpriteSheetSpec sprite_{};
    std::array<AnimationSpec, kMaxObjectAnimations> animations_{};
    std::size_t animationCount_{};
    PropertyList<kMaxObjectProperties> properties_{};
    float width_{};
    float height_{};
    float spawnX_{};
    float spawnY_{};
    std::uint32_t present_{};
    ObjectCategory category_{ObjectCategory::None};
    Facing facing_{Facing::Right};
    bool abstract_{};
    bool resolved_{};
    friend class WorldModule;
    friend struct WorldParser;
};

struct TilesetSpec {
    std::array<char, 32> id{};
    std::array<char, 96> sheet{};
    std::int32_t tileWidth{};
    std::int32_t tileHeight{};
    std::int32_t margin{};
    std::int32_t spacing{};
};

// A legend entry: one map symbol selects a tileset cell, its rotation and collision.
struct TileType {
    char symbol{};
    std::array<char, 32> tileset{};
    std::size_t tilesetIndex{};
    std::int32_t column{};
    std::int32_t row{};
    std::int32_t rotation{};
    bool flipX{};
    bool flipY{};
    TileCollision collision{TileCollision::None};
};

struct TileLayer {
    std::array<char, 24> name{};
    std::int32_t columns{};
    std::int32_t rows{};
    float scrollFactorX{1.0F};
    // Row-major symbols with a kMaxRoomColumns stride; '.' and ' ' are empty.
    std::array<char, kMaxRoomColumns * kMaxRoomRows> cells{};
};

struct RoomExit {
    bool present{};
    bool completesGame{};
    std::array<char, 96> room{};
    std::array<char, 96> requiresAbility{};
};

struct ObjectPlacement {
    std::array<char, 32> id{};
    std::array<char, 96> object{};
    float x{};
    float y{};
    Facing facing{Facing::Right};
    bool hasPosition{};
    bool hasFacing{};
    PropertyList<kMaxPlacementProperties> properties{};
};

class RoomDefinition {
public:
    [[nodiscard]] std::string_view id() const noexcept { return id_.data(); }
    [[nodiscard]] std::string_view parent() const noexcept { return parent_.data(); }
    [[nodiscard]] bool isAbstract() const noexcept { return abstract_; }
    [[nodiscard]] std::string_view name() const noexcept { return name_.data(); }
    [[nodiscard]] int columns() const noexcept { return columns_; }
    [[nodiscard]] int rows() const noexcept { return rows_; }
    [[nodiscard]] std::array<std::uint8_t, 3> background() const noexcept { return background_; }
    [[nodiscard]] RoomProjection projection() const noexcept { return projection_; }
    [[nodiscard]] std::span<const TilesetSpec> tilesets() const noexcept {
        return {tilesets_.data(), tilesetCount_};
    }
    [[nodiscard]] std::span<const TileType> tileTypes() const noexcept {
        return {tileTypes_.data(), tileTypeCount_};
    }
    [[nodiscard]] std::span<const TileLayer> layers() const noexcept {
        return {layers_.data(), layerCount_};
    }
    [[nodiscard]] const TileType* findTileType(char symbol) const noexcept;
    // Returns the TileType for a layer cell, or nullptr when empty or out of range.
    [[nodiscard]] const TileType* tile(std::size_t layer, int column, int row) const noexcept;
    // Combined collision of all layers; hazards take precedence over solids.
    [[nodiscard]] TileCollision collision(int column, int row) const noexcept;
    [[nodiscard]] const RoomExit& leftExit() const noexcept { return exits_[0]; }
    [[nodiscard]] const RoomExit& rightExit() const noexcept { return exits_[1]; }
    [[nodiscard]] std::span<const ObjectPlacement> placements() const noexcept {
        return {placements_.data(), placementCount_};
    }
    [[nodiscard]] const ObjectPlacement* findPlacement(std::string_view id) const noexcept;

private:
    std::array<char, 96> id_{};
    std::array<char, 96> parent_{};
    std::array<char, 32> name_{};
    std::array<TilesetSpec, kMaxRoomTilesets> tilesets_{};
    std::size_t tilesetCount_{};
    std::array<TileType, kMaxRoomTileTypes> tileTypes_{};
    std::size_t tileTypeCount_{};
    std::array<TileLayer, kMaxRoomLayers> layers_{};
    std::size_t layerCount_{};
    std::array<RoomExit, 2> exits_{};
    std::array<ObjectPlacement, kMaxRoomPlacements> placements_{};
    std::size_t placementCount_{};
    std::array<std::uint8_t, 3> background_{};
    RoomProjection projection_{RoomProjection::Side};
    int columns_{};
    int rows_{};
    std::uint32_t present_{};
    bool abstract_{};
    bool resolved_{};
    friend class WorldModule;
    friend struct WorldParser;
};

// Loads a world manifest, then one document per listed object and room.
// Call finalize() after all documents are added; it resolves inheritance and
// cross-references and is the only step that makes definitions visible.
class WorldModule final : public IModule {
public:
    [[nodiscard]] Status loadManifest(JsonValue document) noexcept;
    [[nodiscard]] Status addObject(std::string_view expectedId, JsonValue document) noexcept;
    [[nodiscard]] Status addRoom(std::string_view expectedId, JsonValue document) noexcept;
    [[nodiscard]] Status finalize() noexcept;
    void reset() noexcept;

    [[nodiscard]] const ModuleInfo& info() const noexcept override;
    bool initialize(ModuleContext&) noexcept override;
    void shutdown() noexcept override;
    [[nodiscard]] bool initialized() const noexcept { return initialized_; }
    [[nodiscard]] bool finalized() const noexcept { return finalized_; }

    [[nodiscard]] std::size_t manifestObjectCount() const noexcept { return manifestObjectCount_; }
    [[nodiscard]] std::size_t manifestRoomCount() const noexcept { return manifestRoomCount_; }
    [[nodiscard]] std::string_view manifestObject(std::size_t index) const noexcept;
    [[nodiscard]] std::string_view manifestRoom(std::size_t index) const noexcept;
    [[nodiscard]] std::string_view startRoom() const noexcept { return startRoom_.data(); }

    [[nodiscard]] const ObjectDefinition* findObject(std::string_view id) const noexcept;
    [[nodiscard]] const RoomDefinition* findRoom(std::string_view id) const noexcept;
    [[nodiscard]] std::span<const ObjectDefinition> objects() const noexcept {
        return {objects_.data(), finalized_ ? objectCount_ : 0};
    }
    [[nodiscard]] std::span<const RoomDefinition> rooms() const noexcept {
        return {rooms_.data(), finalized_ ? roomCount_ : 0};
    }

    // Maps a content ID such as "echo:object/enemy_patrol" to the packaged
    // relative path "world/objects/enemy_patrol.json". Only IDs with a safe
    // file name are accepted, so JSON never names arbitrary files.
    [[nodiscard]] static bool contentPath(std::string_view id, char* output,
                                          std::size_t capacity) noexcept;
    // Sprite sheets must be relative "sprites/....png" paths without traversal.
    [[nodiscard]] static bool validSpritePath(std::string_view path) noexcept;

private:
    [[nodiscard]] Status resolveObjects() noexcept;
    [[nodiscard]] Status resolveRooms() noexcept;
    [[nodiscard]] Status validateReferences() noexcept;
    [[nodiscard]] ObjectDefinition* findRawObject(std::string_view id) noexcept;
    [[nodiscard]] RoomDefinition* findRawRoom(std::string_view id) noexcept;

    std::array<std::array<char, 96>, kMaxWorldObjects> manifestObjects_{};
    std::array<std::array<char, 96>, kMaxWorldRooms> manifestRooms_{};
    std::size_t manifestObjectCount_{};
    std::size_t manifestRoomCount_{};
    std::array<char, 96> startRoom_{};
    std::array<ObjectDefinition, kMaxWorldObjects> objects_{};
    std::size_t objectCount_{};
    std::array<RoomDefinition, kMaxWorldRooms> rooms_{};
    std::size_t roomCount_{};
    bool manifestLoaded_{};
    bool finalized_{};
    bool initialized_{};
};

[[nodiscard]] std::string_view objectCategoryName(ObjectCategory category) noexcept;
[[nodiscard]] CapabilityManifestFragment worldCapabilityManifestFragment() noexcept;

} // namespace echo::framework
