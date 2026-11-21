#pragma once

#include "echo/framework/schema.hpp"
#include "echo/framework/capability_manifest.hpp"

#include <array>

namespace echo {

inline constexpr float kDefaultInteractionReach = 52.0F;
inline constexpr float kMaxInteractionReach = 128.0F;
inline constexpr std::string_view kDialogueInteractionComponentId =
    "echo.project_echo:component/dialogue_interaction";

struct DialogueInteractionComponent {
    float horizontalReach{kDefaultInteractionReach};
};

namespace project_components_detail {

[[nodiscard]] inline framework::Status interactionFactory(
    void*, const framework::ComponentConfigView& config,
    framework::ComponentInstance& output) noexcept {
    double reach{};
    const framework::Status status = config.getFloat("horizontalReach", reach);
    if (!status) {
        return status;
    }
    return output.store(DialogueInteractionComponent{static_cast<float>(reach)});
}

} // namespace project_components_detail

[[nodiscard]] inline framework::CapabilityManifestFragment
projectCapabilityManifestFragment() noexcept {
    using namespace framework;
    static constexpr std::array<std::string_view, 2> requiredModules{
        "echo.state", "echo.narrative.dialogue"};
    static constexpr std::array<std::string_view, 4> optionalModules{
        "echo.rpg.abilities", "echo.rpg.attributes",
        "echo.rpg.equipment", "echo.rpg.inventory"};
    static constexpr std::array<std::string_view, 1> stateProvides{"echo.state"};
    static constexpr std::array<std::string_view, 1> projectProvides{"echo.actions"};
    static constexpr std::array<ManifestCapabilityDescriptor, 2> capabilities{{
        {"echo.inventory.write"}, {"echo.state.write"},
    }};
    static constexpr std::array<ManifestModuleDescriptor, 2> modules{{
        {"echo.state", "Echo state service", 1, {}, {}, stateProvides, {}},
        {"echo.project_echo", "PROJECT ECHO adapter", 1, requiredModules, optionalModules,
         projectProvides, {}},
    }};
    static constexpr std::array<ManifestActionParameter, 2> stateFlagParameters{{
        {"key", "string", true}, {"value", "boolean", true},
    }};
    static constexpr std::array<ManifestActionParameter, 1> grantItemParameters{{
        {"item", "string", true},
    }};
    static constexpr std::array<ManifestActionDescriptor, 2> actions{{
        {"echo.inventory.grant", "echo.inventory.write", true, grantItemParameters},
        {"echo.state.set_flag", "echo.state.write", true, stateFlagParameters},
    }};
    static constexpr std::array<ManifestActionParameter, 3> abilityUnlockedFields{{
        {"actorIndex", "integer", true},
        {"actorGeneration", "integer", true},
        {"ability", "string", true},
    }};
    static constexpr std::array<ManifestEventDescriptor, 1> events{{
        {"echo.project_echo.event.ability_unlocked", 1, abilityUnlockedFields},
    }};
    static constexpr std::array<ManifestSchemaDescriptor, 1> schemas{{
        {"echo.project_echo.game_config", 1},
    }};
    static constexpr std::array<std::string_view, 1> constraints{
        "The reference game preserves its existing five-room progression."};
    static constexpr std::array<ManifestFeatureDescriptor, 1> features{{
        {"echo.project_echo.reference_game",
         "Project-specific components and actions are compiled into the reference game.",
         constraints},
    }};
    return {.modules = modules,
            .capabilities = capabilities,
            .actions = actions,
            .events = events,
            .contentSchemas = schemas,
            .runtimeFeatures = features};
}

[[nodiscard]] inline framework::Status registerProjectComponentTypes(
    framework::ComponentTypeRegistry& registry) noexcept {
    using namespace framework;
    static constexpr std::array<PropertyDescriptor, 1> properties{{
        {.name = "horizontalReach", .label = "Horizontal reach",
         .description = "Extra horizontal distance for talking to the engineer.",
         .group = "Dialogue", .unit = "px", .type = PropertyType::Float,
         .editor = EditorControl::Slider, .hasDefault = true,
         .defaultValue = {PropertyType::Float, false, 0, kDefaultInteractionReach},
         .hasMinimum = true, .hasMaximum = true,
         .minimum = 0.0, .maximum = kMaxInteractionReach},
    }};
    static constexpr std::array<ComponentDependency, 1> dependencies{{
        {"echo:component/transform2d", true},
    }};
    static constexpr ComponentTypeDescriptor descriptor{
        kDialogueInteractionComponentId, 1, "Dialogue interaction",
        "Horizontal reach for the engineer's conversation.", "PROJECT ECHO",
        properties, dependencies, &project_components_detail::interactionFactory,
        nullptr, nullptr};
    return registry.add(descriptor);
}

} // namespace echo
