#include "echo/framework/hierarchy.hpp"

#include <cmath>

namespace echo::rendering {
namespace {

constexpr float kMaxPosition = 1000000.0F;
constexpr float kMaxRotation = 100000.0F;
constexpr float kMaxScale = 100000.0F;

[[nodiscard]] bool bounded(Vec3 value, float maximum) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) &&
           std::isfinite(value.z) && std::abs(value.x) <= maximum &&
           std::abs(value.y) <= maximum && std::abs(value.z) <= maximum;
}

} // namespace

Status EntityStore::create(const Transform3D& transform,
                           EntityHandle& output) noexcept {
    output = {};
    if (!validTransform(transform)) {
        return {ErrorCode::InvalidValue, "transform",
                "Entity transform must be finite and within supported bounds."};
    }
    for (std::size_t index = 0; index < slots_.size(); ++index) {
        Slot& slot = slots_[index];
        if (slot.active) continue;
        if (slot.generation == 0) slot.generation = 1;
        slot.active = true;
        slot.local = transform;
        slot.parent = {};
        ++activeCount_;
        output = {static_cast<std::uint32_t>(index), slot.generation};
        return Status::success();
    }
    return {ErrorCode::CapacityExceeded, "entities",
            "Entity store capacity is full."};
}

Status EntityStore::destroy(EntityHandle entity) noexcept {
    Slot* slot = find(entity);
    if (slot == nullptr) {
        return {ErrorCode::NotFound, "entity", "Entity handle is stale or invalid."};
    }
    for (Slot& child : slots_) {
        if (child.active && child.parent == entity) {
            child.parent = {};
        }
    }
    std::uint32_t nextGeneration = slot->generation + 1U;
    if (nextGeneration == 0) nextGeneration = 1;
    slot->active = false;
    slot->parent = {};
    slot->local = {};
    slot->generation = nextGeneration;
    --activeCount_;
    return Status::success();
}

Status EntityStore::setTransform(EntityHandle entity,
                                const Transform3D& transform) noexcept {
    Slot* slot = find(entity);
    if (slot == nullptr) {
        return {ErrorCode::NotFound, "entity", "Entity handle is stale or invalid."};
    }
    if (!validTransform(transform)) {
        return {ErrorCode::InvalidValue, "transform",
                "Entity transform must be finite and within supported bounds."};
    }
    slot->local = transform;
    return Status::success();
}

Status EntityStore::getTransform(EntityHandle entity,
                                Transform3D& output) const noexcept {
    const Slot* slot = find(entity);
    if (slot == nullptr) {
        output = {};
        return {ErrorCode::NotFound, "entity", "Entity handle is stale or invalid."};
    }
    output = slot->local;
    return Status::success();
}

Status EntityStore::setParent(EntityHandle child,
                             EntityHandle parent) noexcept {
    Slot* childSlot = find(child);
    if (childSlot == nullptr) {
        return {ErrorCode::NotFound, "child", "Child handle is stale or invalid."};
    }
    if (!parent.valid()) {
        if (parent.index != UINT32_MAX || parent.generation != 0) {
            return {ErrorCode::InvalidArgument, "parent",
                    "Invalid parent handles must use the empty handle value."};
        }
        childSlot->parent = {};
        return Status::success();
    }
    if (find(parent) == nullptr) {
        return {ErrorCode::NotFound, "parent", "Parent handle is stale or invalid."};
    }
    if (child == parent) {
        return {ErrorCode::DependencyCycle, "parent", "An entity cannot parent itself."};
    }

    std::array<bool, kMaxEntities> visited{};
    EntityHandle current = parent;
    for (std::size_t depth = 0; depth < kMaxEntities; ++depth) {
        if (!current.valid()) break;
        if (current == child) {
            return {ErrorCode::DependencyCycle, "parent",
                    "Parent assignment would create an entity cycle."};
        }
        const Slot* currentSlot = find(current);
        if (currentSlot == nullptr) {
            return {ErrorCode::NotFound, "parent",
                    "Parent chain contains a stale entity handle."};
        }
        if (visited[current.index]) {
            return {ErrorCode::DependencyCycle, "parent",
                    "Existing parent chain contains a cycle."};
        }
        visited[current.index] = true;
        current = currentSlot->parent;
    }
    if (current.valid()) {
        return {ErrorCode::DependencyCycle, "parent",
                "Parent chain exceeds the bounded entity count."};
    }
    childSlot->parent = parent;
    return Status::success();
}

Status EntityStore::getParent(EntityHandle child,
                             EntityHandle& output) const noexcept {
    const Slot* slot = find(child);
    if (slot == nullptr) {
        output = {};
        return {ErrorCode::NotFound, "entity", "Entity handle is stale or invalid."};
    }
    output = slot->parent;
    return Status::success();
}

Status EntityStore::worldMatrix(EntityHandle entity,
                               Mat4& output) const noexcept {
    output = Mat4::identity();
    if (find(entity) == nullptr) {
        return {ErrorCode::NotFound, "entity", "Entity handle is stale or invalid."};
    }

    std::array<EntityHandle, kMaxEntities> chain{};
    std::array<bool, kMaxEntities> visited{};
    std::size_t count = 0;
    EntityHandle current = entity;
    while (current.valid()) {
        const Slot* slot = find(current);
        if (slot == nullptr) {
            output = Mat4::identity();
            return {ErrorCode::NotFound, "parent",
                    "Entity parent chain contains a stale handle."};
        }
        if (count == chain.size() || visited[current.index]) {
            output = Mat4::identity();
            return {ErrorCode::DependencyCycle, "parent",
                    "Entity parent chain contains a cycle or exceeds capacity."};
        }
        visited[current.index] = true;
        chain[count++] = current;
        current = slot->parent;
    }

    Mat4 world = Mat4::identity();
    while (count != 0) {
        const Slot* slot = find(chain[--count]);
        if (slot == nullptr) {
            output = Mat4::identity();
            return {ErrorCode::NotFound, "entity",
                    "Entity became invalid while propagating transforms."};
        }
        world = world * localMatrix(slot->local);
    }
    if (!finite(world)) {
        output = Mat4::identity();
        return {ErrorCode::InvalidValue, "transform",
                "World transform produced non-finite matrix values."};
    }
    output = world;
    return Status::success();
}

bool EntityStore::contains(EntityHandle entity) const noexcept {
    return find(entity) != nullptr;
}

void EntityStore::reset() noexcept {
    for (Slot& slot : slots_) {
        std::uint32_t nextGeneration = slot.generation + 1U;
        if (nextGeneration == 0) nextGeneration = 1;
        slot = {};
        slot.generation = nextGeneration;
    }
    activeCount_ = 0;
}

const EntityStore::Slot* EntityStore::find(EntityHandle entity) const noexcept {
    if (!entity.valid() || entity.index >= slots_.size()) return nullptr;
    const Slot& slot = slots_[entity.index];
    return slot.active && slot.generation == entity.generation ? &slot : nullptr;
}

EntityStore::Slot* EntityStore::find(EntityHandle entity) noexcept {
    if (!entity.valid() || entity.index >= slots_.size()) return nullptr;
    Slot& slot = slots_[entity.index];
    return slot.active && slot.generation == entity.generation ? &slot : nullptr;
}

bool EntityStore::validTransform(const Transform3D& transform) noexcept {
    return bounded(transform.position, kMaxPosition) &&
           bounded(transform.rotation, kMaxRotation) &&
           bounded(transform.scale, kMaxScale);
}

Mat4 EntityStore::localMatrix(const Transform3D& transform) noexcept {
    return translation(transform.position) * rotation(transform.rotation) *
           scaling(transform.scale);
}

} // namespace echo::rendering
