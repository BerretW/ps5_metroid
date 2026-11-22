#include "game/game.hpp"

#include <algorithm>
#include <cmath>
#include <initializer_list>

namespace echo {
namespace {

using framework::ErrorCode;
using framework::ObjectCategory;
using framework::ObjectDefinition;
using framework::ObjectPlacement;
using framework::ObjectProperty;
using framework::ObjectPropertyType;
using framework::Status;

constexpr float kLayoutFloorY = 292.0F;
constexpr float kLayoutPlayerWidth = 20.0F;
constexpr float kLayoutPlayerHeight = 28.0F;
constexpr int kMaxEnemyHealth = 99;
constexpr float kMaxEnemySpeed = 400.0F;
constexpr std::string_view kEchoStepAbility = "echo:ability/echo_step";
constexpr std::string_view kMaintenanceKeyItem = "echo:item/maintenance_key";

struct DefaultRoom {
    std::string_view name;
    std::span<const TileSpan> tiles;
    std::array<std::uint8_t, 3> background;
    RoomExitLayout left;
    RoomExitLayout right;
};

constexpr TileSpan kLandingTiles[] = {
    {0, 10, kRoomColumns, Tile::Solid}, {5, 8, 5, Tile::Solid},
    {14, 7, 5, Tile::Solid}, {25, 8, 4, Tile::Solid}, {22, 8, 3, Tile::Hazard},
};
constexpr TileSpan kMaintenanceTiles[] = {
    {0, 10, kRoomColumns, Tile::Solid}, {6, 8, 4, Tile::Solid},
    {14, 7, 5, Tile::Solid}, {18, 8, 3, Tile::Hazard},
};
constexpr TileSpan kPowerTiles[] = {
    {0, 10, kRoomColumns, Tile::Solid}, {4, 8, 5, Tile::Solid},
    {13, 7, 5, Tile::Solid}, {23, 8, 5, Tile::Solid}, {31, 8, 3, Tile::Hazard},
};
constexpr TileSpan kLabTiles[] = {
    {0, 10, kRoomColumns, Tile::Solid}, {8, 8, 6, Tile::Solid}, {23, 8, 6, Tile::Solid},
};
constexpr TileSpan kCentralTiles[] = {
    {0, 10, kRoomColumns, Tile::Solid}, {7, 8, 5, Tile::Solid},
    {18, 7, 5, Tile::Solid}, {29, 8, 5, Tile::Solid},
};

const std::array<DefaultRoom, kRoomCount> kDefaultRooms{{
    {"Landing Bay", kLandingTiles, {13, 30, 43},
     {}, {true, false, false, RoomId::MaintenanceTunnel}},
    {"Maintenance Tunnel", kMaintenanceTiles, {13, 30, 43},
     {true, false, false, RoomId::LandingBay}, {true, false, false, RoomId::PowerStation}},
    {"Power Station", kPowerTiles, {28, 27, 47},
     {true, false, false, RoomId::MaintenanceTunnel},
     {true, false, true, RoomId::CentralChamber}},
    {"Research Laboratory", kLabTiles, {20, 38, 42}, {}, {}},
    {"Central Chamber", kCentralTiles, {13, 30, 43},
     {true, false, false, RoomId::PowerStation}, {true, true, false, RoomId::CentralChamber}},
}};

[[nodiscard]] WorldLayout makeDefaultLayout() noexcept {
    WorldLayout layout{};
    for (std::size_t index = 0; index < kRoomCount; ++index) {
        const DefaultRoom& source = kDefaultRooms[index];
        RoomLayout& room = layout.rooms[index];
        room.name = source.name;
        room.columns = kRoomColumns;
        room.background = source.background;
        room.left = source.left;
        room.right = source.right;
        for (const TileSpan& span : source.tiles) {
            for (int column = span.column; column < span.column + span.length; ++column) {
                Tile& tile = room.tiles[static_cast<std::size_t>(span.row * kMaxLayoutColumns + column)];
                if (tile != Tile::Hazard) tile = span.tile;
            }
        }
    }
    layout.enemies = {{
        {RoomId::MaintenanceTunnel, {760.0F, kLayoutFloorY, 28.0F, 28.0F}, 700.0F, 910.0F, 45.0F, 2},
        {RoomId::PowerStation, {790.0F, kLayoutFloorY, 30.0F, 28.0F}, 720.0F, 940.0F, 52.0F, 3},
        {RoomId::MaintenanceTunnel, {390.0F, kLayoutFloorY, 28.0F, 28.0F}, 340.0F, 520.0F, 70.0F, 3,
         EnemyType::Charger},
        {RoomId::PowerStation, {530.0F, kLayoutFloorY, 30.0F, 28.0F}, 480.0F, 650.0F, 26.0F, 6,
         EnemyType::Armored},
        {RoomId::ResearchLab, {1000.0F, kLayoutFloorY, 28.0F, 28.0F}, 960.0F, 1100.0F, 0.0F, 4,
         EnemyType::Sentry},
    }};
    layout.doors[0] = {RoomId::MaintenanceTunnel, {970.0F, 272.0F, 44.0F, 48.0F},
                       RoomId::ResearchLab, true, true, true};
    layout.doors[1] = {RoomId::ResearchLab, {0.0F, 0.0F, 120.0F, 384.0F},
                       RoomId::MaintenanceTunnel, false, false, false};
    layout.doorCount = 2;
    layout.startRoom = RoomId::LandingBay;
    layout.playerStart = {42.0F, kLayoutFloorY};
    layout.abilityRoom = RoomId::ResearchLab;
    layout.abilityPickup = {690.0F, kLayoutFloorY + 2.0F, 20.0F, 24.0F};
    return layout;
}

[[nodiscard]] Status invalid(std::string_view field, std::string_view detail) noexcept {
    return {ErrorCode::InvalidValue, field, detail};
}

[[nodiscard]] bool roomFromId(std::string_view id, RoomId& room) noexcept {
    for (std::size_t index = 0; index < kRoomResourceIds.size(); ++index) {
        if (kRoomResourceIds[index] == id) {
            room = static_cast<RoomId>(index);
            return true;
        }
    }
    return false;
}

// Placement properties override the object's inherited properties.
[[nodiscard]] const ObjectProperty* findProperty(const ObjectPlacement& placement,
                                                 const ObjectDefinition& object,
                                                 std::string_view key) noexcept {
    const ObjectProperty* property = placement.properties.find(key);
    return property != nullptr ? property : object.property(key);
}

[[nodiscard]] bool readNumber(const ObjectProperty* property, float& output) noexcept {
    if (property == nullptr || (property->type != ObjectPropertyType::Integer &&
                                property->type != ObjectPropertyType::Number) ||
        !std::isfinite(property->number)) {
        return false;
    }
    output = static_cast<float>(property->number);
    return true;
}

[[nodiscard]] bool readText(const ObjectProperty* property, std::string_view& output) noexcept {
    if (property == nullptr || property->type != ObjectPropertyType::String) return false;
    output = property->text.data();
    return true;
}

[[nodiscard]] bool fitsRoom(const Rect& bounds, const RoomLayout& room) noexcept {
    return std::isfinite(bounds.x) && std::isfinite(bounds.y) && bounds.width > 0.0F &&
           bounds.height > 0.0F && bounds.x >= 0.0F && bounds.y >= 0.0F &&
           bounds.x + bounds.width <= room.width() &&
           bounds.y + bounds.height <= static_cast<float>(kRoomHeight);
}

[[nodiscard]] bool collides(const RoomLayout& room, const Rect& bounds) noexcept {
    const int left = static_cast<int>(std::floor(bounds.x / kTileSize));
    const int right = static_cast<int>(std::floor((bounds.x + bounds.width - 0.01F) / kTileSize));
    const int top = static_cast<int>(std::floor(bounds.y / kTileSize));
    const int bottom = static_cast<int>(std::floor((bounds.y + bounds.height - 0.01F) / kTileSize));
    for (int row = std::max(0, top); row <= std::min(kRoomRows - 1, bottom); ++row) {
        for (int column = std::max(0, left); column <= std::min(room.columns - 1, right); ++column) {
            if (room.tiles[static_cast<std::size_t>(row * kMaxLayoutColumns + column)] == Tile::Solid) {
                return true;
            }
        }
    }
    return false;
}

[[nodiscard]] Status readExit(const framework::RoomExit& source, RoomExitLayout& output) noexcept {
    output = {};
    if (!source.present) return Status::success();
    output.open = true;
    output.completesGame = source.completesGame;
    const std::string_view ability = source.requiresAbility.data();
    if (!ability.empty() && ability != kEchoStepAbility) {
        return invalid("world.room.exits",
                       "Room exits can only require the compiled Echo Step ability.");
    }
    output.requiresEchoStep = !ability.empty();
    if (!output.completesGame && !roomFromId(source.room.data(), output.room)) {
        return invalid("world.room.exits", "Room exits must lead to one of the five game rooms.");
    }
    return Status::success();
}

[[nodiscard]] const ObjectDefinition* requiredObject(const framework::WorldModule& world,
                                                     std::string_view id,
                                                     ObjectCategory category) noexcept {
    const ObjectDefinition* object = world.findObject(id);
    return object != nullptr && !object->isAbstract() && object->category() == category
        ? object : nullptr;
}

[[nodiscard]] Status addEnemy(const ObjectPlacement& placement, const ObjectDefinition& object,
                              RoomId room, const RoomLayout& roomLayout, WorldLayout& layout,
                              std::array<bool, kEnemyCount>& usedSlots) noexcept {
    // Each room's enemies fill that room's save slots in placement order.
    std::size_t slot = 0;
    while (slot < kEnemyCount && (usedSlots[slot] || kEnemySlotRooms[slot] != room)) ++slot;
    if (slot == kEnemyCount) {
        return {ErrorCode::CapacityExceeded, placement.object.data(),
                "This room has no free enemy save slot (Maintenance 2, Power 2, Lab 1)."};
    }
    EnemySpawn& enemy = layout.enemies[slot];
    enemy = {};
    enemy.room = room;
    enemy.archetype = &object;
    enemy.bounds = {placement.hasPosition ? placement.x : object.spawnX(),
                    placement.hasPosition ? placement.y : object.spawnY(),
                    object.width(), object.height()};
    std::string_view behavior;
    float health{};
    if (!readText(findProperty(placement, object, "behavior"), behavior) ||
        !readNumber(findProperty(placement, object, "health"), health)) {
        return invalid(placement.object.data(), "Enemies need behavior and health properties.");
    }
    if (behavior == "patrol") enemy.type = EnemyType::Patrol;
    else if (behavior == "charger") enemy.type = EnemyType::Charger;
    else if (behavior == "armored") enemy.type = EnemyType::Armored;
    else if (behavior == "sentry") enemy.type = EnemyType::Sentry;
    else return invalid(placement.object.data(), "Unknown enemy behavior.");
    if (health != std::floor(health) || health < 1.0F || health > kMaxEnemyHealth) {
        return invalid(placement.object.data(), "Enemy health must be an integer from 1 to 99.");
    }
    enemy.health = static_cast<int>(health);
    float speed = 0.0F;
    const ObjectProperty* speedProperty = findProperty(placement, object, "speed");
    if (speedProperty != nullptr && !readNumber(speedProperty, speed)) {
        return invalid(placement.object.data(), "Enemy speed must be a number.");
    }
    const framework::Facing facing = placement.hasFacing ? placement.facing : object.facing();
    enemy.speed = facing == framework::Facing::Left ? -speed : speed;
    enemy.patrolLeft = enemy.bounds.x;
    enemy.patrolRight = enemy.bounds.x + enemy.bounds.width;
    const ObjectProperty* minimum = findProperty(placement, object, "patrolMin");
    const ObjectProperty* maximum = findProperty(placement, object, "patrolMax");
    if ((minimum != nullptr && !readNumber(minimum, enemy.patrolLeft)) ||
        (maximum != nullptr && !readNumber(maximum, enemy.patrolRight))) {
        return invalid(placement.object.data(), "Enemy patrol bounds must be numbers.");
    }
    if (!fitsRoom(enemy.bounds, roomLayout) || speed < 0.0F || speed > kMaxEnemySpeed ||
        enemy.patrolLeft < 0.0F || enemy.patrolRight > roomLayout.width() ||
        enemy.bounds.x < enemy.patrolLeft ||
        enemy.bounds.x + enemy.bounds.width > enemy.patrolRight) {
        return invalid(placement.object.data(),
                       "Enemy bounds, speed (0-400) and patrol range must fit its room.");
    }
    usedSlots[slot] = true;
    return Status::success();
}

[[nodiscard]] Status addDoor(const ObjectPlacement& placement, const ObjectDefinition& object,
                             RoomId room, const RoomLayout& roomLayout,
                             WorldLayout& layout) noexcept {
    if (layout.doorCount == layout.doors.size()) {
        return {ErrorCode::CapacityExceeded, placement.object.data(),
                "The world supports at most eight doors and triggers."};
    }
    DoorLayout door{};
    door.room = room;
    door.archetype = &object;
    door.visible = object.category() == ObjectCategory::Door;
    door.bounds = {placement.hasPosition ? placement.x : object.spawnX(),
                   placement.hasPosition ? placement.y : object.spawnY(),
                   object.width(), object.height()};
    std::string_view target;
    std::string_view arrive;
    if (!readText(findProperty(placement, object, "target"), target) ||
        !roomFromId(target, door.destination) ||
        !readText(findProperty(placement, object, "arrive"), arrive) ||
        (arrive != "left" && arrive != "right")) {
        return invalid(placement.object.data(),
                       "Doors need a target game room and arrive set to left or right.");
    }
    door.arriveFromLeft = arrive == "left";
    const ObjectProperty* requirement = findProperty(placement, object, "requiresItem");
    if (requirement != nullptr) {
        std::string_view item;
        if (!readText(requirement, item) || item != kMaintenanceKeyItem) {
            return invalid(placement.object.data(),
                           "Doors can only require the compiled maintenance key.");
        }
        door.requiresKey = true;
    }
    if (!fitsRoom(door.bounds, roomLayout)) {
        return invalid(placement.object.data(), "Door bounds must fit inside the room.");
    }
    layout.doors[layout.doorCount++] = door;
    return Status::success();
}

} // namespace

const WorldLayout& defaultWorldLayout() noexcept {
    static const WorldLayout layout = makeDefaultLayout();
    return layout;
}

Status validateWorldLayout(const WorldLayout& layout) noexcept {
    for (std::size_t index = 0; index < kRoomCount; ++index) {
        const RoomLayout& room = layout.rooms[index];
        if (room.columns < framework::kMinRoomColumns || room.columns > kMaxLayoutColumns ||
            room.name.empty()) {
            return invalid(kRoomResourceIds[index], "Rooms need a name and 20-64 columns.");
        }
        for (const RoomExitLayout* exit : {&room.left, &room.right}) {
            if (exit->open && !exit->completesGame && exit->room >= RoomId::Count) {
                return invalid(kRoomResourceIds[index], "Room exit target is invalid.");
            }
        }
    }
    for (std::size_t index = 0; index < kEnemyCount; ++index) {
        const EnemySpawn& enemy = layout.enemies[index];
        if (enemy.room != kEnemySlotRooms[index] || enemy.health < 1 ||
            enemy.health > kMaxEnemyHealth || !std::isfinite(enemy.speed) ||
            std::abs(enemy.speed) > kMaxEnemySpeed ||
            !fitsRoom(enemy.bounds, layout.rooms[static_cast<std::size_t>(enemy.room)]) ||
            enemy.bounds.x < enemy.patrolLeft ||
            enemy.bounds.x + enemy.bounds.width > enemy.patrolRight) {
            return invalid("world.enemies", "An enemy spawn is outside its save-slot contract.");
        }
    }
    if (layout.doorCount > layout.doors.size() || layout.propCount > layout.props.size()) {
        return {ErrorCode::CapacityExceeded, "world", "World layout lists exceed capacity."};
    }
    for (const DoorLayout& door : layout.doorList()) {
        if (door.room >= RoomId::Count || door.destination >= RoomId::Count ||
            !fitsRoom(door.bounds, layout.rooms[static_cast<std::size_t>(door.room)])) {
            return invalid("world.doors", "A door is outside its room or targets no room.");
        }
    }
    const RoomLayout& start = layout.rooms[static_cast<std::size_t>(RoomId::LandingBay)];
    const Rect player{layout.playerStart.x, layout.playerStart.y,
                      kLayoutPlayerWidth, kLayoutPlayerHeight};
    if (layout.startRoom != RoomId::LandingBay || !fitsRoom(player, start) ||
        collides(start, player)) {
        return invalid("world.player", "The player must start inside the Landing Bay.");
    }
    if (layout.abilityRoom >= RoomId::Count ||
        !fitsRoom(layout.abilityPickup,
                  layout.rooms[static_cast<std::size_t>(layout.abilityRoom)])) {
        return invalid("world.pickup", "The Echo Step pickup must fit inside its room.");
    }
    return Status::success();
}

Status buildWorldLayout(const framework::WorldModule& world, WorldLayout& output) noexcept {
    if (!world.finalized()) {
        return {ErrorCode::InvalidState, "echo.world",
                "World data must be finalized before building the game layout."};
    }
    WorldLayout layout{};
    if (world.startRoom() != kRoomResourceIds[0]) {
        return invalid("world.startRoom", "The world must start in echo:room/landing_bay.");
    }
    for (std::size_t index = 0; index < kRoomCount; ++index) {
        const framework::RoomDefinition* definition = world.findRoom(kRoomResourceIds[index]);
        if (definition == nullptr || definition->isAbstract()) {
            return {ErrorCode::MissingDependency, kRoomResourceIds[index],
                    "A required game room is missing or abstract."};
        }
        if (definition->rows() != kRoomRows) {
            return invalid(kRoomResourceIds[index], "Game rooms must be exactly 12 tiles tall.");
        }
        RoomLayout& room = layout.rooms[index];
        room.name = definition->name();
        room.columns = definition->columns();
        room.background = definition->background();
        room.definition = definition;
        for (int row = 0; row < kRoomRows; ++row) {
            for (int column = 0; column < room.columns; ++column) {
                const framework::TileCollision collision = definition->collision(column, row);
                room.tiles[static_cast<std::size_t>(row * kMaxLayoutColumns + column)] =
                    collision == framework::TileCollision::Hazard ? Tile::Hazard :
                    collision == framework::TileCollision::Solid ? Tile::Solid : Tile::Empty;
            }
        }
        Status status = readExit(definition->leftExit(), room.left);
        if (status) status = readExit(definition->rightExit(), room.right);
        if (!status) return {status.code, kRoomResourceIds[index], status.detail};
    }

    std::array<bool, kEnemyCount> usedSlots{};
    bool pickupFound = false;
    for (std::size_t index = 0; index < kRoomCount; ++index) {
        const RoomId room = static_cast<RoomId>(index);
        const RoomLayout& roomLayout = layout.rooms[index];
        for (const ObjectPlacement& placement : roomLayout.definition->placements()) {
            const ObjectDefinition* object = world.findObject(placement.object.data());
            if (object == nullptr) {
                return {ErrorCode::MissingDependency, placement.object.data(),
                        "A placed object is missing."};
            }
            Status status = Status::success();
            switch (object->category()) {
            case ObjectCategory::Enemy:
                status = addEnemy(placement, *object, room, roomLayout, layout, usedSlots);
                break;
            case ObjectCategory::Door:
            case ObjectCategory::Trigger:
                status = addDoor(placement, *object, room, roomLayout, layout);
                break;
            case ObjectCategory::Pickup: {
                std::string_view ability;
                if (pickupFound ||
                    !readText(findProperty(placement, *object, "ability"), ability) ||
                    ability != kEchoStepAbility) {
                    return invalid(placement.object.data(),
                                   "The world places exactly one Echo Step pickup.");
                }
                pickupFound = true;
                layout.abilityRoom = room;
                layout.abilityPickup = {placement.hasPosition ? placement.x : object->spawnX(),
                                        placement.hasPosition ? placement.y : object->spawnY(),
                                        object->width(), object->height()};
                layout.archetypes.pickup = object;
                break;
            }
            case ObjectCategory::Prop: {
                if (layout.propCount == layout.props.size()) {
                    return {ErrorCode::CapacityExceeded, placement.object.data(),
                            "The world places at most 24 props."};
                }
                const framework::Facing facing =
                    placement.hasFacing ? placement.facing : object->facing();
                layout.props[layout.propCount++] = {
                    room,
                    {placement.hasPosition ? placement.x : object->spawnX(),
                     placement.hasPosition ? placement.y : object->spawnY()},
                    facing == framework::Facing::Left, object};
                break;
            }
            default:
                return invalid(placement.object.data(),
                               "Rooms may place enemies, doors, triggers, pickups and props.");
            }
            if (!status) return status;
        }
    }
    if (std::find(usedSlots.begin(), usedSlots.end(), false) != usedSlots.end() || !pickupFound) {
        return {ErrorCode::MissingDependency, "world",
                "The world must place five enemies and the Echo Step pickup."};
    }

    layout.archetypes.player =
        requiredObject(world, "echo:object/player", ObjectCategory::Player);
    layout.archetypes.engineer =
        requiredObject(world, "echo:object/engineer", ObjectCategory::Npc);
    layout.archetypes.projectile =
        requiredObject(world, "echo:object/projectile_player", ObjectCategory::Projectile);
    layout.archetypes.enemyProjectile =
        requiredObject(world, "echo:object/projectile_enemy", ObjectCategory::Projectile);
    layout.archetypes.loot = requiredObject(world, "echo:object/loot_crate", ObjectCategory::Loot);
    const WorldArchetypes& archetypes = layout.archetypes;
    if (archetypes.player == nullptr || archetypes.engineer == nullptr ||
        archetypes.projectile == nullptr || archetypes.enemyProjectile == nullptr ||
        archetypes.loot == nullptr) {
        return {ErrorCode::MissingDependency, "world.objects",
                "Player, engineer, projectile and loot objects are required."};
    }
    if (archetypes.player->width() != kLayoutPlayerWidth ||
        archetypes.player->height() != kLayoutPlayerHeight) {
        return invalid("echo:object/player", "Player collision bounds are fixed at 20x28.");
    }
    layout.startRoom = RoomId::LandingBay;
    layout.playerStart = {archetypes.player->spawnX(), archetypes.player->spawnY()};
    const Status status = validateWorldLayout(layout);
    if (!status) return status;
    output = layout;
    return Status::success();
}

} // namespace echo
