#pragma once

#include "echo/framework/attributes.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace echo::framework {

struct EquipmentAttributeModifier {
    std::string_view attributeId;
    std::int32_t amount{};
};

struct EquipmentDefinition {
    std::string_view id;
    std::string_view slot;
    std::span<const EquipmentAttributeModifier> modifiers;
};

class EquipmentModule final : public IModule {
public:
    [[nodiscard]] const ModuleInfo& info() const noexcept override;
    bool initialize(ModuleContext&) noexcept override;
    void shutdown() noexcept override;

    [[nodiscard]] const EquipmentDefinition* find(std::string_view id) const noexcept;
    [[nodiscard]] std::span<const EquipmentDefinition> definitions() const noexcept;
    [[nodiscard]] bool initialized() const noexcept { return initialized_; }

private:
    bool initialized_{};
};

template <std::size_t SlotCapacity>
class EquipmentLoadout {
    static_assert(SlotCapacity > 0);

public:
    template <std::size_t AttributeCapacity, std::size_t ModifierCapacity>
    [[nodiscard]] Status equip(
        const EquipmentModule& module, std::string_view id,
        AttributeSet<AttributeCapacity, ModifierCapacity>& attributes) noexcept {
        if (!module.initialized()) {
            return {ErrorCode::InvalidState, "echo.rpg.equipment",
                    "The equipment module must be initialized before equipping an item."};
        }
        const EquipmentDefinition* definition = module.find(id);
        if (definition == nullptr) {
            return {ErrorCode::NotFound, id, "Equipment definition is not registered."};
        }
        if (contains(id)) {
            return {ErrorCode::DuplicateId, id, "Equipment is already equipped."};
        }
        for (const Entry& entry : entries_) {
            if (entry.active && std::string_view(entry.slot.data()) == definition->slot) {
                return {ErrorCode::InvalidState, definition->slot,
                        "The equipment slot is already occupied."};
            }
        }
        const auto free = std::find_if(entries_.begin(), entries_.end(),
            [](const Entry& entry) { return !entry.active; });
        if (free == entries_.end()) {
            return {ErrorCode::CapacityExceeded, "equipment",
                    "Equipment slot capacity is full."};
        }
        if (id.size() >= free->id.size() ||
            definition->slot.size() >= free->slot.size()) {
            return {ErrorCode::InvalidValue, id, "Equipment ID or slot exceeds fixed storage."};
        }

        std::size_t applied = 0;
        for (const EquipmentAttributeModifier& modifier : definition->modifiers) {
            const Status status =
                attributes.addModifier(modifier.attributeId, definition->id, modifier.amount);
            if (!status) {
                while (applied > 0) {
                    --applied;
                    const Status rollback = attributes.removeModifier(
                        definition->modifiers[applied].attributeId, definition->id);
                    if (!rollback) return rollback;
                }
                return status;
            }
            ++applied;
        }
        std::copy(id.begin(), id.end(), free->id.begin());
        std::copy(definition->slot.begin(), definition->slot.end(), free->slot.begin());
        free->active = true;
        return Status::success();
    }

    template <std::size_t AttributeCapacity, std::size_t ModifierCapacity>
    [[nodiscard]] Status unequip(
        const EquipmentModule& module, std::string_view slot,
        AttributeSet<AttributeCapacity, ModifierCapacity>& attributes) noexcept {
        if (!module.initialized()) {
            return {ErrorCode::InvalidState, "echo.rpg.equipment",
                    "The equipment module must be initialized before unequipping an item."};
        }
        const auto found = std::find_if(entries_.begin(), entries_.end(),
            [slot](const Entry& entry) {
                return entry.active && std::string_view(entry.slot.data()) == slot;
            });
        if (found == entries_.end()) {
            return {ErrorCode::NotFound, slot, "No equipment is assigned to this slot."};
        }
        const std::string_view id(found->id.data());
        const EquipmentDefinition* definition = module.find(id);
        if (definition == nullptr) {
            return {ErrorCode::InvalidState, id, "Equipped definition is no longer registered."};
        }

        std::size_t removed = 0;
        for (const EquipmentAttributeModifier& modifier : definition->modifiers) {
            const Status status = attributes.removeModifier(modifier.attributeId, definition->id);
            if (!status) {
                while (removed > 0) {
                    --removed;
                    const EquipmentAttributeModifier& rollback =
                        definition->modifiers[removed];
                    const Status restore =
                        attributes.addModifier(rollback.attributeId, definition->id,
                                              rollback.amount);
                    if (!restore) return restore;
                }
                return status;
            }
            ++removed;
        }
        *found = {};
        return Status::success();
    }

    [[nodiscard]] bool contains(std::string_view id) const noexcept {
        for (const Entry& entry : entries_) {
            if (entry.active && std::string_view(entry.id.data()) == id) return true;
        }
        return false;
    }

    [[nodiscard]] bool occupies(std::string_view slot) const noexcept {
        for (const Entry& entry : entries_) {
            if (entry.active && std::string_view(entry.slot.data()) == slot) return true;
        }
        return false;
    }

    void resetAfterAttributesReset() noexcept { entries_.fill({}); }

private:
    struct Entry {
        std::array<char, 128> id{};
        std::array<char, 128> slot{};
        bool active{};
    };

    std::array<Entry, SlotCapacity> entries_{};
};

[[nodiscard]] CapabilityManifestFragment equipmentCapabilityManifestFragment() noexcept;

} // namespace echo::framework
