#pragma once

#include "echo/framework/rendering.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace echo::rendering {

inline constexpr std::size_t kMaxEntities = 128;
using EntityHandle = Handle<struct EntityTag>;

struct Transform3D {
    Vec3 position{};
    Vec3 rotation{};
    Vec3 scale{1.0F, 1.0F, 1.0F};
};

class EntityStore {
public:
    [[nodiscard]] Status create(const Transform3D& transform,
                                EntityHandle& output) noexcept;
    [[nodiscard]] Status destroy(EntityHandle entity) noexcept;
    [[nodiscard]] Status setTransform(EntityHandle entity,
                                      const Transform3D& transform) noexcept;
    [[nodiscard]] Status getTransform(EntityHandle entity,
                                      Transform3D& output) const noexcept;
    [[nodiscard]] Status setParent(EntityHandle child,
                                   EntityHandle parent = {}) noexcept;
    [[nodiscard]] Status getParent(EntityHandle child,
                                   EntityHandle& output) const noexcept;
    [[nodiscard]] Status worldMatrix(EntityHandle entity,
                                     Mat4& output) const noexcept;
    [[nodiscard]] bool contains(EntityHandle entity) const noexcept;
    [[nodiscard]] std::size_t size() const noexcept { return activeCount_; }
    void reset() noexcept;

private:
    struct Slot {
        Transform3D local{};
        EntityHandle parent{};
        std::uint32_t generation{1};
        bool active{};
    };

    [[nodiscard]] const Slot* find(EntityHandle entity) const noexcept;
    [[nodiscard]] Slot* find(EntityHandle entity) noexcept;
    [[nodiscard]] static bool validTransform(const Transform3D& transform) noexcept;
    [[nodiscard]] static Mat4 localMatrix(const Transform3D& transform) noexcept;

    std::array<Slot, kMaxEntities> slots_{};
    std::size_t activeCount_{};
};

} // namespace echo::rendering
