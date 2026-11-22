#include "echo/framework/world.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace echo::framework {
namespace {

constexpr std::array<std::string_view, 1> kServices{"echo.service.world"};
constexpr ModuleInfo kModuleInfo{"echo.world", "World objects and rooms", 1, {}, {}, kServices, {}};
constexpr std::array<ManifestModuleDescriptor, 1> kModules{{
    {"echo.world", "World objects and rooms", 1, {}, {}, kServices, {}},
}};
constexpr std::array<ManifestSchemaDescriptor, 3> kSchemas{{
    {"echo.world.manifest_json", 1},
    {"echo.world.object_json", 1},
    {"echo.world.room_json", 1},
}};

enum ObjectField : std::uint32_t {
    kCategory = 1U << 0U,
    kSheet = 1U << 1U,
    kFrameWidth = 1U << 2U,
    kFrameHeight = 1U << 3U,
    kOffsetX = 1U << 4U,
    kOffsetY = 1U << 5U,
    kWidth = 1U << 6U,
    kHeight = 1U << 7U,
    kSpawnX = 1U << 8U,
    kSpawnY = 1U << 9U,
    kFacing = 1U << 10U,
    kDefaultAnimation = 1U << 11U,
};

enum AnimationField : std::uint8_t {
    kRow = 1U << 0U,
    kColumn = 1U << 1U,
    kFrames = 1U << 2U,
    kFps = 1U << 3U,
    kLoop = 1U << 4U,
};

enum RoomField : std::uint32_t {
    kName = 1U << 0U,
    kSize = 1U << 1U,
    kBackground = 1U << 2U,
    kLayers = 1U << 3U,
    kExitLeft = 1U << 4U,
    kExitRight = 1U << 5U,
    kProjection = 1U << 6U,
};

constexpr std::array<std::string_view, 11> kObjectFields{
    "schemaVersion", "id", "parent", "abstract", "category", "sprite",
    "bounds", "spawn", "animations", "defaultAnimation", "properties"};
constexpr std::array<std::string_view, 4> kSpriteFields{"sheet", "frameWidth", "frameHeight", "offset"};
constexpr std::array<std::string_view, 2> kSpawnFields{"position", "facing"};
constexpr std::array<std::string_view, 5> kAnimationFields{"row", "column", "frames", "fps", "loop"};
constexpr std::array<std::string_view, 13> kRoomFields{
    "schemaVersion", "id", "parent", "abstract", "name", "size", "background",
    "projection", "tilesets", "tiles", "layers", "exits", "objects"};
constexpr std::array<std::string_view, 2> kSizeFields{"columns", "rows"};
constexpr std::array<std::string_view, 6> kTilesetFields{
    "id", "sheet", "tileWidth", "tileHeight", "margin", "spacing"};
constexpr std::array<std::string_view, 7> kTileFields{
    "tileset", "x", "y", "rotation", "flipX", "flipY", "collision"};
constexpr std::array<std::string_view, 3> kLayerFields{"name", "rows", "scrollFactorX"};
constexpr std::array<std::string_view, 2> kExitSides{"left", "right"};
constexpr std::array<std::string_view, 3> kExitFields{"room", "requiresAbility", "completesGame"};
constexpr std::array<std::string_view, 5> kPlacementFields{
    "id", "object", "position", "facing", "properties"};

constexpr std::array<std::string_view, 10> kCategoryNames{
    "none", "player", "npc", "enemy", "projectile", "pickup", "door", "loot", "prop", "trigger"};

[[nodiscard]] Status invalid(std::string_view subject, std::string_view detail) noexcept {
    return {ErrorCode::InvalidValue, subject, detail};
}

// Accepts only listed members and rejects duplicate keys.
[[nodiscard]] bool allowedFields(JsonValue object,
                                 std::span<const std::string_view> allowed) noexcept {
    if (!object.isObject()) return false;
    for (std::size_t index = 0; index < object.size(); ++index) {
        std::string_view name;
        JsonValue value{};
        if (!object.memberAt(index, name, value) ||
            std::find(allowed.begin(), allowed.end(), name) == allowed.end()) return false;
        for (std::size_t previous = 0; previous < index; ++previous) {
            std::string_view earlier;
            JsonValue ignored{};
            if (!object.memberAt(previous, earlier, ignored) || earlier == name) return false;
        }
    }
    return true;
}

[[nodiscard]] bool uniqueKeys(JsonValue object) noexcept {
    if (!object.isObject()) return false;
    for (std::size_t index = 0; index < object.size(); ++index) {
        std::string_view name;
        JsonValue value{};
        if (!object.memberAt(index, name, value)) return false;
        for (std::size_t previous = 0; previous < index; ++previous) {
            std::string_view earlier;
            JsonValue ignored{};
            if (!object.memberAt(previous, earlier, ignored) || earlier == name) return false;
        }
    }
    return true;
}

template <std::size_t Size>
[[nodiscard]] bool copyText(std::string_view input, std::array<char, Size>& output) noexcept {
    if (input.empty() || input.size() >= Size) return false;
    for (const char character : input) {
        const auto code = static_cast<unsigned char>(character);
        if (code < 0x20U || code == 0x7FU) return false;
    }
    output.fill('\0');
    std::memcpy(output.data(), input.data(), input.size());
    return true;
}

template <std::size_t Size>
[[nodiscard]] std::string_view text(const std::array<char, Size>& value) noexcept {
    return value.data();
}

[[nodiscard]] bool readInt(JsonValue value, std::int64_t minimum, std::int64_t maximum,
                           std::int32_t& output) noexcept {
    std::int64_t parsed{};
    if (!value.integer(parsed) || parsed < minimum || parsed > maximum) return false;
    output = static_cast<std::int32_t>(parsed);
    return true;
}

[[nodiscard]] bool readFloat(JsonValue value, double minimum, double maximum,
                             float& output) noexcept {
    double parsed{};
    if (!value.number(parsed) || !std::isfinite(parsed) || parsed < minimum || parsed > maximum) {
        return false;
    }
    output = static_cast<float>(parsed);
    return true;
}

[[nodiscard]] bool readPair(JsonValue value, double minimum, double maximum,
                            float& first, float& second) noexcept {
    return value.isArray() && value.size() == 2 &&
           readFloat(value.at(0), minimum, maximum, first) &&
           readFloat(value.at(1), minimum, maximum, second);
}

[[nodiscard]] bool readFacing(JsonValue value, Facing& output) noexcept {
    std::string_view name;
    if (!value.string(name)) return false;
    if (name == "right") {
        output = Facing::Right;
    } else if (name == "left") {
        output = Facing::Left;
    } else {
        return false;
    }
    return true;
}

[[nodiscard]] bool validKey(std::string_view key, std::size_t capacity) noexcept {
    if (key.empty() || key.size() >= capacity) return false;
    return std::all_of(key.begin(), key.end(), [](char character) {
        return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
               (character >= '0' && character <= '9') || character == '_' || character == '.' ||
               character == '-';
    });
}

[[nodiscard]] bool validFileName(std::string_view name) noexcept {
    if (name.empty() || name.size() > 48) return false;
    return std::all_of(name.begin(), name.end(), [](char character) {
        return (character >= 'a' && character <= 'z') || (character >= '0' && character <= '9') ||
               character == '_' || character == '-';
    });
}

[[nodiscard]] bool hasType(std::string_view id, std::string_view type) noexcept {
    const std::size_t colon = id.find(':');
    const std::size_t slash = id.find('/');
    return colon != std::string_view::npos && slash != std::string_view::npos && slash > colon &&
           std::string_view(id.data() + colon + 1, slash - colon - 1) == type;
}

template <std::size_t Capacity>
[[nodiscard]] Status parseProperties(JsonValue document, PropertyList<Capacity>& output) noexcept {
    if (!uniqueKeys(document) || document.size() > Capacity) {
        return invalid("properties", "Properties must be a bounded object with unique keys.");
    }
    output = {};
    for (std::size_t index = 0; index < document.size(); ++index) {
        std::string_view key;
        JsonValue value{};
        (void)document.memberAt(index, key, value);
        ObjectProperty& property = output.items[index];
        std::string_view textValue;
        if (!validKey(key, property.key.size()) || !copyText(key, property.key)) {
            return invalid("properties", "Property names use letters, digits, '_', '-' or '.'.");
        }
        if (value.isInteger()) {
            property.type = ObjectPropertyType::Integer;
            (void)value.integer(property.integer);
            property.number = static_cast<double>(property.integer);
        } else if (value.isNumber()) {
            property.type = ObjectPropertyType::Number;
            if (!value.number(property.number) || !std::isfinite(property.number)) {
                return invalid("properties", "Property numbers must be finite.");
            }
        } else if (value.isBoolean()) {
            property.type = ObjectPropertyType::Boolean;
            (void)value.boolean(property.boolean);
        } else if (value.string(textValue) && copyText(textValue, property.text)) {
            property.type = ObjectPropertyType::String;
        } else {
            return invalid("properties", "Property values must be numbers, booleans or short strings.");
        }
        ++output.count;
    }
    return Status::success();
}

template <std::size_t Capacity, std::size_t Other>
[[nodiscard]] bool mergeProperties(PropertyList<Capacity>& base,
                                   const PropertyList<Other>& overrides) noexcept {
    for (const ObjectProperty& property : overrides.view()) {
        bool replaced = false;
        for (std::size_t index = 0; index < base.count; ++index) {
            if (text(base.items[index].key) == text(property.key)) {
                base.items[index] = property;
                replaced = true;
                break;
            }
        }
        if (!replaced) {
            if (base.count == Capacity) return false;
            base.items[base.count++] = property;
        }
    }
    return true;
}

} // namespace

struct WorldParser {
    [[nodiscard]] static Status parseObject(JsonValue document, ObjectDefinition& output) noexcept;
    [[nodiscard]] static Status parseRoom(JsonValue document, RoomDefinition& output) noexcept;
    [[nodiscard]] static Status mergeObject(const ObjectDefinition& parent,
                                            ObjectDefinition& child) noexcept;
    [[nodiscard]] static Status mergeRoom(const RoomDefinition& parent,
                                          RoomDefinition& child) noexcept;
    [[nodiscard]] static Status validateObject(const ObjectDefinition& object) noexcept;
    [[nodiscard]] static Status validateRoom(RoomDefinition& room) noexcept;
};

Status WorldParser::parseObject(JsonValue document, ObjectDefinition& output) noexcept {
    output = {};
    std::int64_t version{};
    std::string_view id;
    if (!allowedFields(document, kObjectFields) ||
        !document.find("schemaVersion").integer(version) || version != 1) {
        return invalid("world.object", "Objects require schema version 1 and known fields only.");
    }
    if (!document.find("id").string(id) || !isValidContentId(id) || !hasType(id, "object") ||
        !copyText(id, output.id_)) {
        return invalid("world.object.id", "Objects require a stable namespace:object/name ID.");
    }
    const JsonValue parent = document.find("parent");
    if (parent.valid()) {
        std::string_view parentId;
        if (!parent.string(parentId) || !isValidContentId(parentId) ||
            !hasType(parentId, "object") || parentId == id || !copyText(parentId, output.parent_)) {
            return invalid("world.object.parent", "An object parent must be another object ID.");
        }
    }
    const JsonValue abstractValue = document.find("abstract");
    if (abstractValue.valid() && !abstractValue.boolean(output.abstract_)) {
        return invalid("world.object.abstract", "The abstract flag must be a boolean.");
    }
    const JsonValue category = document.find("category");
    if (category.valid()) {
        std::string_view name;
        const auto found = category.string(name)
            ? std::find(kCategoryNames.begin() + 1, kCategoryNames.end(), name)
            : kCategoryNames.end();
        if (found == kCategoryNames.end()) {
            return invalid("world.object.category", "The object category is not supported.");
        }
        output.category_ = static_cast<ObjectCategory>(found - kCategoryNames.begin());
        output.present_ |= kCategory;
    }
    const JsonValue sprite = document.find("sprite");
    if (sprite.valid()) {
        if (!allowedFields(sprite, kSpriteFields)) {
            return invalid("world.object.sprite", "The sprite block contains unknown fields.");
        }
        std::string_view sheet;
        const JsonValue sheetValue = sprite.find("sheet");
        if (sheetValue.valid()) {
            if (!sheetValue.string(sheet) || !WorldModule::validSpritePath(sheet) ||
                !copyText(sheet, output.sprite_.sheet)) {
                return invalid("world.object.sprite.sheet",
                               "Sprite sheets must be relative sprites/*.png paths.");
            }
            output.present_ |= kSheet;
        }
        const JsonValue frameWidth = sprite.find("frameWidth");
        if (frameWidth.valid()) {
            if (!readInt(frameWidth, 1, 512, output.sprite_.frameWidth)) {
                return invalid("world.object.sprite.frameWidth", "Frame width must be 1-512 pixels.");
            }
            output.present_ |= kFrameWidth;
        }
        const JsonValue frameHeight = sprite.find("frameHeight");
        if (frameHeight.valid()) {
            if (!readInt(frameHeight, 1, 512, output.sprite_.frameHeight)) {
                return invalid("world.object.sprite.frameHeight", "Frame height must be 1-512 pixels.");
            }
            output.present_ |= kFrameHeight;
        }
        const JsonValue offset = sprite.find("offset");
        if (offset.valid()) {
            if (!offset.isArray() || offset.size() != 2 ||
                !readInt(offset.at(0), -256, 256, output.sprite_.offsetX) ||
                !readInt(offset.at(1), -256, 256, output.sprite_.offsetY)) {
                return invalid("world.object.sprite.offset", "Sprite offset must be two integers within 256 pixels.");
            }
            output.present_ |= kOffsetX | kOffsetY;
        }
    }
    const JsonValue bounds = document.find("bounds");
    if (bounds.valid()) {
        if (!readPair(bounds, 1.0, 1024.0, output.width_, output.height_)) {
            return invalid("world.object.bounds", "Bounds must be [width, height] between 1 and 1024.");
        }
        output.present_ |= kWidth | kHeight;
    }
    const JsonValue spawn = document.find("spawn");
    if (spawn.valid()) {
        if (!allowedFields(spawn, kSpawnFields)) {
            return invalid("world.object.spawn", "The spawn block contains unknown fields.");
        }
        const JsonValue position = spawn.find("position");
        if (position.valid()) {
            if (!readPair(position, 0.0, 4096.0, output.spawnX_, output.spawnY_)) {
                return invalid("world.object.spawn.position", "Spawn position must be [x, y] within 0-4096.");
            }
            output.present_ |= kSpawnX | kSpawnY;
        }
        const JsonValue facing = spawn.find("facing");
        if (facing.valid()) {
            if (!readFacing(facing, output.facing_)) {
                return invalid("world.object.spawn.facing", "Facing must be left or right.");
            }
            output.present_ |= kFacing;
        }
    }
    const JsonValue animations = document.find("animations");
    if (animations.valid()) {
        if (!uniqueKeys(animations) || animations.size() > kMaxObjectAnimations) {
            return invalid("world.object.animations", "Animations must be a bounded object with unique names.");
        }
        for (std::size_t index = 0; index < animations.size(); ++index) {
            std::string_view name;
            JsonValue entry{};
            (void)animations.memberAt(index, name, entry);
            AnimationSpec& animation = output.animations_[index];
            if (!validKey(name, animation.name.size()) || !copyText(name, animation.name) ||
                !allowedFields(entry, kAnimationFields)) {
                return invalid("world.object.animation", "Animation names or fields are invalid.");
            }
            const JsonValue row = entry.find("row");
            const JsonValue column = entry.find("column");
            const JsonValue frames = entry.find("frames");
            const JsonValue fps = entry.find("fps");
            const JsonValue loop = entry.find("loop");
            if ((row.valid() && !readInt(row, 0, 255, animation.row)) ||
                (column.valid() && !readInt(column, 0, 255, animation.column)) ||
                (frames.valid() && !readInt(frames, 1, 64, animation.frames)) ||
                (fps.valid() && !readInt(fps, 1, 60, animation.fps)) ||
                (loop.valid() && !loop.boolean(animation.loop))) {
                return invalid("world.object.animation",
                               "Animation row/column 0-255, frames 1-64, fps 1-60 and boolean loop are required.");
            }
            animation.present = static_cast<std::uint8_t>(
                (row.valid() ? kRow : 0U) | (column.valid() ? kColumn : 0U) |
                (frames.valid() ? kFrames : 0U) | (fps.valid() ? kFps : 0U) |
                (loop.valid() ? kLoop : 0U));
            ++output.animationCount_;
        }
    }
    const JsonValue defaultAnimation = document.find("defaultAnimation");
    if (defaultAnimation.valid()) {
        std::string_view name;
        if (!defaultAnimation.string(name) || !validKey(name, output.defaultAnimation_.size()) ||
            !copyText(name, output.defaultAnimation_)) {
            return invalid("world.object.defaultAnimation", "The default animation name is invalid.");
        }
        output.present_ |= kDefaultAnimation;
    }
    const JsonValue properties = document.find("properties");
    if (properties.valid()) {
        const Status status = parseProperties(properties, output.properties_);
        if (!status) return {status.code, "world.object.properties", status.detail};
    }
    return Status::success();
}

Status WorldParser::mergeObject(const ObjectDefinition& parent, ObjectDefinition& child) noexcept {
    ObjectDefinition merged = parent;
    merged.id_ = child.id_;
    merged.parent_ = child.parent_;
    merged.abstract_ = child.abstract_;
    const std::uint32_t own = child.present_;
    if ((own & kCategory) != 0U) merged.category_ = child.category_;
    if ((own & kSheet) != 0U) merged.sprite_.sheet = child.sprite_.sheet;
    if ((own & kFrameWidth) != 0U) merged.sprite_.frameWidth = child.sprite_.frameWidth;
    if ((own & kFrameHeight) != 0U) merged.sprite_.frameHeight = child.sprite_.frameHeight;
    if ((own & kOffsetX) != 0U) merged.sprite_.offsetX = child.sprite_.offsetX;
    if ((own & kOffsetY) != 0U) merged.sprite_.offsetY = child.sprite_.offsetY;
    if ((own & kWidth) != 0U) merged.width_ = child.width_;
    if ((own & kHeight) != 0U) merged.height_ = child.height_;
    if ((own & kSpawnX) != 0U) merged.spawnX_ = child.spawnX_;
    if ((own & kSpawnY) != 0U) merged.spawnY_ = child.spawnY_;
    if ((own & kFacing) != 0U) merged.facing_ = child.facing_;
    if ((own & kDefaultAnimation) != 0U) merged.defaultAnimation_ = child.defaultAnimation_;
    merged.present_ = parent.present_ | own;
    for (const AnimationSpec& animation : child.animations()) {
        AnimationSpec* target = nullptr;
        for (std::size_t index = 0; index < merged.animationCount_; ++index) {
            if (text(merged.animations_[index].name) == text(animation.name)) {
                target = &merged.animations_[index];
                break;
            }
        }
        if (target == nullptr) {
            if (merged.animationCount_ == kMaxObjectAnimations) {
                return {ErrorCode::CapacityExceeded, "world.object.animations",
                        "Inherited animations exceed the fixed animation capacity."};
            }
            merged.animations_[merged.animationCount_++] = animation;
            continue;
        }
        if ((animation.present & kRow) != 0U) target->row = animation.row;
        if ((animation.present & kColumn) != 0U) target->column = animation.column;
        if ((animation.present & kFrames) != 0U) target->frames = animation.frames;
        if ((animation.present & kFps) != 0U) target->fps = animation.fps;
        if ((animation.present & kLoop) != 0U) target->loop = animation.loop;
        target->present = static_cast<std::uint8_t>(target->present | animation.present);
    }
    if (!mergeProperties(merged.properties_, child.properties_)) {
        return {ErrorCode::CapacityExceeded, "world.object.properties",
                "Inherited properties exceed the fixed property capacity."};
    }
    child = merged;
    return Status::success();
}

Status WorldParser::validateObject(const ObjectDefinition& object) noexcept {
    if (object.isAbstract()) return Status::success();
    constexpr std::uint32_t kRequired = kCategory | kSheet | kFrameWidth | kFrameHeight |
                                        kWidth | kHeight;
    if ((object.present_ & kRequired) != kRequired) {
        return {ErrorCode::MissingDependency, object.id(),
                "A concrete object needs category, sprite sheet, frame size and bounds (own or inherited)."};
    }
    if (object.animationCount_ == 0) {
        return {ErrorCode::MissingDependency, object.id(), "A concrete object needs at least one animation."};
    }
    for (const AnimationSpec& animation : object.animations()) {
        if ((animation.present & (kFrames | kFps)) != (kFrames | kFps)) {
            return {ErrorCode::MissingDependency, object.id(),
                    "Every resolved animation needs frames and fps."};
        }
    }
    if (!object.defaultAnimation().empty() && object.findAnimation(object.defaultAnimation()) == nullptr) {
        return {ErrorCode::MissingDependency, object.id(),
                "The default animation is not defined by the object or its parents."};
    }
    return Status::success();
}

Status WorldParser::parseRoom(JsonValue document, RoomDefinition& output) noexcept {
    output = {};
    std::int64_t version{};
    std::string_view id;
    if (!allowedFields(document, kRoomFields) ||
        !document.find("schemaVersion").integer(version) || version != 1) {
        return invalid("world.room", "Rooms require schema version 1 and known fields only.");
    }
    if (!document.find("id").string(id) || !isValidContentId(id) || !hasType(id, "room") ||
        !copyText(id, output.id_)) {
        return invalid("world.room.id", "Rooms require a stable namespace:room/name ID.");
    }
    const JsonValue parent = document.find("parent");
    if (parent.valid()) {
        std::string_view parentId;
        if (!parent.string(parentId) || !isValidContentId(parentId) || !hasType(parentId, "room") ||
            parentId == id || !copyText(parentId, output.parent_)) {
            return invalid("world.room.parent", "A room parent must be another room ID.");
        }
    }
    const JsonValue abstractValue = document.find("abstract");
    if (abstractValue.valid() && !abstractValue.boolean(output.abstract_)) {
        return invalid("world.room.abstract", "The abstract flag must be a boolean.");
    }
    const JsonValue name = document.find("name");
    if (name.valid()) {
        std::string_view value;
        if (!name.string(value) || !copyText(value, output.name_)) {
            return invalid("world.room.name", "Room names must be 1-31 printable characters.");
        }
        output.present_ |= kName;
    }
    const JsonValue size = document.find("size");
    if (size.valid()) {
        std::int32_t columns{};
        std::int32_t rows{};
        if (!allowedFields(size, kSizeFields) ||
            !readInt(size.find("columns"), kMinRoomColumns, kMaxRoomColumns, columns) ||
            !readInt(size.find("rows"), 1, kMaxRoomRows, rows)) {
            return invalid("world.room.size", "Room size needs columns 20-64 and rows 1-16.");
        }
        output.columns_ = columns;
        output.rows_ = rows;
        output.present_ |= kSize;
    }
    const JsonValue background = document.find("background");
    if (background.valid()) {
        if (!background.isArray() || background.size() != 3) {
            return invalid("world.room.background", "Background must be [r, g, b].");
        }
        for (std::size_t index = 0; index < 3; ++index) {
            std::int32_t channel{};
            if (!readInt(background.at(index), 0, 255, channel)) {
                return invalid("world.room.background", "Background channels must be 0-255.");
            }
            output.background_[index] = static_cast<std::uint8_t>(channel);
        }
        output.present_ |= kBackground;
    }
    const JsonValue projection = document.find("projection");
    if (projection.valid()) {
        std::string_view value;
        if (!projection.string(value) || (value != "side" && value != "isometric")) {
            return invalid("world.room.projection", "Projection must be side or isometric.");
        }
        output.projection_ = value == "isometric" ? RoomProjection::Isometric
                                                  : RoomProjection::Side;
        output.present_ |= kProjection;
    }
    const JsonValue tilesets = document.find("tilesets");
    if (tilesets.valid()) {
        if (!tilesets.isArray() || tilesets.size() > kMaxRoomTilesets) {
            return {ErrorCode::CapacityExceeded, "world.room.tilesets",
                    "Tilesets must be an array of at most 16 entries."};
        }
        for (std::size_t index = 0; index < tilesets.size(); ++index) {
            const JsonValue entry = tilesets.at(index);
            TilesetSpec& tileset = output.tilesets_[index];
            std::string_view tilesetId;
            std::string_view sheet;
            const JsonValue margin = entry.find("margin");
            const JsonValue spacing = entry.find("spacing");
            if (!allowedFields(entry, kTilesetFields) || !entry.find("id").string(tilesetId) ||
                !validKey(tilesetId, tileset.id.size()) || !copyText(tilesetId, tileset.id) ||
                !entry.find("sheet").string(sheet) || !WorldModule::validSpritePath(sheet) ||
                !copyText(sheet, tileset.sheet) ||
                !readInt(entry.find("tileWidth"), 1, 256, tileset.tileWidth) ||
                !readInt(entry.find("tileHeight"), 1, 256, tileset.tileHeight) ||
                (margin.valid() && !readInt(margin, 0, 64, tileset.margin)) ||
                (spacing.valid() && !readInt(spacing, 0, 64, tileset.spacing))) {
                return invalid("world.room.tileset",
                               "Tilesets need id, sprites/*.png sheet, tile size 1-256 and margin/spacing 0-64.");
            }
            for (std::size_t previous = 0; previous < index; ++previous) {
                if (text(output.tilesets_[previous].id) == tilesetId) {
                    return {ErrorCode::DuplicateId, "world.room.tileset", "Tileset IDs must be unique."};
                }
            }
            ++output.tilesetCount_;
        }
    }
    const JsonValue tiles = document.find("tiles");
    if (tiles.valid()) {
        if (!uniqueKeys(tiles) || tiles.size() > kMaxRoomTileTypes) {
            return {ErrorCode::CapacityExceeded, "world.room.tiles",
                    "Tiles must be an object of at most 64 unique symbols."};
        }
        for (std::size_t index = 0; index < tiles.size(); ++index) {
            std::string_view symbol;
            JsonValue entry{};
            (void)tiles.memberAt(index, symbol, entry);
            TileType& tile = output.tileTypes_[index];
            std::string_view tileset;
            const JsonValue rotation = entry.find("rotation");
            const JsonValue flipX = entry.find("flipX");
            const JsonValue flipY = entry.find("flipY");
            const JsonValue collision = entry.find("collision");
            if (symbol.size() != 1 || symbol[0] <= ' ' || symbol[0] > '~' || symbol[0] == '.' ||
                !allowedFields(entry, kTileFields) || !entry.find("tileset").string(tileset) ||
                !validKey(tileset, tile.tileset.size()) || !copyText(tileset, tile.tileset) ||
                !readInt(entry.find("x"), 0, 255, tile.column) ||
                !readInt(entry.find("y"), 0, 255, tile.row) ||
                (rotation.valid() && !readInt(rotation, 0, 270, tile.rotation)) ||
                tile.rotation % 90 != 0 ||
                (flipX.valid() && !flipX.boolean(tile.flipX)) ||
                (flipY.valid() && !flipY.boolean(tile.flipY))) {
                return invalid("world.room.tile",
                               "Tiles need a one-character symbol, tileset ID, sheet x/y and rotation 0/90/180/270.");
            }
            tile.symbol = symbol[0];
            if (collision.valid()) {
                std::string_view value;
                if (!collision.string(value)) {
                    return invalid("world.room.tile.collision", "Collision must be none, solid or hazard.");
                }
                if (value == "solid") {
                    tile.collision = TileCollision::Solid;
                } else if (value == "hazard") {
                    tile.collision = TileCollision::Hazard;
                } else if (value != "none") {
                    return invalid("world.room.tile.collision", "Collision must be none, solid or hazard.");
                }
            }
            ++output.tileTypeCount_;
        }
    }
    const JsonValue layers = document.find("layers");
    if (layers.valid()) {
        if (!layers.isArray() || layers.size() == 0 || layers.size() > kMaxRoomLayers) {
            return {ErrorCode::CapacityExceeded, "world.room.layers", "Rooms need 1-4 tile layers."};
        }
        for (std::size_t index = 0; index < layers.size(); ++index) {
            const JsonValue entry = layers.at(index);
            TileLayer& layer = output.layers_[index];
            std::string_view layerName;
            const JsonValue rows = entry.find("rows");
            const JsonValue scrollFactorX = entry.find("scrollFactorX");
            if (!allowedFields(entry, kLayerFields) || !entry.find("name").string(layerName) ||
                !validKey(layerName, layer.name.size()) || !copyText(layerName, layer.name) ||
                !rows.isArray() || rows.size() == 0 ||
                rows.size() > static_cast<std::size_t>(kMaxRoomRows)) {
                return invalid("world.room.layer", "Layers need a name and 1-16 rows.");
            }
            if (scrollFactorX.valid() &&
                !readFloat(scrollFactorX, 0.0, 1.0, layer.scrollFactorX)) {
                return invalid("world.room.layer.scrollFactorX",
                               "scrollFactorX must be a finite number from 0 to 1.");
            }
            layer.rows = static_cast<std::int32_t>(rows.size());
            for (std::size_t row = 0; row < rows.size(); ++row) {
                std::string_view line;
                if (!rows.at(row).string(line) || line.empty() ||
                    line.size() > static_cast<std::size_t>(kMaxRoomColumns) ||
                    (row != 0 && line.size() != static_cast<std::size_t>(layer.columns))) {
                    return invalid("world.room.layer.rows",
                                   "Layer rows must be equally long strings of at most 64 symbols.");
                }
                layer.columns = static_cast<std::int32_t>(line.size());
                for (std::size_t column = 0; column < line.size(); ++column) {
                    layer.cells[row * kMaxRoomColumns + column] = line[column];
                }
            }
            ++output.layerCount_;
        }
        output.present_ |= kLayers;
    }
    const JsonValue exits = document.find("exits");
    if (exits.valid()) {
        if (!allowedFields(exits, kExitSides)) {
            return invalid("world.room.exits", "Exits support only left and right.");
        }
        for (std::size_t side = 0; side < 2; ++side) {
            const JsonValue entry = exits.find(kExitSides[side]);
            if (!entry.valid()) continue;
            output.present_ |= side == 0 ? kExitLeft : kExitRight;
            RoomExit& exit = output.exits_[side];
            if (entry.isObject() && entry.size() == 0) continue;
            if (!allowedFields(entry, kExitFields)) {
                return invalid("world.room.exit", "An exit is {} or has room, requiresAbility, completesGame.");
            }
            const JsonValue room = entry.find("room");
            const JsonValue ability = entry.find("requiresAbility");
            const JsonValue completes = entry.find("completesGame");
            std::string_view value;
            if ((room.valid() && (!room.string(value) || !isValidContentId(value) ||
                                  !hasType(value, "room") || !copyText(value, exit.room))) ||
                (ability.valid() && (!ability.string(value) || !isValidContentId(value) ||
                                     !copyText(value, exit.requiresAbility))) ||
                (completes.valid() && !completes.boolean(exit.completesGame)) ||
                (room.valid() == exit.completesGame)) {
                return invalid("world.room.exit",
                               "An exit needs either a target room or completesGame, with an optional ability ID.");
            }
            exit.present = true;
        }
    }
    const JsonValue objects = document.find("objects");
    if (objects.valid()) {
        if (!objects.isArray() || objects.size() > kMaxRoomPlacements) {
            return {ErrorCode::CapacityExceeded, "world.room.objects",
                    "Rooms place at most 16 objects."};
        }
        for (std::size_t index = 0; index < objects.size(); ++index) {
            const JsonValue entry = objects.at(index);
            ObjectPlacement& placement = output.placements_[index];
            std::string_view placementId;
            std::string_view object;
            if (!allowedFields(entry, kPlacementFields) || !entry.find("id").string(placementId) ||
                !validKey(placementId, placement.id.size()) || !copyText(placementId, placement.id) ||
                !entry.find("object").string(object) || !isValidContentId(object) ||
                !hasType(object, "object") || !copyText(object, placement.object)) {
                return invalid("world.room.object", "Placements need a unique id and an object ID.");
            }
            for (std::size_t previous = 0; previous < index; ++previous) {
                if (text(output.placements_[previous].id) == placementId) {
                    return {ErrorCode::DuplicateId, "world.room.object", "Placement IDs must be unique per room."};
                }
            }
            const JsonValue position = entry.find("position");
            if (position.valid()) {
                if (!readPair(position, 0.0, 4096.0, placement.x, placement.y)) {
                    return invalid("world.room.object.position", "Placement position must be [x, y] within 0-4096.");
                }
                placement.hasPosition = true;
            }
            const JsonValue facing = entry.find("facing");
            if (facing.valid()) {
                if (!readFacing(facing, placement.facing)) {
                    return invalid("world.room.object.facing", "Facing must be left or right.");
                }
                placement.hasFacing = true;
            }
            const JsonValue properties = entry.find("properties");
            if (properties.valid()) {
                const Status status = parseProperties(properties, placement.properties);
                if (!status) return {status.code, "world.room.object.properties", status.detail};
            }
            ++output.placementCount_;
        }
    }
    return Status::success();
}

Status WorldParser::mergeRoom(const RoomDefinition& parent, RoomDefinition& child) noexcept {
    RoomDefinition merged = parent;
    merged.id_ = child.id_;
    merged.parent_ = child.parent_;
    merged.abstract_ = child.abstract_;
    const std::uint32_t own = child.present_;
    if ((own & kName) != 0U) merged.name_ = child.name_;
    if ((own & kSize) != 0U) {
        merged.columns_ = child.columns_;
        merged.rows_ = child.rows_;
    }
    if ((own & kBackground) != 0U) merged.background_ = child.background_;
    if ((own & kProjection) != 0U) merged.projection_ = child.projection_;
    if ((own & kLayers) != 0U) {
        merged.layers_ = child.layers_;
        merged.layerCount_ = child.layerCount_;
    }
    if ((own & kExitLeft) != 0U) merged.exits_[0] = child.exits_[0];
    if ((own & kExitRight) != 0U) merged.exits_[1] = child.exits_[1];
    merged.present_ = parent.present_ | own;
    for (const TilesetSpec& tileset : child.tilesets()) {
        std::size_t index = 0;
        while (index < merged.tilesetCount_ && text(merged.tilesets_[index].id) != text(tileset.id)) ++index;
        if (index == kMaxRoomTilesets) {
            return {ErrorCode::CapacityExceeded, "world.room.tilesets", "Inherited tilesets exceed 16 entries."};
        }
        merged.tilesets_[index] = tileset;
        merged.tilesetCount_ = std::max(merged.tilesetCount_, index + 1);
    }
    for (const TileType& tile : child.tileTypes()) {
        std::size_t index = 0;
        while (index < merged.tileTypeCount_ && merged.tileTypes_[index].symbol != tile.symbol) ++index;
        if (index == kMaxRoomTileTypes) {
            return {ErrorCode::CapacityExceeded, "world.room.tiles", "Inherited tiles exceed 64 symbols."};
        }
        merged.tileTypes_[index] = tile;
        merged.tileTypeCount_ = std::max(merged.tileTypeCount_, index + 1);
    }
    for (const ObjectPlacement& placement : child.placements()) {
        std::size_t index = 0;
        while (index < merged.placementCount_ && text(merged.placements_[index].id) != text(placement.id)) ++index;
        if (index == kMaxRoomPlacements) {
            return {ErrorCode::CapacityExceeded, "world.room.objects", "Inherited placements exceed 16 objects."};
        }
        merged.placements_[index] = placement;
        merged.placementCount_ = std::max(merged.placementCount_, index + 1);
    }
    child = merged;
    return Status::success();
}

Status WorldParser::validateRoom(RoomDefinition& room) noexcept {
    if (room.isAbstract()) return Status::success();
    if ((room.present_ & (kName | kSize | kLayers)) != (kName | kSize | kLayers)) {
        return {ErrorCode::MissingDependency, room.id(),
                "A concrete room needs name, size and layers (own or inherited)."};
    }
    for (std::size_t index = 0; index < room.tileTypeCount_; ++index) {
        TileType& tile = room.tileTypes_[index];
        std::size_t tileset = 0;
        while (tileset < room.tilesetCount_ && text(room.tilesets_[tileset].id) != text(tile.tileset)) ++tileset;
        if (tileset == room.tilesetCount_) {
            return {ErrorCode::MissingDependency, room.id(), "A tile references an undefined tileset."};
        }
        tile.tilesetIndex = tileset;
    }
    for (std::size_t index = 0; index < room.layerCount_; ++index) {
        const TileLayer& layer = room.layers_[index];
        if (room.projection_ == RoomProjection::Isometric && layer.scrollFactorX != 1.0F) {
            return invalid("world.room.layer.scrollFactorX",
                           "Parallax scrolling is supported only in side-view rooms.");
        }
        if (layer.columns != room.columns_ || layer.rows != room.rows_) {
            return {ErrorCode::InvalidValue, room.id(), "Every layer must match the room size exactly."};
        }
        for (int row = 0; row < layer.rows; ++row) {
            for (int column = 0; column < layer.columns; ++column) {
                const char symbol = layer.cells[static_cast<std::size_t>(row) * kMaxRoomColumns +
                                                static_cast<std::size_t>(column)];
                if (symbol != '.' && symbol != ' ' && room.findTileType(symbol) == nullptr) {
                    return {ErrorCode::MissingDependency, room.id(), "A layer uses an undefined tile symbol."};
                }
            }
        }
    }
    return Status::success();
}

const AnimationSpec* ObjectDefinition::findAnimation(std::string_view name) const noexcept {
    for (std::size_t index = 0; index < animationCount_; ++index) {
        if (text(animations_[index].name) == name) return &animations_[index];
    }
    return nullptr;
}

const AnimationSpec* ObjectDefinition::animationOrDefault(std::string_view name) const noexcept {
    if (const AnimationSpec* found = findAnimation(name)) return found;
    if (const AnimationSpec* fallback = findAnimation(defaultAnimation())) return fallback;
    return animationCount_ != 0 ? &animations_[0] : nullptr;
}

const TileType* RoomDefinition::findTileType(char symbol) const noexcept {
    for (std::size_t index = 0; index < tileTypeCount_; ++index) {
        if (tileTypes_[index].symbol == symbol) return &tileTypes_[index];
    }
    return nullptr;
}

const TileType* RoomDefinition::tile(std::size_t layer, int column, int row) const noexcept {
    if (layer >= layerCount_ || column < 0 || row < 0 || column >= columns_ || row >= rows_) return nullptr;
    const char symbol = layers_[layer].cells[static_cast<std::size_t>(row) * kMaxRoomColumns +
                                             static_cast<std::size_t>(column)];
    return symbol == '.' || symbol == ' ' ? nullptr : findTileType(symbol);
}

TileCollision RoomDefinition::collision(int column, int row) const noexcept {
    TileCollision result = TileCollision::None;
    for (std::size_t layer = 0; layer < layerCount_; ++layer) {
        const TileType* type = tile(layer, column, row);
        if (type == nullptr) continue;
        if (type->collision == TileCollision::Hazard) return TileCollision::Hazard;
        if (type->collision == TileCollision::Solid) result = TileCollision::Solid;
    }
    return result;
}

const ObjectPlacement* RoomDefinition::findPlacement(std::string_view id) const noexcept {
    for (std::size_t index = 0; index < placementCount_; ++index) {
        if (text(placements_[index].id) == id) return &placements_[index];
    }
    return nullptr;
}

bool WorldModule::contentPath(std::string_view id, char* output, std::size_t capacity) noexcept {
    if (output == nullptr || capacity == 0 || !isValidContentId(id)) return false;
    const std::size_t slash = id.find('/');
    if (slash == std::string_view::npos) return false;
    // Non-throwing slices: the PS5 runtime has no exception support.
    const std::string_view name(id.data() + slash + 1, id.size() - slash - 1);
    const char* folder = hasType(id, "object") ? "objects" : hasType(id, "room") ? "rooms" : nullptr;
    if (folder == nullptr || !validFileName(name)) return false;
    const int written = std::snprintf(output, capacity, "world/%s/%.*s.json", folder,
                                      static_cast<int>(name.size()), name.data());
    return written > 0 && static_cast<std::size_t>(written) < capacity;
}

bool WorldModule::validSpritePath(std::string_view path) noexcept {
    constexpr std::string_view kPrefix = "sprites/";
    constexpr std::string_view kSuffix = ".png";
    if (path.size() <= kPrefix.size() + kSuffix.size() || path.size() >= 96 ||
        std::string_view(path.data(), kPrefix.size()) != kPrefix ||
        std::string_view(path.data() + path.size() - kSuffix.size(), kSuffix.size()) != kSuffix ||
        path.find("..") != std::string_view::npos || path.find("//") != std::string_view::npos) {
        return false;
    }
    return std::all_of(path.begin(), path.end(), [](char character) {
        return (character >= 'a' && character <= 'z') || (character >= '0' && character <= '9') ||
               character == '_' || character == '-' || character == '/' || character == '.';
    });
}

void WorldModule::reset() noexcept {
    if (initialized_) return;
    manifestObjectCount_ = 0;
    manifestRoomCount_ = 0;
    startRoom_.fill('\0');
    objectCount_ = 0;
    roomCount_ = 0;
    manifestLoaded_ = false;
    finalized_ = false;
}

Status WorldModule::loadManifest(JsonValue document) noexcept {
    if (initialized_ || manifestLoaded_) {
        return {ErrorCode::InvalidState, "echo.world", "The world manifest can be loaded only once."};
    }
    constexpr std::array<std::string_view, 4> kFields{"schemaVersion", "startRoom", "objects", "rooms"};
    std::int64_t version{};
    std::string_view start;
    const JsonValue objects = document.find("objects");
    const JsonValue rooms = document.find("rooms");
    if (!allowedFields(document, kFields) || !document.find("schemaVersion").integer(version) ||
        version != 1 || !document.find("startRoom").string(start) || !objects.isArray() ||
        !rooms.isArray() || objects.size() == 0 || rooms.size() == 0) {
        return invalid("world.manifest", "The world manifest needs schemaVersion 1, startRoom, objects and rooms.");
    }
    if (objects.size() > kMaxWorldObjects || rooms.size() > kMaxWorldRooms) {
        return {ErrorCode::CapacityExceeded, "world.manifest",
                "The world manifest lists more objects or rooms than the fixed capacity."};
    }
    std::array<char, 64> path{};
    std::array<char, 64> otherPath{};
    const auto readList = [&](JsonValue list, auto& output, std::string_view type) -> bool {
        for (std::size_t index = 0; index < list.size(); ++index) {
            std::string_view id;
            if (!list.at(index).string(id) || !hasType(id, type) ||
                !contentPath(id, path.data(), path.size()) || !copyText(id, output[index])) {
                return false;
            }
            for (std::size_t previous = 0; previous < index; ++previous) {
                if (!contentPath(text(output[previous]), otherPath.data(), otherPath.size()) ||
                    std::string_view(otherPath.data()) == std::string_view(path.data())) {
                    return false;
                }
            }
        }
        return true;
    };
    if (!readList(objects, manifestObjects_, "object") || !readList(rooms, manifestRooms_, "room")) {
        return invalid("world.manifest",
                       "Manifest entries must be unique object/room IDs with lowercase file-safe names.");
    }
    if (!hasType(start, "room") || !copyText(start, startRoom_)) {
        return invalid("world.manifest.startRoom", "The start room must be a room ID.");
    }
    manifestObjectCount_ = objects.size();
    manifestRoomCount_ = rooms.size();
    manifestLoaded_ = true;
    return Status::success();
}

std::string_view WorldModule::manifestObject(std::size_t index) const noexcept {
    return index < manifestObjectCount_ ? text(manifestObjects_[index]) : std::string_view{};
}

std::string_view WorldModule::manifestRoom(std::size_t index) const noexcept {
    return index < manifestRoomCount_ ? text(manifestRooms_[index]) : std::string_view{};
}

Status WorldModule::addObject(std::string_view expectedId, JsonValue document) noexcept {
    if (initialized_ || finalized_ || !manifestLoaded_) {
        return {ErrorCode::InvalidState, "echo.world", "Objects are added after the manifest and before finalize."};
    }
    bool listed = false;
    for (std::size_t index = 0; index < manifestObjectCount_; ++index) {
        listed = listed || text(manifestObjects_[index]) == expectedId;
    }
    if (!listed || findRawObject(expectedId) != nullptr || objectCount_ == kMaxWorldObjects) {
        return {ErrorCode::InvalidArgument, "world.object", "The object is not listed or was already added."};
    }
    ObjectDefinition& slot = objects_[objectCount_];
    const Status status = WorldParser::parseObject(document, slot);
    if (!status) return status;
    if (slot.id() != expectedId) {
        slot = {};
        return invalid("world.object.id", "The object file ID does not match its manifest entry.");
    }
    ++objectCount_;
    return Status::success();
}

Status WorldModule::addRoom(std::string_view expectedId, JsonValue document) noexcept {
    if (initialized_ || finalized_ || !manifestLoaded_) {
        return {ErrorCode::InvalidState, "echo.world", "Rooms are added after the manifest and before finalize."};
    }
    bool listed = false;
    for (std::size_t index = 0; index < manifestRoomCount_; ++index) {
        listed = listed || text(manifestRooms_[index]) == expectedId;
    }
    if (!listed || findRawRoom(expectedId) != nullptr || roomCount_ == kMaxWorldRooms) {
        return {ErrorCode::InvalidArgument, "world.room", "The room is not listed or was already added."};
    }
    RoomDefinition& slot = rooms_[roomCount_];
    const Status status = WorldParser::parseRoom(document, slot);
    if (!status) return status;
    if (slot.id() != expectedId) {
        slot = {};
        return invalid("world.room.id", "The room file ID does not match its manifest entry.");
    }
    ++roomCount_;
    return Status::success();
}

ObjectDefinition* WorldModule::findRawObject(std::string_view id) noexcept {
    for (std::size_t index = 0; index < objectCount_; ++index) {
        if (objects_[index].id() == id) return &objects_[index];
    }
    return nullptr;
}

RoomDefinition* WorldModule::findRawRoom(std::string_view id) noexcept {
    for (std::size_t index = 0; index < roomCount_; ++index) {
        if (rooms_[index].id() == id) return &rooms_[index];
    }
    return nullptr;
}

const ObjectDefinition* WorldModule::findObject(std::string_view id) const noexcept {
    if (!finalized_) return nullptr;
    for (std::size_t index = 0; index < objectCount_; ++index) {
        if (objects_[index].id() == id) return &objects_[index];
    }
    return nullptr;
}

const RoomDefinition* WorldModule::findRoom(std::string_view id) const noexcept {
    if (!finalized_) return nullptr;
    for (std::size_t index = 0; index < roomCount_; ++index) {
        if (rooms_[index].id() == id) return &rooms_[index];
    }
    return nullptr;
}

// Parents are resolved before children; every pass resolves at least one more
// inheritance level, so a pass without progress means a missing parent or a cycle.
Status WorldModule::resolveObjects() noexcept {
    std::array<std::size_t, kMaxWorldObjects> depth{};
    std::size_t remaining = objectCount_;
    while (remaining != 0) {
        std::size_t progress = 0;
        for (std::size_t index = 0; index < objectCount_; ++index) {
            ObjectDefinition& object = objects_[index];
            if (object.resolved_) continue;
            if (object.parent().empty()) {
                object.resolved_ = true;
            } else {
                ObjectDefinition* parent = findRawObject(object.parent());
                if (parent == nullptr) {
                    return {ErrorCode::MissingDependency, object.id(), "The parent object is not listed in the world."};
                }
                if (!parent->resolved_) continue;
                const std::size_t parentIndex = static_cast<std::size_t>(parent - objects_.data());
                depth[index] = depth[parentIndex] + 1;
                if (depth[index] > kMaxInheritanceDepth) {
                    return {ErrorCode::CapacityExceeded, object.id(), "Object inheritance is deeper than 8 levels."};
                }
                const Status status = WorldParser::mergeObject(*parent, object);
                if (!status) return {status.code, object.id(), status.detail};
                object.resolved_ = true;
            }
            ++progress;
            --remaining;
        }
        if (progress == 0) {
            return {ErrorCode::DependencyCycle, "world.object.parent", "Object inheritance contains a cycle."};
        }
    }
    for (std::size_t index = 0; index < objectCount_; ++index) {
        const Status status = WorldParser::validateObject(objects_[index]);
        if (!status) return status;
    }
    return Status::success();
}

Status WorldModule::resolveRooms() noexcept {
    std::array<std::size_t, kMaxWorldRooms> depth{};
    std::size_t remaining = roomCount_;
    while (remaining != 0) {
        std::size_t progress = 0;
        for (std::size_t index = 0; index < roomCount_; ++index) {
            RoomDefinition& room = rooms_[index];
            if (room.resolved_) continue;
            if (room.parent().empty()) {
                room.resolved_ = true;
            } else {
                RoomDefinition* parent = findRawRoom(room.parent());
                if (parent == nullptr) {
                    return {ErrorCode::MissingDependency, room.id(), "The parent room is not listed in the world."};
                }
                if (!parent->resolved_) continue;
                const std::size_t parentIndex = static_cast<std::size_t>(parent - rooms_.data());
                depth[index] = depth[parentIndex] + 1;
                if (depth[index] > kMaxInheritanceDepth) {
                    return {ErrorCode::CapacityExceeded, room.id(), "Room inheritance is deeper than 8 levels."};
                }
                const Status status = WorldParser::mergeRoom(*parent, room);
                if (!status) return {status.code, room.id(), status.detail};
                room.resolved_ = true;
            }
            ++progress;
            --remaining;
        }
        if (progress == 0) {
            return {ErrorCode::DependencyCycle, "world.room.parent", "Room inheritance contains a cycle."};
        }
    }
    for (std::size_t index = 0; index < roomCount_; ++index) {
        const Status status = WorldParser::validateRoom(rooms_[index]);
        if (!status) return status;
    }
    return Status::success();
}

Status WorldModule::validateReferences() noexcept {
    const RoomDefinition* start = findRawRoom(startRoom());
    if (start == nullptr || start->isAbstract()) {
        return {ErrorCode::MissingDependency, "world.manifest.startRoom",
                "The start room must be a concrete listed room."};
    }
    for (std::size_t index = 0; index < roomCount_; ++index) {
        const RoomDefinition& room = rooms_[index];
        if (room.isAbstract()) continue;
        const float width = static_cast<float>(room.columns() * 32);
        const float height = static_cast<float>(room.rows() * 32);
        for (const RoomExit* exit : {&room.leftExit(), &room.rightExit()}) {
            if (!exit->present || exit->completesGame) continue;
            const RoomDefinition* target = findRawRoom(text(exit->room));
            if (target == nullptr || target->isAbstract()) {
                return {ErrorCode::MissingDependency, room.id(), "An exit targets an unknown or abstract room."};
            }
        }
        for (const ObjectPlacement& placement : room.placements()) {
            const ObjectDefinition* object = findRawObject(text(placement.object));
            if (object == nullptr || object->isAbstract()) {
                return {ErrorCode::MissingDependency, room.id(), "A placement references an unknown or abstract object."};
            }
            const float x = placement.hasPosition ? placement.x : object->spawnX();
            const float y = placement.hasPosition ? placement.y : object->spawnY();
            if (x > width || y > height) {
                return {ErrorCode::InvalidValue, room.id(), "A placement lies outside its room."};
            }
        }
    }
    return Status::success();
}

Status WorldModule::finalize() noexcept {
    if (initialized_ || finalized_ || !manifestLoaded_) {
        return {ErrorCode::InvalidState, "echo.world", "The world can be finalized once after loading its manifest."};
    }
    if (objectCount_ != manifestObjectCount_ || roomCount_ != manifestRoomCount_) {
        return {ErrorCode::MissingDependency, "world.manifest", "Not every listed object and room was loaded."};
    }
    Status status = resolveObjects();
    if (status) status = resolveRooms();
    if (status) status = validateReferences();
    if (!status) return status;
    finalized_ = true;
    return Status::success();
}

const ModuleInfo& WorldModule::info() const noexcept { return kModuleInfo; }

bool WorldModule::initialize(ModuleContext&) noexcept {
    initialized_ = finalized_;
    return initialized_;
}

void WorldModule::shutdown() noexcept { initialized_ = false; }

std::string_view objectCategoryName(ObjectCategory category) noexcept {
    const auto index = static_cast<std::size_t>(category);
    return index < kCategoryNames.size() ? kCategoryNames[index] : std::string_view{};
}

CapabilityManifestFragment worldCapabilityManifestFragment() noexcept {
    return {.modules = kModules, .contentSchemas = kSchemas};
}

} // namespace echo::framework
