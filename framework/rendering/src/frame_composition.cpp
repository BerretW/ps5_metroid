#include "echo/framework/frame_composition.hpp"

namespace echo::rendering {
namespace {

constexpr std::uint8_t kLastStage =
    static_cast<std::uint8_t>(RenderPassStage::Presentation);

Status invalidState() noexcept {
    return {ErrorCode::InvalidState, "render pass scheduler",
            "The scheduler cannot be modified while executing."};
}

} // namespace

bool FramePassScheduler::valid(RenderPassHandle handle) const noexcept {
    return handle.valid() && handle.index < count_ && handle.generation == generation_;
}

bool FramePassScheduler::valid(FrameResourceHandle handle) const noexcept {
    return handle.valid() && handle.index < resourceCount_ &&
           handle.generation == generation_;
}

Status FramePassScheduler::add(RenderPassStage stage, RenderPassCallback callback,
                               void* context, RenderPassHandle& handle) noexcept {
    handle = {};
    if (executing_) return invalidState();
    if (static_cast<std::uint8_t>(stage) > kLastStage || callback == nullptr)
        return {ErrorCode::InvalidArgument, "render pass",
                "A known stage and callback are required."};
    if (count_ == passes_.size())
        return {ErrorCode::CapacityExceeded, "render pass",
                "Frame pass capacity is full."};
    const auto index = static_cast<std::uint32_t>(count_++);
    passes_[index] = {stage, callback, context, 0, 0, 0, true};
    handle = {index, generation_};
    return Status::success();
}

Status FramePassScheduler::dependsOn(RenderPassHandle pass,
                                     RenderPassHandle dependency) noexcept {
    if (executing_) return invalidState();
    if (!valid(pass) || !valid(dependency))
        return {ErrorCode::NotFound, "render pass",
                "A dependency handle is stale or invalid."};
    if (pass.index == dependency.index)
        return {ErrorCode::DependencyCycle, "render pass",
                "A pass cannot depend on itself."};
    if (passes_[dependency.index].stage > passes_[pass.index].stage)
        return {ErrorCode::InvalidArgument, "render pass dependency",
                "A pass cannot depend on a later pipeline stage."};
    passes_[pass.index].dependencies |= 1U << dependency.index;
    return Status::success();
}

Status FramePassScheduler::createResource(bool initialized,
                                          FrameResourceHandle& handle) noexcept {
    handle = {};
    if (executing_) return invalidState();
    if (resourceCount_ == resources_.size())
        return {ErrorCode::CapacityExceeded, "frame resource",
                "Frame resource capacity is full."};
    const auto index = static_cast<std::uint32_t>(resourceCount_++);
    resources_[index] = {initialized};
    handle = {index, generation_};
    return Status::success();
}

Status FramePassScheduler::useResource(RenderPassHandle pass,
                                       FrameResourceHandle resource,
                                       FrameResourceAccess access) noexcept {
    if (executing_) return invalidState();
    if (!valid(pass) || !valid(resource))
        return {ErrorCode::NotFound, "frame resource access",
                "A pass or resource handle is stale or invalid."};
    if (access != FrameResourceAccess::Read && access != FrameResourceAccess::Write)
        return {ErrorCode::InvalidArgument, "frame resource access",
                "A known read or write access is required."};

    auto& passData = passes_[pass.index];
    const auto bit = 1U << resource.index;
    const auto opposite = access == FrameResourceAccess::Read
        ? passData.writes : passData.reads;
    if (opposite & bit)
        return {ErrorCode::InvalidArgument, "frame resource access",
                "A pass cannot read and write the same resource."};
    if (access == FrameResourceAccess::Read)
        passData.reads |= bit;
    else
        passData.writes |= bit;
    return Status::success();
}

Status FramePassScheduler::setEnabled(RenderPassHandle pass, bool enabled) noexcept {
    if (executing_) return invalidState();
    if (!valid(pass))
        return {ErrorCode::NotFound, "render pass",
                "The pass handle is stale or invalid."};
    passes_[pass.index].enabled = enabled;
    return Status::success();
}

Status FramePassScheduler::execute() noexcept {
    if (executing_)
        return {ErrorCode::InvalidState, "render pass scheduler",
                "Recursive execution is not allowed."};
    executing_ = true;
    const auto completeMask = (1U << count_) - 1U;
    std::array<std::uint32_t, kMaxFramePasses> dependencies{};
    std::array<bool, kMaxFrameResources> initialized{};
    for (std::size_t i = 0; i < count_; ++i)
        dependencies[i] = passes_[i].dependencies;
    for (std::size_t i = 0; i < resourceCount_; ++i)
        initialized[i] = resources_[i].initialized;

    for (std::uint32_t resource = 0; resource < resourceCount_; ++resource) {
        std::array<std::uint32_t, kMaxFramePasses> users{};
        std::size_t userCount{};
        const auto resourceBit = 1U << resource;
        for (std::uint32_t pass = 0; pass < count_; ++pass) {
            if ((passes_[pass].reads | passes_[pass].writes) & resourceBit)
                users[userCount++] = pass;
        }
        for (std::size_t i = 1; i < userCount; ++i) {
            const auto value = users[i];
            std::size_t position = i;
            while (position > 0) {
                const auto previous = users[position - 1];
                if (passes_[previous].stage < passes_[value].stage ||
                    (passes_[previous].stage == passes_[value].stage &&
                     previous < value))
                    break;
                users[position] = previous;
                --position;
            }
            users[position] = value;
        }

        std::uint32_t lastWriter = UINT32_MAX;
        std::uint32_t readers{};
        for (std::size_t i = 0; i < userCount; ++i) {
            const auto pass = users[i];
            const auto passBit = 1U << pass;
            if (passes_[pass].reads & resourceBit) {
                if (lastWriter != UINT32_MAX)
                    dependencies[pass] |= 1U << lastWriter;
                readers |= passBit;
            } else {
                if (lastWriter != UINT32_MAX)
                    dependencies[pass] |= 1U << lastWriter;
                dependencies[pass] |= readers;
                readers = 0;
                lastWriter = pass;
            }
        }
    }

    std::uint32_t completed{};
    while (completed != completeMask) {
        std::uint32_t selected = UINT32_MAX;
        for (std::uint32_t i = 0; i < count_; ++i) {
            const auto bit = 1U << i;
            if ((completed & bit) || (dependencies[i] & completed) != dependencies[i])
                continue;
            if (selected == UINT32_MAX ||
                passes_[i].stage < passes_[selected].stage ||
                (passes_[i].stage == passes_[selected].stage && i < selected))
                selected = i;
        }
        if (selected == UINT32_MAX) {
            executing_ = false;
            return {ErrorCode::DependencyCycle, "render pass scheduler",
                    "Pass dependencies contain a cycle."};
        }
        const auto bit = 1U << selected;
        completed |= bit;
        if (passes_[selected].enabled) {
            for (std::uint32_t resource = 0; resource < resourceCount_; ++resource) {
                if ((passes_[selected].reads & (1U << resource)) &&
                    !initialized[resource]) {
                    executing_ = false;
                    return {ErrorCode::InvalidState, "frame resource read",
                            "A pass reads a resource before it has been initialized."};
                }
            }
            const auto status = passes_[selected].callback(passes_[selected].context);
            if (!status) {
                executing_ = false;
                return status;
            }
            for (std::uint32_t resource = 0; resource < resourceCount_; ++resource) {
                if (passes_[selected].writes & (1U << resource))
                    initialized[resource] = true;
            }
        }
    }
    executing_ = false;
    return Status::success();
}

Status FramePassScheduler::clear() noexcept {
    if (executing_) return invalidState();
    passes_ = {};
    resources_ = {};
    count_ = 0;
    resourceCount_ = 0;
    if (++generation_ == 0) ++generation_;
    return Status::success();
}

} // namespace echo::rendering
