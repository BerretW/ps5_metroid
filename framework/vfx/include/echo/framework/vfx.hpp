#pragma once

#include "echo/framework/core.hpp"
#include "echo/framework/data.hpp"
#include "echo/framework/rendering.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace echo::vfx {

inline constexpr std::size_t kMaxEmitters = 16;
inline constexpr std::size_t kMaxParticlesPerEmitter = 256;
inline constexpr float kMaxParticleLifetimeSeconds = 120.0F;

using EmitterHandle = rendering::Handle<struct EmitterTag>;

struct EmitterConfig {
    std::uint16_t maximumParticles{64};
    float minimumLifetime{0.25F};
    float maximumLifetime{1.0F};
    float emissionRate{};
    rendering::Vec3 position{};
    rendering::Vec3 initialVelocity{};
    rendering::Vec3 velocitySpread{};
    rendering::Vec3 gravity{};
    rendering::Vec4 startColor{1, 1, 1, 1};
    rendering::Vec4 endColor{1, 1, 1, 0};
    float startScale{1.0F};
    float endScale{};
    std::uint32_t randomSeed{0x9E3779B9U};
    bool continuous{};
};

struct Particle {
    rendering::Vec3 position{};
    rendering::Vec3 velocity{};
    float age{};
    float lifetime{};
    rendering::Vec4 color{1, 1, 1, 1};
    float scale{1.0F};
};

[[nodiscard]] framework::Status validateEmitterConfig(
    const EmitterConfig& config) noexcept;

class ParticleSystem {
public:
    [[nodiscard]] framework::Status createEmitter(const EmitterConfig& config,
                                                  EmitterHandle& output) noexcept;
    [[nodiscard]] framework::Status destroyEmitter(EmitterHandle handle) noexcept;
    [[nodiscard]] framework::Status configureEmitter(EmitterHandle handle,
                                                     const EmitterConfig& config) noexcept;
    [[nodiscard]] framework::Status emitBurst(EmitterHandle handle,
                                              std::size_t count) noexcept;
    [[nodiscard]] framework::Status setGravity(EmitterHandle handle,
                                               rendering::Vec3 gravity,
                                               float transitionSeconds = 0.0F) noexcept;
    [[nodiscard]] framework::Status setPosition(EmitterHandle handle,
                                                rendering::Vec3 position) noexcept;
    [[nodiscard]] framework::Status update(float fixedDeltaSeconds) noexcept;
    [[nodiscard]] std::span<const Particle> particles(EmitterHandle handle) const noexcept;
    [[nodiscard]] std::size_t activeParticleCount(EmitterHandle handle) const noexcept;
    [[nodiscard]] std::size_t emitterCount() const noexcept { return emitterCount_; }
    void reset() noexcept;

private:
    struct Emitter {
        EmitterConfig config{};
        std::array<Particle, kMaxParticlesPerEmitter> particles{};
        std::uint32_t generation{1};
        std::uint32_t randomState{};
        std::size_t particleCount{};
        float emissionAccumulator{};
        rendering::Vec3 gravityStart{};
        rendering::Vec3 gravityTarget{};
        float gravityElapsed{};
        float gravityDuration{};
        bool active{};
    };

    [[nodiscard]] Emitter* find(EmitterHandle handle) noexcept;
    [[nodiscard]] const Emitter* find(EmitterHandle handle) const noexcept;
    [[nodiscard]] static std::uint32_t random(Emitter& emitter) noexcept;
    [[nodiscard]] static float randomUnit(Emitter& emitter) noexcept;
    [[nodiscard]] static bool finite(rendering::Vec3 value) noexcept;
    [[nodiscard]] static rendering::Vec3 lerp(rendering::Vec3 a,
                                              rendering::Vec3 b,
                                              float amount) noexcept;
    [[nodiscard]] static framework::Status spawn(Emitter& emitter) noexcept;

    std::array<Emitter, kMaxEmitters> emitters_{};
    std::size_t emitterCount_{};
};

struct EmitterDefinition {
    std::string_view id;
    EmitterConfig config{};
};

class EmitterDocument {
public:
    EmitterDocument() noexcept = default;
    EmitterDocument(const EmitterDocument&) = delete;
    EmitterDocument& operator=(const EmitterDocument&) = delete;
    EmitterDocument(EmitterDocument&&) = delete;
    EmitterDocument& operator=(EmitterDocument&&) = delete;

    [[nodiscard]] framework::Status parse(const char* data, std::size_t size,
                                          framework::JsonError& error) noexcept;
    [[nodiscard]] const EmitterDefinition* definition() const noexcept {
        return loaded_ ? &definition_ : nullptr;
    }

private:
    void reset() noexcept;

    framework::JsonDocument document_{};
    EmitterDefinition definition_{};
    bool loaded_{};
};

class VfxModule final : public framework::IModule {
public:
    [[nodiscard]] const framework::ModuleInfo& info() const noexcept override;
    bool initialize(framework::ModuleContext& context) noexcept override;
    void shutdown() noexcept override;
    [[nodiscard]] ParticleSystem& system() noexcept { return system_; }
    [[nodiscard]] const ParticleSystem& system() const noexcept { return system_; }
    [[nodiscard]] bool initialized() const noexcept { return initialized_; }

private:
    ParticleSystem system_{};
    bool initialized_{};
};

} // namespace echo::vfx
