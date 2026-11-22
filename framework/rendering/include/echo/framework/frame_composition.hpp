#pragma once

#include "echo/framework/rendering.hpp"
#include <array>

namespace echo::rendering {

inline constexpr std::size_t kMaxFramePasses = 16;
inline constexpr std::size_t kMaxFrameResources = 16;

enum class RenderPassStage : std::uint8_t {
    BeginFrame,
    Shadow,
    Opaque,
    Transparent,
    WorldSprites,
    Particles,
    Postprocessing,
    Ui,
    Presentation
};

struct RenderPassHandle {
    std::uint32_t index{UINT32_MAX};
    std::uint32_t generation{};
    bool valid() const noexcept { return index != UINT32_MAX && generation != 0; }
};

struct FrameResourceHandle {
    std::uint32_t index{UINT32_MAX};
    std::uint32_t generation{};
    bool valid() const noexcept { return index != UINT32_MAX && generation != 0; }
};

enum class FrameResourceAccess : std::uint8_t { Read, Write };

using RenderPassCallback = Status (*)(void*) noexcept;

class FramePassScheduler {
public:
    Status add(RenderPassStage stage, RenderPassCallback callback, void* context,
               RenderPassHandle& handle) noexcept;
    Status dependsOn(RenderPassHandle pass, RenderPassHandle dependency) noexcept;
    Status createResource(bool initialized, FrameResourceHandle& handle) noexcept;
    Status useResource(RenderPassHandle pass, FrameResourceHandle resource,
                       FrameResourceAccess access) noexcept;
    Status setEnabled(RenderPassHandle pass, bool enabled) noexcept;
    Status execute() noexcept;
    Status clear() noexcept;
    std::size_t size() const noexcept { return count_; }

private:
    struct Pass {
        RenderPassStage stage{};
        RenderPassCallback callback{};
        void* context{};
        std::uint32_t dependencies{};
        std::uint32_t reads{};
        std::uint32_t writes{};
        bool enabled{true};
    };

    struct Resource {
        bool initialized{};
    };

    std::array<Pass, kMaxFramePasses> passes_{};
    std::array<Resource, kMaxFrameResources> resources_{};
    std::size_t count_{};
    std::size_t resourceCount_{};
    std::uint32_t generation_{1};
    bool executing_{};

    bool valid(RenderPassHandle handle) const noexcept;
    bool valid(FrameResourceHandle handle) const noexcept;
};

} // namespace echo::rendering
