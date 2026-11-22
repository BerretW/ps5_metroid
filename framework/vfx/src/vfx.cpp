#include "echo/framework/vfx.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace echo::vfx {
namespace {

constexpr float kMaxEmissionRate = 10000.0F;
constexpr float kMaxVelocityMagnitude = 100000.0F;
constexpr float kMaxGravityMagnitude = 100000.0F;
constexpr float kMaxEmitterPosition = 1000000.0F;
constexpr float kMaxParticleScale = 10000.0F;
constexpr float kMaxFixedDeltaSeconds = 0.25F;
constexpr std::array<std::string_view, 1> kVfxProvides{
    "echo.service.vfx.cpu_particles"};
constexpr framework::ModuleInfo kVfxModuleInfo{
    "echo.vfx.cpu_particles",
    "CPU Particle Effects",
    1,
    {},
    {},
    kVfxProvides,
    {},
};

[[nodiscard]] bool boundedVector(rendering::Vec3 value, float maximum) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) &&
           std::isfinite(value.z) && std::abs(value.x) <= maximum &&
           std::abs(value.y) <= maximum && std::abs(value.z) <= maximum;
}

[[nodiscard]] bool validColor(rendering::Vec4 value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) &&
           std::isfinite(value.z) && std::isfinite(value.w) &&
           value.x >= 0.0F && value.x <= 1.0F &&
           value.y >= 0.0F && value.y <= 1.0F &&
           value.z >= 0.0F && value.z <= 1.0F &&
           value.w >= 0.0F && value.w <= 1.0F;
}

[[nodiscard]] rendering::Vec4 lerpColor(rendering::Vec4 a, rendering::Vec4 b,
                                         float amount) noexcept {
    return {a.x + (b.x - a.x) * amount,
            a.y + (b.y - a.y) * amount,
            a.z + (b.z - a.z) * amount,
            a.w + (b.w - a.w) * amount};
}

} // namespace

framework::Status validateEmitterConfig(const EmitterConfig& config) noexcept {
    if (config.maximumParticles == 0 ||
        config.maximumParticles > kMaxParticlesPerEmitter ||
        !std::isfinite(config.minimumLifetime) ||
        !std::isfinite(config.maximumLifetime) ||
        config.minimumLifetime <= 0.0F ||
        config.maximumLifetime < config.minimumLifetime ||
        config.maximumLifetime > kMaxParticleLifetimeSeconds ||
        !std::isfinite(config.emissionRate) || config.emissionRate < 0.0F ||
        config.emissionRate > kMaxEmissionRate ||
        !boundedVector(config.position, kMaxEmitterPosition) ||
        !boundedVector(config.initialVelocity, kMaxVelocityMagnitude) ||
        !boundedVector(config.velocitySpread, kMaxVelocityMagnitude) ||
        config.velocitySpread.x < 0.0F || config.velocitySpread.y < 0.0F ||
        config.velocitySpread.z < 0.0F ||
        std::abs(config.initialVelocity.x) + config.velocitySpread.x > kMaxVelocityMagnitude ||
        std::abs(config.initialVelocity.y) + config.velocitySpread.y > kMaxVelocityMagnitude ||
        std::abs(config.initialVelocity.z) + config.velocitySpread.z > kMaxVelocityMagnitude ||
        !boundedVector(config.gravity, kMaxGravityMagnitude) ||
        !validColor(config.startColor) || !validColor(config.endColor) ||
        !std::isfinite(config.startScale) || !std::isfinite(config.endScale) ||
        config.startScale < 0.0F || config.endScale < 0.0F ||
        config.startScale > kMaxParticleScale || config.endScale > kMaxParticleScale) {
        return {framework::ErrorCode::InvalidValue, "emitter",
                "Emitter bounds, lifetime, rate, velocity, and gravity are invalid."};
    }
    if (config.continuous && config.emissionRate <= 0.0F) {
        return {framework::ErrorCode::InvalidValue, "emissionRate",
                "Continuous emitters require a positive emission rate."};
    }
    return framework::Status::success();
}

framework::Status ParticleSystem::createEmitter(const EmitterConfig& config,
                                                EmitterHandle& output) noexcept {
    output = {};
    const framework::Status valid = validateEmitterConfig(config);
    if (!valid) return valid;
    for (std::size_t index = 0; index < emitters_.size(); ++index) {
        Emitter& emitter = emitters_[index];
        if (emitter.active) continue;
        const std::uint32_t generation = emitter.generation == 0 ? 1 : emitter.generation;
        emitter = {};
        emitter.generation = generation;
        emitter.active = true;
        emitter.config = config;
        emitter.randomState = config.randomSeed == 0 ? 0xA341316CU : config.randomSeed;
        emitter.gravityStart = config.gravity;
        emitter.gravityTarget = config.gravity;
        ++emitterCount_;
        output = {static_cast<std::uint32_t>(index), emitter.generation};
        return framework::Status::success();
    }
    return {framework::ErrorCode::CapacityExceeded, "emitters",
            "Particle system emitter capacity is full."};
}

framework::Status ParticleSystem::destroyEmitter(EmitterHandle handle) noexcept {
    Emitter* emitter = find(handle);
    if (emitter == nullptr) {
        return {framework::ErrorCode::NotFound, "emitter",
                "Emitter handle is stale or invalid."};
    }
    const std::uint32_t nextGeneration = emitter->generation + 1U;
    emitter->particles.fill({});
    emitter->config = {};
    emitter->particleCount = 0;
    emitter->emissionAccumulator = 0.0F;
    emitter->active = false;
    emitter->generation = nextGeneration == 0 ? 1 : nextGeneration;
    --emitterCount_;
    return framework::Status::success();
}

framework::Status ParticleSystem::configureEmitter(
    EmitterHandle handle, const EmitterConfig& config) noexcept {
    const framework::Status valid = validateEmitterConfig(config);
    if (!valid) return valid;
    Emitter* emitter = find(handle);
    if (emitter == nullptr) {
        return {framework::ErrorCode::NotFound, "emitter",
                "Emitter handle is stale or invalid."};
    }
    emitter->config = config;
    emitter->particleCount = std::min<std::size_t>(emitter->particleCount,
                                                    config.maximumParticles);
    emitter->emissionAccumulator = 0.0F;
    emitter->gravityStart = emitter->config.gravity;
    emitter->gravityTarget = emitter->config.gravity;
    emitter->gravityElapsed = 0.0F;
    emitter->gravityDuration = 0.0F;
    if (emitter->randomState == 0) {
        emitter->randomState = config.randomSeed == 0 ? 0xA341316CU : config.randomSeed;
    }
    return framework::Status::success();
}

framework::Status ParticleSystem::emitBurst(EmitterHandle handle,
                                            std::size_t count) noexcept {
    Emitter* emitter = find(handle);
    if (emitter == nullptr) {
        return {framework::ErrorCode::NotFound, "emitter",
                "Emitter handle is stale or invalid."};
    }
    if (count > emitter->config.maximumParticles - emitter->particleCount) {
        return {framework::ErrorCode::CapacityExceeded, "particles",
                "Burst exceeds remaining emitter particle capacity."};
    }
    for (std::size_t index = 0; index < count; ++index) {
        const framework::Status status = spawn(*emitter);
        if (!status) return status;
    }
    return framework::Status::success();
}

framework::Status ParticleSystem::setGravity(EmitterHandle handle,
                                             rendering::Vec3 gravity,
                                             float transitionSeconds) noexcept {
    Emitter* emitter = find(handle);
    if (emitter == nullptr) {
        return {framework::ErrorCode::NotFound, "emitter",
                "Emitter handle is stale or invalid."};
    }
    if (!boundedVector(gravity, kMaxGravityMagnitude) ||
        !std::isfinite(transitionSeconds) || transitionSeconds < 0.0F ||
        transitionSeconds > kMaxParticleLifetimeSeconds) {
        return {framework::ErrorCode::InvalidValue, "gravity",
                "Gravity and transition duration must be finite and bounded."};
    }
    emitter->gravityStart = emitter->config.gravity;
    emitter->gravityTarget = gravity;
    emitter->gravityElapsed = 0.0F;
    emitter->gravityDuration = transitionSeconds;
    if (transitionSeconds == 0.0F) {
        emitter->config.gravity = gravity;
        emitter->gravityStart = gravity;
    }
    return framework::Status::success();
}

framework::Status ParticleSystem::setPosition(EmitterHandle handle,
                                              rendering::Vec3 position) noexcept {
    Emitter* emitter = find(handle);
    if (emitter == nullptr) {
        return {framework::ErrorCode::NotFound, "emitter",
                "Emitter handle is stale or invalid."};
    }
    if (!boundedVector(position, kMaxEmitterPosition)) {
        return {framework::ErrorCode::InvalidValue, "position",
                "Emitter position must be finite and bounded."};
    }
    emitter->config.position = position;
    return framework::Status::success();
}

framework::Status ParticleSystem::update(float fixedDeltaSeconds) noexcept {
    if (!std::isfinite(fixedDeltaSeconds) || fixedDeltaSeconds <= 0.0F ||
        fixedDeltaSeconds > kMaxFixedDeltaSeconds) {
        return {framework::ErrorCode::InvalidValue, "fixedDeltaSeconds",
                "Particle updates require a finite positive fixed step no larger than 0.25 seconds."};
    }
    for (Emitter& emitter : emitters_) {
        if (!emitter.active) continue;
        if (emitter.gravityDuration > 0.0F) {
            emitter.gravityElapsed = std::min(emitter.gravityElapsed + fixedDeltaSeconds,
                                              emitter.gravityDuration);
            const float amount = emitter.gravityElapsed / emitter.gravityDuration;
            emitter.config.gravity = lerp(emitter.gravityStart,
                                          emitter.gravityTarget, amount);
            if (emitter.gravityElapsed >= emitter.gravityDuration) {
                emitter.gravityDuration = 0.0F;
                emitter.gravityStart = emitter.gravityTarget;
                emitter.config.gravity = emitter.gravityTarget;
            }
        }

        for (std::size_t index = 0; index < emitter.particleCount;) {
            Particle& particle = emitter.particles[index];
            particle.velocity = particle.velocity + emitter.config.gravity * fixedDeltaSeconds;
            particle.position = particle.position + particle.velocity * fixedDeltaSeconds;
            particle.age += fixedDeltaSeconds;
            const float lifeAmount = std::min(particle.age / particle.lifetime, 1.0F);
            particle.color = lerpColor(emitter.config.startColor,
                                       emitter.config.endColor, lifeAmount);
            particle.scale = emitter.config.startScale +
                (emitter.config.endScale - emitter.config.startScale) * lifeAmount;
            if (particle.age >= particle.lifetime ||
                !finite(particle.position) || !finite(particle.velocity)) {
                --emitter.particleCount;
                if (index != emitter.particleCount) {
                    particle = emitter.particles[emitter.particleCount];
                }
                emitter.particles[emitter.particleCount] = {};
                continue;
            }
            ++index;
        }

        if (emitter.config.continuous) {
            const float amount = emitter.config.emissionRate * fixedDeltaSeconds +
                                 emitter.emissionAccumulator;
            if (!std::isfinite(amount)) {
                emitter.emissionAccumulator = 0.0F;
                continue;
            }
            const std::size_t due = static_cast<std::size_t>(std::floor(amount));
            emitter.emissionAccumulator = amount - static_cast<float>(due);
            const std::size_t available = emitter.config.maximumParticles -
                                          emitter.particleCount;
            const std::size_t emitCount = std::min(due, available);
            for (std::size_t index = 0; index < emitCount; ++index) {
                const framework::Status status = spawn(emitter);
                if (!status) break;
            }
        }
    }
    return framework::Status::success();
}

std::span<const Particle> ParticleSystem::particles(EmitterHandle handle) const noexcept {
    const Emitter* emitter = find(handle);
    return emitter == nullptr
        ? std::span<const Particle>{}
        : std::span<const Particle>{emitter->particles.data(), emitter->particleCount};
}

std::size_t ParticleSystem::activeParticleCount(EmitterHandle handle) const noexcept {
    const Emitter* emitter = find(handle);
    return emitter == nullptr ? 0 : emitter->particleCount;
}

void ParticleSystem::reset() noexcept {
    for (Emitter& emitter : emitters_) {
        std::uint32_t nextGeneration = emitter.generation + 1U;
        if (nextGeneration == 0) nextGeneration = 1;
        emitter = {};
        emitter.generation = nextGeneration;
    }
    emitterCount_ = 0;
}

ParticleSystem::Emitter* ParticleSystem::find(EmitterHandle handle) noexcept {
    if (!handle.valid() || handle.index >= emitters_.size()) return nullptr;
    Emitter& emitter = emitters_[handle.index];
    return emitter.active && emitter.generation == handle.generation
        ? &emitter : nullptr;
}

const ParticleSystem::Emitter* ParticleSystem::find(EmitterHandle handle) const noexcept {
    if (!handle.valid() || handle.index >= emitters_.size()) return nullptr;
    const Emitter& emitter = emitters_[handle.index];
    return emitter.active && emitter.generation == handle.generation
        ? &emitter : nullptr;
}

std::uint32_t ParticleSystem::random(Emitter& emitter) noexcept {
    std::uint32_t value = emitter.randomState;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    emitter.randomState = value == 0 ? 0xA341316CU : value;
    return emitter.randomState;
}

float ParticleSystem::randomUnit(Emitter& emitter) noexcept {
    return static_cast<float>(random(emitter) >> 8) * (1.0F / 16777216.0F);
}

bool ParticleSystem::finite(rendering::Vec3 value) noexcept {
    return boundedVector(value, std::numeric_limits<float>::max());
}

rendering::Vec3 ParticleSystem::lerp(rendering::Vec3 a, rendering::Vec3 b,
                                    float amount) noexcept {
    return a + (b - a) * amount;
}

framework::Status ParticleSystem::spawn(Emitter& emitter) noexcept {
    if (emitter.particleCount >= emitter.config.maximumParticles) {
        return {framework::ErrorCode::CapacityExceeded, "particles",
                "Emitter particle capacity is full."};
    }
    const float range = emitter.config.maximumLifetime -
                        emitter.config.minimumLifetime;
    Particle particle{};
    particle.position = emitter.config.position;
    particle.color = emitter.config.startColor;
    particle.scale = emitter.config.startScale;
    particle.velocity = {
        emitter.config.initialVelocity.x +
            (randomUnit(emitter) * 2.0F - 1.0F) * emitter.config.velocitySpread.x,
        emitter.config.initialVelocity.y +
            (randomUnit(emitter) * 2.0F - 1.0F) * emitter.config.velocitySpread.y,
        emitter.config.initialVelocity.z +
            (randomUnit(emitter) * 2.0F - 1.0F) * emitter.config.velocitySpread.z};
    particle.lifetime = emitter.config.minimumLifetime + randomUnit(emitter) * range;
    if (!finite(particle.position) || !finite(particle.velocity) ||
        !std::isfinite(particle.lifetime) || particle.lifetime <= 0.0F) {
        return {framework::ErrorCode::InvalidValue, "particle",
                "Emitter produced a non-finite particle."};
    }
    emitter.particles[emitter.particleCount++] = particle;
    return framework::Status::success();
}

const framework::ModuleInfo& VfxModule::info() const noexcept {
    return kVfxModuleInfo;
}

bool VfxModule::initialize(framework::ModuleContext&) noexcept {
    if (initialized_) return true;
    system_.reset();
    initialized_ = true;
    return true;
}

void VfxModule::shutdown() noexcept {
    system_.reset();
    initialized_ = false;
}

} // namespace echo::vfx
