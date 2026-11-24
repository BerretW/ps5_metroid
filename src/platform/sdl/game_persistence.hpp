#pragma once

#include "echo/framework/data.hpp"
#include "echo/framework/power.hpp"
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
    "inventory.item.8", "inventory.item.9", "inventory.item.10", "inventory.item.11",
    "inventory.item.12", "inventory.item.13", "inventory.item.14", "inventory.item.15",
    "inventory.item.16", "inventory.item.17", "inventory.item.18", "inventory.item.19",
}};
inline constexpr std::array<std::string_view, Inventory::kCapacity> kInventoryQuantityKeys{{
    "inventory.quantity.0", "inventory.quantity.1", "inventory.quantity.2",
    "inventory.quantity.3", "inventory.quantity.4", "inventory.quantity.5",
    "inventory.quantity.6", "inventory.quantity.7",
    "inventory.quantity.8", "inventory.quantity.9", "inventory.quantity.10",
    "inventory.quantity.11", "inventory.quantity.12", "inventory.quantity.13",
    "inventory.quantity.14", "inventory.quantity.15", "inventory.quantity.16",
    "inventory.quantity.17", "inventory.quantity.18", "inventory.quantity.19",
}};
inline constexpr std::array<std::string_view, kEquipmentSlotCount> kEquipmentKeys{
    "equipment.head", "equipment.body", "equipment.weapon", "equipment.accessory"};
inline constexpr std::array<std::string_view, kEquipmentSlotCount> kEquipmentQuantityKeys{
    "equipment.head.quantity", "equipment.body.quantity", "equipment.weapon.quantity",
    "equipment.accessory.quantity"};
inline constexpr std::array<std::string_view, 2> kLootActiveKeys{
    "loot.0.active", "loot.1.active"};
inline constexpr std::array<std::string_view, 2> kLootRoomKeys{
    "loot.0.room", "loot.1.room"};
inline constexpr std::array<std::string_view, 2> kLootItemKeys{
    "loot.0.item", "loot.1.item"};
inline constexpr std::array<std::string_view, 2> kLootQuantityKeys{
    "loot.0.quantity", "loot.1.quantity"};
inline constexpr std::array<std::string_view, 2> kLootXKeys{"loot.0.x", "loot.1.x"};
inline constexpr std::array<std::string_view, 2> kLootYKeys{"loot.0.y", "loot.1.y"};
inline constexpr std::array<std::string_view, kEnemyCount> kEnemyKeys{
    "enemy.0.health", "enemy.1.health", "enemy.2.health", "enemy.3.health", "enemy.4.health"};
inline constexpr std::array<std::string_view, 10> kExtraLootActiveKeys{
    "loot.2.active", "loot.3.active", "loot.4.active", "loot.5.active", "loot.6.active",
    "loot.7.active", "loot.8.active", "loot.9.active", "loot.10.active", "loot.11.active"};
inline constexpr std::array<std::string_view, 10> kExtraLootXKeys{
    "loot.2.x", "loot.3.x", "loot.4.x", "loot.5.x", "loot.6.x",
    "loot.7.x", "loot.8.x", "loot.9.x", "loot.10.x", "loot.11.x"};

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
    if (const Status next = setInteger("quest.branch", checkpoint.questBranch); !next) return next;
    if (const Status next = setBoolean("quest.reward_claimed",
                                       checkpoint.questRewardClaimed); !next) return next;
    if (const Status next = setInteger("game.deaths", checkpoint.deaths); !next) return next;
    if (checkpoint.firedTriggers != 0) {
        if (const Status next = setInteger("world.triggers_fired",
                                           checkpoint.firedTriggers); !next) return next;
    }
    if (const Status next = setInteger("inventory.count",
                                       static_cast<std::int64_t>(checkpoint.inventory.count)); !next) return next;
    if (const Status next = setInteger("inventory.layout", 2); !next) return next;
    for (std::size_t index = 0; index < Inventory::kCapacity; ++index) {
        if (const Status next = setInteger(
                game_persistence_detail::kInventoryKeys[index],
                static_cast<std::int64_t>(checkpoint.inventory.items[index])); !next) return next;
        if (const Status next = setInteger(
                game_persistence_detail::kInventoryQuantityKeys[index],
                checkpoint.inventory.quantities[index]); !next) return next;
    }
    for (std::size_t index = 0; index < kEquipmentSlotCount; ++index) {
        if (const Status next = setInteger(
                game_persistence_detail::kEquipmentKeys[index],
                static_cast<std::int64_t>(checkpoint.equipment[index].item)); !next) return next;
        if (const Status next = setInteger(
                game_persistence_detail::kEquipmentQuantityKeys[index],
                checkpoint.equipment[index].quantity); !next) return next;
    }
    if (const Status next = setInteger("inventory.active",
            static_cast<std::int64_t>(checkpoint.activeItem.item)); !next) return next;
    if (const Status next = setInteger("inventory.active.quantity",
            checkpoint.activeItem.quantity); !next) return next;
    for (std::size_t index = 0; index < checkpoint.enemyHealth.size(); ++index) {
        if (const Status next = setInteger(game_persistence_detail::kEnemyKeys[index],
                                           checkpoint.enemyHealth[index]); !next) return next;
    }
    for (std::size_t index = 0; index < 2; ++index) {
        const LootDrop& drop = checkpoint.lootDrops[index];
        if (const Status next = setBoolean(
                game_persistence_detail::kLootActiveKeys[index], drop.active); !next) return next;
        if (const Status next = setInteger(
                game_persistence_detail::kLootRoomKeys[index],
                static_cast<std::int64_t>(drop.room)); !next) return next;
        if (const Status next = setInteger(
                game_persistence_detail::kLootItemKeys[index],
                static_cast<std::int64_t>(drop.stack.item)); !next) return next;
        if (const Status next = setInteger(
                game_persistence_detail::kLootQuantityKeys[index], drop.stack.quantity); !next) return next;
        if (const Status next = setNumber(
                game_persistence_detail::kLootXKeys[index], drop.position.x); !next) return next;
        if (const Status next = setNumber(
                game_persistence_detail::kLootYKeys[index], drop.position.y); !next) return next;
    }
    for (std::size_t index = 2; index < checkpoint.lootDrops.size(); ++index) {
        const LootDrop& drop = checkpoint.lootDrops[index];
        if (const Status next = setBoolean(
                game_persistence_detail::kExtraLootActiveKeys[index - 2], drop.active); !next) return next;
        if (const Status next = setNumber(
                game_persistence_detail::kExtraLootXKeys[index - 2], drop.position.x); !next) return next;
    }
    if (const Status next = setNumber("effect.speed", checkpoint.speedBoostSeconds); !next) return next;
    if (const Status next = setNumber("effect.jump", checkpoint.jumpBoostSeconds); !next) return next;
    if (const Status next = setNumber("effect.shield", checkpoint.shieldSeconds); !next) return next;
    if (const Status next = setInteger("effect.speed.amount",
                                       checkpoint.speedBoostAmount); !next) return next;
    if (const Status next = setInteger("effect.jump.amount",
                                       checkpoint.jumpBoostAmount); !next) return next;
    return output.replaceValues(candidate);
}

inline framework::Status decodeGameCheckpoint(
    const framework::GameState& state, GameCheckpoint& output,
    const Game* lootContext = nullptr) noexcept {
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
    std::int64_t questBranch{};
    std::int64_t speedBoostAmount{};
    std::int64_t jumpBoostAmount{};
    StateValue layoutValue{};
    const bool hasLayout = static_cast<bool>(get("inventory.layout", layoutValue));
    if (hasLayout &&
        (layoutValue.type != ValueType::Integer || layoutValue.integer != 2)) {
        return {ErrorCode::CorruptData, "inventory.layout", "Unsupported inventory layout."};
    }
    bool echoStepUnlocked{};
    if (!getInteger("save.room", room) || room < 0 ||
        room >= static_cast<std::int64_t>(RoomId::Count) ||
        !getInteger("player.health", health) || health < 1 || health > 8 ||
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
        inventoryCount > static_cast<std::int64_t>(hasLayout ? Inventory::kCapacity : 8) ||
        !getBoolean("game.completed", candidate.completed) ||
        !getInteger("quest.branch", questBranch) || questBranch < 0 || questBranch > 2 ||
        !getBoolean("quest.reward_claimed", candidate.questRewardClaimed)) {
        return {ErrorCode::CorruptData, "autosave",
                "Autosave is missing required gameplay state or has invalid field types."};
    }
    candidate.room = static_cast<RoomId>(room);
    candidate.playerHealth = static_cast<int>(health);
    candidate.playerFacing = static_cast<int>(facing);
    candidate.deaths = static_cast<int>(deaths);
    candidate.questBranch = static_cast<std::uint8_t>(questBranch);
    // Optional so saves written before door triggers existed still load.
    std::int64_t firedTriggers{};
    StateValue firedValue{};
    if (get("world.triggers_fired", firedValue)) {
        if (firedValue.type != ValueType::Integer || firedValue.integer < 0 ||
            firedValue.integer > 255) {
            return {ErrorCode::CorruptData, "world.triggers_fired",
                    "Autosave trigger state is invalid."};
        }
        firedTriggers = firedValue.integer;
    }
    candidate.firedTriggers = static_cast<std::uint8_t>(firedTriggers);
    if (candidate.questRewardClaimed &&
        (candidate.questBranch == 0 || !candidate.completed)) {
        return {ErrorCode::CorruptData, "quest.reward_claimed",
                "A claimed quest reward requires a completed selected route."};
    }
    for (std::size_t index = 0; index < candidate.enemyHealth.size(); ++index) {
        const std::string_view key = game_persistence_detail::kEnemyKeys[index];
        constexpr std::int64_t kMaximumEnemyHealth = 99;
        std::int64_t value{};
        if (!getInteger(key, value) || value < 0 || value > kMaximumEnemyHealth) {
            return {ErrorCode::CorruptData, key, "Autosave enemy health is invalid."};
        }
        candidate.enemyHealth[index] = static_cast<int>(value);
    }
    // Game::restore checks loot against the actual room width.
    constexpr float kMaxLootX = static_cast<float>(kMaxLayoutColumns * kTileSize) - 16.0F;
    for (std::size_t index = 0; index < 2; ++index) {
        bool active{};
        std::int64_t lootRoomValue{};
        std::int64_t item{};
        std::int64_t quantity{};
        float x{};
        float y{};
        if (!getBoolean(game_persistence_detail::kLootActiveKeys[index], active) ||
            !getInteger(game_persistence_detail::kLootRoomKeys[index], lootRoomValue) ||
            lootRoomValue < 0 || lootRoomValue >= static_cast<std::int64_t>(RoomId::Count) ||
            !getInteger(game_persistence_detail::kLootItemKeys[index], item) ||
            item < 0 || item >= static_cast<std::int64_t>(ItemId::Count) ||
            !getInteger(game_persistence_detail::kLootQuantityKeys[index], quantity) ||
            quantity < 0 || quantity > std::numeric_limits<std::uint16_t>::max() ||
            !getNumber(game_persistence_detail::kLootXKeys[index], x) ||
            !getNumber(game_persistence_detail::kLootYKeys[index], y)) {
            return {ErrorCode::CorruptData, "loot", "Autosave loot drop fields are invalid."};
        }
        const ItemStack stack{static_cast<ItemId>(item), static_cast<std::uint16_t>(quantity)};
        if ((active && (stack.item == ItemId::None || stack.quantity == 0)) ||
            (!active && stack != ItemStack{}) ||
            x < 0.0F || x > kMaxLootX ||
            y < 0.0F || y > static_cast<float>(kRoomHeight) - 16.0F) {
            return {ErrorCode::CorruptData, "loot", "Autosave loot drop is inconsistent."};
        }
        candidate.lootDrops[index] = {
            static_cast<RoomId>(lootRoomValue), {x, y}, stack, active};
    }
    for (std::size_t index = 2; index < candidate.lootDrops.size(); ++index) {
        bool active{};
        float x{};
        if (!getBoolean(game_persistence_detail::kExtraLootActiveKeys[index - 2], active) ||
            !getNumber(game_persistence_detail::kExtraLootXKeys[index - 2], x) ||
            x < 0.0F || x > kMaxLootX ||
            (active && lootContext == nullptr)) {
            return {ErrorCode::CorruptData, "loot", "Autosave loot drop is inconsistent."};
        }
        if (active) {
            const std::size_t enemySlot = lootContext->lootEnemy(index);
            const ItemStack expected = lootContext->expectedLoot(index);
            const RoomId expectedRoom = lootContext->expectedLootRoom(index);
            if (enemySlot >= candidate.enemyHealth.size() ||
                candidate.enemyHealth[enemySlot] != 0 ||
                expected.item == ItemId::None || expected.quantity == 0 ||
                expectedRoom >= RoomId::Count) {
                return {ErrorCode::CorruptData, "loot",
                        "Autosave loot drop is inconsistent with enemy progress."};
            }
            candidate.lootDrops[index] = {
                expectedRoom, {x, 304.0F}, expected, true};
        }
    }
    if (!getNumber("effect.speed", candidate.speedBoostSeconds) ||
        !getNumber("effect.jump", candidate.jumpBoostSeconds) ||
        !getNumber("effect.shield", candidate.shieldSeconds) ||
        !getInteger("effect.speed.amount", speedBoostAmount) ||
        !getInteger("effect.jump.amount", jumpBoostAmount) ||
        candidate.speedBoostSeconds < 0 || candidate.speedBoostSeconds > 8 ||
        candidate.jumpBoostSeconds < 0 || candidate.jumpBoostSeconds > 8 ||
        candidate.shieldSeconds < 0 || candidate.shieldSeconds > 4 ||
        speedBoostAmount < 0 || speedBoostAmount > 1000 ||
        jumpBoostAmount < 0 || jumpBoostAmount > 1000 ||
        (candidate.speedBoostSeconds > 0 && speedBoostAmount == 0) ||
        (candidate.jumpBoostSeconds > 0 && jumpBoostAmount == 0)) {
        return {ErrorCode::CorruptData, "effect", "Autosave effect duration is invalid."};
    }
    candidate.speedBoostAmount = static_cast<std::int32_t>(speedBoostAmount);
    candidate.jumpBoostAmount = static_cast<std::int32_t>(jumpBoostAmount);
    candidate.inventoryLayoutVersion = hasLayout ? 2 : 1;
    if (!hasLayout) {
        for (std::size_t slot = 8; slot < Inventory::kCapacity; ++slot) {
            StateValue unexpected{};
            if (get(game_persistence_detail::kInventoryKeys[slot], unexpected) ||
                get(game_persistence_detail::kInventoryQuantityKeys[slot], unexpected)) {
                return {ErrorCode::CorruptData, "inventory.layout",
                        "Expanded backpack slots require an inventory layout version."};
            }
        }
    }
    const std::size_t slotsToRead = hasLayout ? Inventory::kCapacity :
        static_cast<std::size_t>(inventoryCount);
    for (std::size_t index = 0; index < slotsToRead; ++index) {
        const std::string_view key = game_persistence_detail::kInventoryKeys[index];
        std::int64_t value{};
        if (!getInteger(key, value) || value < (hasLayout ? 0 : 1) ||
            value >= static_cast<std::int64_t>(ItemId::Count)) {
            return {ErrorCode::CorruptData, key,
                    "Autosave inventory item is invalid or duplicated."};
        }
        std::uint16_t quantity = 1;
        StateValue quantityValue{};
        const bool hasQuantity = static_cast<bool>(
            get(game_persistence_detail::kInventoryQuantityKeys[index], quantityValue));
        if (hasLayout && !hasQuantity) {
            return {ErrorCode::CorruptData, key, "Inventory slot quantity is missing."};
        }
        if (hasQuantity) {
            if (quantityValue.type != ValueType::Integer || quantityValue.integer < 0 ||
                quantityValue.integer > std::numeric_limits<std::uint16_t>::max()) {
                return {ErrorCode::CorruptData,
                        game_persistence_detail::kInventoryQuantityKeys[index],
                        "Autosave inventory quantity is invalid."};
            }
            quantity = static_cast<std::uint16_t>(quantityValue.integer);
        }
        candidate.inventory.items[index] = static_cast<ItemId>(value);
        candidate.inventory.quantities[index] = quantity;
        if (!Game::validItemStackShape(index, {static_cast<ItemId>(value), quantity})) {
            return {ErrorCode::CorruptData, key, "Inventory stack is invalid."};
        }
        if (value != 0) ++candidate.inventory.count;
    }
    if (!candidate.inventory.valid() ||
        candidate.inventory.count != static_cast<std::size_t>(inventoryCount)) {
        return {ErrorCode::CorruptData, "inventory",
                "Autosave inventory slots are malformed."};
    }
    if (candidate.questBranch != 0 &&
        !candidate.inventory.contains(ItemId::MaintenanceKey)) {
        return {ErrorCode::CorruptData, "quest.branch",
                "A selected quest route requires the maintenance key."};
    }
    for (std::size_t index = 0; index < kEquipmentSlotCount; ++index) {
        std::int64_t item{};
        std::int64_t quantity{};
        if (hasLayout) {
            if (!getInteger(game_persistence_detail::kEquipmentKeys[index], item) ||
                !getInteger(game_persistence_detail::kEquipmentQuantityKeys[index], quantity) ||
                item < 0 || item >= static_cast<std::int64_t>(ItemId::Count) ||
                quantity < 0 || quantity > 3 ||
                !Game::validItemStackShape(Game::kFirstEquipmentSlot + index,
                                           {static_cast<ItemId>(item),
                                            static_cast<std::uint16_t>(quantity)})) {
                return {ErrorCode::CorruptData, "equipment", "Equipment slot is invalid."};
            }
            candidate.equipment[index] =
                {static_cast<ItemId>(item), static_cast<std::uint16_t>(quantity)};
        } else {
            StateValue unexpected{};
            if (get(game_persistence_detail::kEquipmentKeys[index], unexpected) ||
                get(game_persistence_detail::kEquipmentQuantityKeys[index], unexpected)) {
                return {ErrorCode::CorruptData, "inventory.layout",
                        "Equipment slots require an inventory layout version."};
            }
        }
    }
    if (hasLayout) {
        std::int64_t item{};
        std::int64_t quantity{};
        if (!getInteger("inventory.active", item) ||
            !getInteger("inventory.active.quantity", quantity) ||
            item < 0 || item >= static_cast<std::int64_t>(ItemId::Count) ||
            quantity < 0 || quantity > 3 ||
            !Game::validItemStackShape(Game::kActiveItemSlot,
                                       {static_cast<ItemId>(item),
                                        static_cast<std::uint16_t>(quantity)})) {
            return {ErrorCode::CorruptData, "inventory.active", "Active item slot is invalid."};
        }
        candidate.activeItem =
            {static_cast<ItemId>(item), static_cast<std::uint16_t>(quantity)};
    } else {
        StateValue unexpected{};
        if (get("inventory.active", unexpected) ||
            get("inventory.active.quantity", unexpected)) {
            return {ErrorCode::CorruptData, "inventory.layout",
                    "An active slot requires an inventory layout version."};
        }
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
                                      void* context) noexcept {
    using namespace framework;
    if (fromVersion < 1 || fromVersion > 7) return false;
    if (fromVersion == 1) {
        const Status countStatus = candidate.set(
            {StateScope::Global, {}, "inventory.count"}, StateValue::fromInteger(1));
        if (!countStatus) return false;
        const Status itemStatus = candidate.set(
            {StateScope::Global, {}, game_persistence_detail::kInventoryKeys[0]},
            StateValue::fromInteger(static_cast<std::int64_t>(ItemId::MaintenanceKey)));
        if (!itemStatus) return false;
    }
    for (std::size_t index = 0; fromVersion < 4 && index < 2; ++index) {
        if (!candidate.set(
                {StateScope::Global, {}, game_persistence_detail::kLootActiveKeys[index]},
                StateValue::fromBoolean(false)) ||
            !candidate.set(
                {StateScope::Global, {}, game_persistence_detail::kLootRoomKeys[index]},
                StateValue::fromInteger(static_cast<std::int64_t>(
                    index == 0 ? RoomId::MaintenanceTunnel : RoomId::PowerStation))) ||
            !candidate.set(
                {StateScope::Global, {}, game_persistence_detail::kLootItemKeys[index]},
                StateValue::fromInteger(0)) ||
            !candidate.set(
                {StateScope::Global, {}, game_persistence_detail::kLootQuantityKeys[index]},
                StateValue::fromInteger(0)) ||
            !candidate.set(
                {StateScope::Global, {}, game_persistence_detail::kLootXKeys[index]},
                StateValue::fromNumber(0.0)) ||
            !candidate.set(
                {StateScope::Global, {}, game_persistence_detail::kLootYKeys[index]},
                StateValue::fromNumber(0.0))) {
            return false;
        }
    }
    if (fromVersion < 5) {
        constexpr std::array<int, 3> newHealth{3, 6, 4};
        for (std::size_t index = 2; index < kEnemyCount; ++index) {
            if (!candidate.set({StateScope::Global, {}, game_persistence_detail::kEnemyKeys[index]},
                               StateValue::fromInteger(newHealth[index - 2]))) return false;
        }
        for (std::size_t index = 0; index < 10; ++index) {
            if (!candidate.set({StateScope::Global, {}, game_persistence_detail::kExtraLootActiveKeys[index]},
                               StateValue::fromBoolean(false)) ||
                !candidate.set({StateScope::Global, {}, game_persistence_detail::kExtraLootXKeys[index]},
                               StateValue::fromNumber(0))) return false;
        }
        for (std::string_view key : {"effect.speed", "effect.jump", "effect.shield"}) {
            if (!candidate.set({StateScope::Global, {}, key}, StateValue::fromNumber(0))) return false;
        }
    }
    if (fromVersion < 8) {
        if (!candidate.set(
                {StateScope::Global, {}, "effect.speed.amount"},
                StateValue::fromInteger(25)) ||
            !candidate.set(
                {StateScope::Global, {}, "effect.jump.amount"},
                StateValue::fromInteger(60))) {
            return false;
        }
    }
    if (!candidate.set({StateScope::Global, {}, "quest.branch"},
                       StateValue::fromInteger(0)) ||
        !candidate.set({StateScope::Global, {}, "quest.reward_claimed"},
                       StateValue::fromBoolean(false))) return false;
    if (context != nullptr) {
        const Status powerStatus =
            static_cast<PowerModule*>(context)->saveState(candidate, true);
        if (!powerStatus) return false;
    }
    return true;
}

} // namespace echo::platform
