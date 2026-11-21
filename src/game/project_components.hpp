#pragma once

#include "echo/framework/schema.hpp"

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
