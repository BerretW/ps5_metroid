#pragma once

#include "echo/framework/capability_manifest.hpp"
#include "echo/framework/data.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace echo::framework {

struct ItemDefinition {
    std::uint32_t key{};
    std::array<char, 128> id{};
    std::array<char, 64> displayName{};
    std::uint16_t maxStack{};
};

class ItemCatalog {
public:
    static constexpr std::size_t kMaxItems = 32;

    [[nodiscard]] Status load(JsonValue document) noexcept;
    [[nodiscard]] const ItemDefinition* find(std::string_view id) const noexcept;
    [[nodiscard]] const ItemDefinition* find(std::uint32_t key) const noexcept;
    [[nodiscard]] const ItemDefinition* at(std::size_t index) const noexcept {
        return index < count_ ? &items_[index] : nullptr;
    }
    [[nodiscard]] std::size_t size() const noexcept { return count_; }
    void clear() noexcept;

private:
    std::array<ItemDefinition, kMaxItems> items_{};
    std::size_t count_{};
};

template <typename ItemId, std::size_t Capacity>
class FixedInventory {
    static_assert(Capacity > 0);

public:
    static constexpr std::size_t kCapacity = Capacity;

    [[nodiscard]] Status add(ItemId item, std::uint16_t maxStack = 1,
                             std::uint16_t amount = 1) noexcept {
        if (count > Capacity) {
            return {ErrorCode::InvalidState, "inventory",
                    "Inventory slot count exceeds its fixed capacity."};
        }
        if (item == ItemId{} || maxStack == 0 || amount == 0) {
            return {ErrorCode::InvalidValue, "inventory.item",
                    "Item ID, stack limit, and amount must be non-zero."};
        }

        std::uint32_t room = 0;
        for (std::size_t index = 0; index < count; ++index) {
            if (items[index] == item) {
                if (quantities[index] > maxStack) {
                    return {ErrorCode::InvalidState, "inventory.item",
                            "Existing item quantity exceeds its current stack limit."};
                }
                room += static_cast<std::uint32_t>(maxStack - quantities[index]);
            }
        }
        const std::uint32_t freeSlots = static_cast<std::uint32_t>(Capacity - count);
        const std::uint32_t available = room +
            freeSlots * static_cast<std::uint32_t>(maxStack);
        if (amount > available) {
            return {ErrorCode::CapacityExceeded, "inventory",
                    "Inventory has insufficient stack capacity for the complete grant."};
        }

        std::uint32_t remaining = amount;
        for (std::size_t index = 0; index < count && remaining != 0; ++index) {
            if (items[index] != item) continue;
            const std::uint16_t added = static_cast<std::uint16_t>(
                std::min<std::uint32_t>(remaining, maxStack - quantities[index]));
            quantities[index] = static_cast<std::uint16_t>(quantities[index] + added);
            remaining -= added;
        }
        while (remaining != 0) {
            const std::uint16_t added =
                static_cast<std::uint16_t>(std::min<std::uint32_t>(remaining, maxStack));
            items[count] = item;
            quantities[count] = added;
            ++count;
            remaining -= added;
        }
        return Status::success();
    }

    [[nodiscard]] Status remove(ItemId item, std::uint16_t amount = 1) noexcept {
        if (count > Capacity) {
            return {ErrorCode::InvalidState, "inventory",
                    "Inventory slot count exceeds its fixed capacity."};
        }
        if (item == ItemId{} || amount == 0) {
            return {ErrorCode::InvalidValue, "inventory.item",
                    "Item ID and removal amount must be non-zero."};
        }
        if (quantity(item) < amount) {
            return {ErrorCode::NotFound, "inventory.item",
                    "Inventory does not contain the requested quantity."};
        }
        std::uint32_t remaining = amount;
        for (std::size_t index = 0; index < count && remaining != 0;) {
            if (items[index] != item) {
                ++index;
                continue;
            }
            const std::uint16_t removed = static_cast<std::uint16_t>(
                std::min<std::uint32_t>(remaining, quantities[index]));
            quantities[index] = static_cast<std::uint16_t>(quantities[index] - removed);
            remaining -= removed;
            if (quantities[index] == 0) {
                erase(index);
            } else {
                ++index;
            }
        }
        return Status::success();
    }

    [[nodiscard]] bool contains(ItemId item) const noexcept {
        return quantity(item) != 0;
    }

    [[nodiscard]] std::uint32_t quantity(ItemId item) const noexcept {
        if (count > Capacity) return 0;
        std::uint32_t total = 0;
        for (std::size_t index = 0; index < count; ++index) {
            if (items[index] == item) total += quantities[index];
        }
        return total;
    }

    [[nodiscard]] std::size_t size() const noexcept { return count; }

    void clear() noexcept {
        items.fill(ItemId{});
        quantities.fill(0);
        count = 0;
    }

    [[nodiscard]] bool valid() const noexcept {
        if (count > Capacity) return false;
        for (std::size_t index = 0; index < count; ++index) {
            if (items[index] == ItemId{} || quantities[index] == 0) return false;
        }
        for (std::size_t index = count; index < Capacity; ++index) {
            if (items[index] != ItemId{} || quantities[index] != 0) return false;
        }
        return true;
    }

    std::array<ItemId, Capacity> items{};
    std::array<std::uint16_t, Capacity> quantities{};
    std::size_t count{};

private:
    void erase(std::size_t index) noexcept {
        for (std::size_t move = index + 1; move < count; ++move) {
            items[move - 1] = items[move];
            quantities[move - 1] = quantities[move];
        }
        --count;
        items[count] = {};
        quantities[count] = 0;
    }
};

class InventoryModule final : public IModule {
public:
    [[nodiscard]] Status load(JsonValue itemDocument) noexcept;
    [[nodiscard]] const ModuleInfo& info() const noexcept override;
    bool initialize(ModuleContext&) noexcept override;
    void shutdown() noexcept override;

    [[nodiscard]] const ItemCatalog& catalog() const noexcept { return catalog_; }
    [[nodiscard]] bool initialized() const noexcept { return initialized_; }

private:
    ItemCatalog catalog_{};
    bool loaded_{};
    bool initialized_{};
};

[[nodiscard]] CapabilityManifestFragment inventoryCapabilityManifestFragment() noexcept;

} // namespace echo::framework
