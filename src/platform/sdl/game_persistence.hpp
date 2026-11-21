#pragma once

#include "echo/framework/data.hpp"
#include "game/game.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string_view>

namespace echo::platform {

namespace game_persistence_detail {

inline constexpr std::array<std::string_view, Inventory::kCapacity> kInventoryKeys{{
    "inventory.item.0", "inventory.item.1", "inventory.item.2", "inventory.item.3",
    "inventory.item.4", "inventory.item.5", "inventory.item.6", "inventory.item.7",
}};
inline constexpr std::array<std::string_view, Inventory::kCapacity> kInventoryQuantityKeys{{
    "inventory.quantity.0", "inventory.quantity.1", "inventory.quantity.2",
    "inventory.quantity.3", "inventory.quantity.4", "inventory.quantity.5",
    "inventory.quantity.6", "inventory.quantity.7",
}};

} // namespace game_persistence_detail

inline framework::Status encodeGameCheckpoint(const Game& game,
                                              framework::GameState& output) noexcept {
    using namespace framework;
    const GameCheckpoint checkpoint = game.checkpoint();
    GameState candidate{};
    const auto setInteger = [&candidate](std::string_view key, std::int64_t value) {
        return candidate.set({StateScope::Global, {}, key}, StateValue::fromInteger(value));
    };
    const auto setNumber = [&candidate](std::string_view key, double value) {
        return candidate.set({StateScope::Global, {}, key}, StateValue::fromNumber(value));
    };
    const auto setBoolean = [&candidate](std::string_view key, bool value) {
        return candidate.set({StateScope::Global, {}, key}, StateValue::fromBoolean(value));
    };
    const Status status = setInteger("save.room", static_cast<std::int64_t>(checkpoint.room));
    if (!status) return status;
    if (const Status next = setNumber("player.x", checkpoint.playerPosition.x); !next) return next;
    if (const Status next = setNumber("player.y", checkpoint.playerPosition.y); !next) return next;
    if (const Status next = setNumber("player.vx", checkpoint.playerVelocity.x); !next) return next;
    if (const Status next = setNumber("player.vy", checkpoint.playerVelocity.y); !next) return next;
    if (const Status next = setInteger("player.health", checkpoint.playerHealth); !next) return next;
    if (const Status next = setInteger("player.facing", checkpoint.playerFacing); !next) return next;
    if (const Status next = setBoolean("player.on_ground", checkpoint.playerOnGround); !next) return next;
    if (const Status next = setBoolean("player.double_jump", checkpoint.hasDoubleJump); !next) return next;
    if (const Status next = setBoolean("player.ability_collected", checkpoint.abilityCollected);
        !next) return next;
    if (const Status next = setBoolean("player.echo_step_unlocked", checkpoint.abilityCollected);
        !next) return next;
    if (const Status next = setBoolean("game.completed", checkpoint.completed); !next) return next;
    if (const Status next = setInteger("game.deaths", checkpoint.deaths); !next) return next;
    if (const Status next = setInteger("inventory.count",
                                       static_cast<std::int64_t>(checkpoint.inventory.count)); !next) return next;
    for (std::size_t index = 0; index < checkpoint.inventory.count; ++index) {
        if (const Status next = setInteger(
                game_persistence_detail::kInventoryKeys[index],
                static_cast<std::int64_t>(checkpoint.inventory.items[index])); !next) return next;
        if (const Status next = setInteger(
                game_persistence_detail::kInventoryQuantityKeys[index],
                checkpoint.inventory.quantities[index]); !next) return next;
    }
    for (std::size_t index = 0; index < checkpoint.enemyHealth.size(); ++index) {
        const char* const key = index == 0 ? "enemy.0.health" : "enemy.1.health";
        if (const Status next = setInteger(key, checkpoint.enemyHealth[index]); !next) return next;
    }
    return output.replaceValues(candidate);
}

inline framework::Status decodeGameCheckpoint(const framework::GameState& state,
                                              GameCheckpoint& output) noexcept {
    using namespace framework;
    const auto get = [&state](std::string_view key, StateValue& value) {
        return state.get({StateScope::Global, {}, key}, value);
    };
    const auto getInteger = [&get](std::string_view key, std::int64_t& value) {
        StateValue stateValue{};
        if (!get(key, stateValue) || stateValue.type != ValueType::Integer) return false;
        value = stateValue.integer;
        return true;
    };
    const auto getNumber = [&get](std::string_view key, float& value) {
        StateValue stateValue{};
        if (!get(key, stateValue) || stateValue.type != ValueType::Number ||
            !std::isfinite(stateValue.number) ||
            std::abs(stateValue.number) > static_cast<double>(std::numeric_limits<float>::max())) {
            return false;
        }
        value = static_cast<float>(stateValue.number);
        return true;
    };
    const auto getBoolean = [&get](std::string_view key, bool& value) {
        StateValue stateValue{};
        if (!get(key, stateValue) || stateValue.type != ValueType::Boolean) return false;
        value = stateValue.boolean;
        return true;
    };

    GameCheckpoint candidate{};
    std::int64_t room{};
    std::int64_t health{};
    std::int64_t facing{};
    std::int64_t deaths{};
    std::int64_t inventoryCount{};
    bool echoStepUnlocked{};
    if (!getInteger("save.room", room) || room < 0 ||
        room >= static_cast<std::int64_t>(RoomId::Count) ||
        !getInteger("player.health", health) || health < 1 || health > 5 ||
        !getInteger("player.facing", facing) || (facing != -1 && facing != 1) ||
        !getInteger("game.deaths", deaths) || deaths < 0 ||
        deaths > std::numeric_limits<int>::max() ||
        !getNumber("player.x", candidate.playerPosition.x) ||
        !getNumber("player.y", candidate.playerPosition.y) ||
        !getNumber("player.vx", candidate.playerVelocity.x) ||
        !getNumber("player.vy", candidate.playerVelocity.y) ||
        !getBoolean("player.on_ground", candidate.playerOnGround) ||
        !getBoolean("player.double_jump", candidate.hasDoubleJump) ||
        !getBoolean("player.ability_collected", candidate.abilityCollected) ||
        !getBoolean("player.echo_step_unlocked", echoStepUnlocked) ||
        !getInteger("inventory.count", inventoryCount) || inventoryCount < 0 ||
        inventoryCount > static_cast<std::int64_t>(Inventory::kCapacity) ||
        !getBoolean("game.completed", candidate.completed)) {
        return {ErrorCode::CorruptData, "autosave",
                "Autosave is missing required gameplay state or has invalid field types."};
    }
    candidate.room = static_cast<RoomId>(room);
    candidate.playerHealth = static_cast<int>(health);
    candidate.playerFacing = static_cast<int>(facing);
    candidate.deaths = static_cast<int>(deaths);
    for (std::size_t index = 0; index < candidate.enemyHealth.size(); ++index) {
        const char* const key = index == 0 ? "enemy.0.health" : "enemy.1.health";
        std::int64_t value{};
        if (!getInteger(key, value) || value < 0 || value > (index == 0 ? 2 : 3)) {
            return {ErrorCode::CorruptData, key, "Autosave enemy health is invalid."};
        }
        candidate.enemyHealth[index] = static_cast<int>(value);
    }
    for (std::size_t index = 0; index < static_cast<std::size_t>(inventoryCount); ++index) {
        const std::string_view key = game_persistence_detail::kInventoryKeys[index];
        std::int64_t value{};
        if (!getInteger(key, value) || value <= 0 ||
            value >= static_cast<std::int64_t>(ItemId::Count)) {
            return {ErrorCode::CorruptData, key,
                    "Autosave inventory item is invalid or duplicated."};
        }
        std::uint16_t quantity = 1;
        StateValue quantityValue{};
        if (get(game_persistence_detail::kInventoryQuantityKeys[index], quantityValue)) {
            if (quantityValue.type != ValueType::Integer || quantityValue.integer <= 0 ||
                quantityValue.integer > std::numeric_limits<std::uint16_t>::max()) {
                return {ErrorCode::CorruptData,
                        game_persistence_detail::kInventoryQuantityKeys[index],
                        "Autosave inventory quantity is invalid."};
            }
            quantity = static_cast<std::uint16_t>(quantityValue.integer);
        }
        candidate.inventory.items[index] = static_cast<ItemId>(value);
        candidate.inventory.quantities[index] = quantity;
        ++candidate.inventory.count;
    }
    if (!candidate.inventory.valid()) {
        return {ErrorCode::CorruptData, "inventory",
                "Autosave inventory slots are malformed."};
    }
    if (candidate.hasDoubleJump != candidate.abilityCollected ||
        candidate.abilityCollected != echoStepUnlocked) {
        return {ErrorCode::CorruptData, "autosave", "Autosave ability fields disagree."};
    }
    output = candidate;
    return Status::success();
}

inline bool migrateGameCheckpointSave(std::uint32_t fromVersion,
                                      framework::GameState& candidate,
                                      void*) noexcept {
    using namespace framework;
    if (fromVersion != 1) {
        return false;
    }
    const Status countStatus = candidate.set(
        {StateScope::Global, {}, "inventory.count"}, StateValue::fromInteger(1));
    if (!countStatus) {
        return false;
    }
    return static_cast<bool>(candidate.set(
        {StateScope::Global, {}, game_persistence_detail::kInventoryKeys[0]},
        StateValue::fromInteger(static_cast<std::int64_t>(ItemId::MaintenanceKey))));
}

} // namespace echo::platform
