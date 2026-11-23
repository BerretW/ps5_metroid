#pragma once

#include "echo/framework/capability_manifest.hpp"
#include "echo/framework/data.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <span>
#include <string_view>

namespace echo::framework {

inline constexpr std::size_t kMaxAbilityDefinitions = 32;
inline constexpr std::size_t kAbilityIdStorage = 128;

struct AbilityDefinition {
    std::string_view id;
};

class AbilityModule final : public IModule {
public:
    AbilityModule() noexcept = default;
    AbilityModule(const AbilityModule&) = delete;
    AbilityModule& operator=(const AbilityModule&) = delete;
    AbilityModule(AbilityModule&&) = delete;
    AbilityModule& operator=(AbilityModule&&) = delete;

    [[nodiscard]] const ModuleInfo& info() const noexcept override;
    bool initialize(ModuleContext&) noexcept override;
    void shutdown() noexcept override;

    [[nodiscard]] Status load(JsonValue document) noexcept;
    [[nodiscard]] const AbilityDefinition* find(std::string_view id) const noexcept;
    [[nodiscard]] std::span<const AbilityDefinition> definitions() const noexcept {
        return {definitions_.data(), definitionCount_};
    }
    [[nodiscard]] bool initialized() const noexcept { return initialized_; }

private:
    std::array<std::array<char, kAbilityIdStorage>, kMaxAbilityDefinitions> ids_{};
    std::array<AbilityDefinition, kMaxAbilityDefinitions> definitions_{};
    std::size_t definitionCount_{};
    bool loaded_{};
    bool initialized_{};
};

template <std::size_t Capacity>
class AbilitySet {
    static_assert(Capacity > 0);

public:
    [[nodiscard]] Status unlock(const AbilityModule& module,
                                std::string_view id) noexcept {
        if (!module.initialized()) {
            return {ErrorCode::InvalidState, "echo.rpg.abilities",
                    "The abilities module must be initialized before unlocking an ability."};
        }
        if (module.find(id) == nullptr) {
            return {ErrorCode::NotFound, id, "Ability definition is not registered."};
        }
        if (contains(id)) {
            return {ErrorCode::DuplicateId, id, "Ability is already unlocked."};
        }
        if (id.size() >= kAbilityIdStorage) {
            return {ErrorCode::InvalidValue, id, "Ability ID exceeds fixed storage."};
        }
        const auto free = std::find_if(ids_.begin(), ids_.end(),
            [](const auto& entry) { return entry[0] == '\0'; });
        if (free == ids_.end()) {
            return {ErrorCode::CapacityExceeded, "abilities",
                    "Ability set capacity is full."};
        }
        std::copy(id.begin(), id.end(), free->begin());
        return Status::success();
    }

    [[nodiscard]] bool contains(std::string_view id) const noexcept {
        for (const auto& entry : ids_) {
            if (entry[0] != '\0' && std::string_view(entry.data()) == id) return true;
        }
        return false;
    }

    void reset() noexcept { ids_.fill({}); }

private:
    std::array<std::array<char, kAbilityIdStorage>, Capacity> ids_{};
};

[[nodiscard]] CapabilityManifestFragment abilitiesCapabilityManifestFragment() noexcept;

} // namespace echo::framework
