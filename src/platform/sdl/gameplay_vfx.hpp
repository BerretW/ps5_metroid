#pragma once

#include "echo/framework/vfx.hpp"
#include "game/game.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <span>

namespace echo::platform {

class GameplayVfx {
public:
    struct Snapshot {
        RoomId room{};
        int deaths{};
        std::array<Enemy, kEnemyCount> enemies{};
        std::array<Enemy, kMaxRoamers> roamers{};
        std::size_t roamerCount{};
        Vec2 playerPosition{384.0F, 192.0F};
        float roomWidth{768.0F};
        float roomHeight{384.0F};
    };

    [[nodiscard]] static Snapshot capture(const Game& game) noexcept {
        Snapshot result{};
        result.room = game.roomId();
        result.deaths = game.deaths();
        std::copy(game.enemies().begin(), game.enemies().end(), result.enemies.begin());
        const auto roamers = game.roamers();
        result.roamerCount = roamers.size();
        std::copy(roamers.begin(), roamers.end(), result.roamers.begin());
        result.playerPosition = game.player().position;
        result.roomWidth = game.roomWidth();
        result.roomHeight = game.roomHeight();
        return result;
    }

    [[nodiscard]] framework::Status reset() noexcept {
        system_.reset();
        vfx::EmitterConfig shot{};
        shot.maximumParticles = 128;
        shot.minimumLifetime = 0.12F;
        shot.maximumLifetime = 0.32F;
        shot.velocitySpread = {78.0F, 78.0F, 0.0F};
        shot.gravity = {0.0F, 20.0F, 0.0F};
        shot.startColor = {0.2F, 0.9F, 1.0F, 1.0F};
        shot.endColor = {1.0F, 0.35F, 0.08F, 0.0F};
        shot.startScale = 1.0F;
        shot.endScale = 0.15F;
        shot.randomSeed = 0x4543484FU;
        auto status = system_.createEmitter(shot, shotEmitter_);
        if (!status) return status;

        vfx::EmitterConfig impact{};
        impact.maximumParticles = 192;
        impact.minimumLifetime = 0.16F;
        impact.maximumLifetime = 0.38F;
        impact.velocitySpread = {110.0F, 110.0F, 0.0F};
        impact.gravity = {0.0F, 40.0F, 0.0F};
        impact.startColor = {1.0F, 0.75F, 0.15F, 1.0F};
        impact.endColor = {1.0F, 0.08F, 0.02F, 0.0F};
        impact.startScale = 0.9F;
        impact.endScale = 0.1F;
        impact.randomSeed = 0x494D5041U;
        status = system_.createEmitter(impact, impactEmitter_);
        if (!status) return status;

        vfx::EmitterConfig explosion{};
        explosion.maximumParticles = 256;
        explosion.minimumLifetime = 0.28F;
        explosion.maximumLifetime = 0.55F;
        explosion.velocitySpread = {170.0F, 170.0F, 0.0F};
        explosion.gravity = {0.0F, 24.0F, 0.0F};
        explosion.startColor = {1.0F, 0.94F, 0.48F, 1.0F};
        explosion.endColor = {1.0F, 0.12F, 0.02F, 0.0F};
        explosion.startScale = 1.8F;
        explosion.endScale = 0.1F;
        explosion.randomSeed = 0x4558504CU;
        status = system_.createEmitter(explosion, explosionEmitter_);
        if (!status) return status;

        fogConfig_ = {};
        fogConfig_.maximumParticles = 256;
        fogConfig_.minimumLifetime = 4.0F;
        fogConfig_.maximumLifetime = 6.0F;
        fogConfig_.emissionRate = 18.0F;
        fogConfig_.velocitySpread = {5.0F, 2.0F, 0.0F};
        fogConfig_.startColor = {0.66F, 0.78F, 0.84F, 0.18F};
        fogConfig_.endColor = {0.66F, 0.78F, 0.84F, 0.0F};
        fogConfig_.startScale = 2.5F;
        fogConfig_.endScale = 3.2F;
        fogConfig_.randomSeed = 0x464F4721U;
        fogActive_ = false;
        fogSpawnIndex_ = 0;
        return system_.createEmitter(fogConfig_, fogEmitter_);
    }

    [[nodiscard]] framework::Status shot(const Projectile& projectile) noexcept {
        return emit(shotEmitter_, projectile.bounds, 8);
    }

    [[nodiscard]] framework::Status step(const Snapshot& before,
                                          const Snapshot& after) noexcept {
        if (before.roamerCount > before.roamers.size() ||
            after.roamerCount > after.roamers.size()) {
            return {framework::ErrorCode::InvalidValue, "roamers",
                    "Gameplay VFX snapshot exceeds roamer capacity."};
        }
        if (before.room != after.room || before.deaths != after.deaths) {
            auto status = reset();
            if (!status) return status;
            if (after.room == RoomId::CargoHold) return primeFog(after);
            return framework::Status::success();
        }
        if (after.room == RoomId::CargoHold) {
            if (!fogActive_) {
                const auto status = primeFog(after);
                if (!status) return status;
            }
            const auto status = system_.setPosition(fogEmitter_, fogPoint(fogSpawnIndex_, after));
            if (!status) return status;
            fogSpawnIndex_ = (fogSpawnIndex_ + 1U) % kFogPointCount;
        }
        auto status = system_.update(kFixedStep);
        if (!status) return status;
        status = emitDamage(before.enemies, after.enemies, after.room);
        if (!status) return status;
        return emitDamage(std::span<const Enemy>{before.roamers.data(), before.roamerCount},
                          std::span<const Enemy>{after.roamers.data(), after.roamerCount}, after.room);
    }

    [[nodiscard]] std::span<const vfx::Particle> shots() const noexcept {
        return system_.particles(shotEmitter_);
    }
    [[nodiscard]] std::span<const vfx::Particle> impacts() const noexcept {
        return system_.particles(impactEmitter_);
    }
    [[nodiscard]] std::span<const vfx::Particle> explosions() const noexcept {
        return system_.particles(explosionEmitter_);
    }
    [[nodiscard]] std::span<const vfx::Particle> fog() const noexcept {
        return system_.particles(fogEmitter_);
    }

private:
    static constexpr std::size_t kFogGridWidth = 4;
    static constexpr std::size_t kFogPointCount = kFogGridWidth * kFogGridWidth;
    static constexpr std::size_t kFogParticlesPerPoint = 8;

    [[nodiscard]] static rendering::Vec3 fogPoint(std::size_t index,
                                                  const Snapshot& snapshot) noexcept {
        const std::size_t column = index % kFogGridWidth;
        const std::size_t row = index / kFogGridWidth;
        const float originX = std::clamp(snapshot.playerPosition.x - 288.0F,
                                         0.0F, std::max(0.0F, snapshot.roomWidth - 576.0F));
        const float originY = std::clamp(snapshot.playerPosition.y - 144.0F,
                                         0.0F, std::max(0.0F, snapshot.roomHeight - 288.0F));
        return {originX + static_cast<float>(column) * 192.0F,
                originY + static_cast<float>(row) * 96.0F, 0.0F};
    }

    [[nodiscard]] framework::Status primeFog(const Snapshot& snapshot) noexcept {
        fogConfig_.continuous = true;
        auto status = system_.configureEmitter(fogEmitter_, fogConfig_);
        if (!status) return status;
        for (std::size_t index = 0; index < kFogPointCount; ++index) {
            status = system_.setPosition(fogEmitter_, fogPoint(index, snapshot));
            if (!status) return status;
            status = system_.emitBurst(fogEmitter_, kFogParticlesPerPoint);
            if (!status) return status;
        }
        fogSpawnIndex_ = 0;
        fogActive_ = true;
        return framework::Status::success();
    }

    [[nodiscard]] framework::Status emit(vfx::EmitterHandle handle, Rect bounds,
                                          std::size_t count) noexcept {
        const auto status = system_.setPosition(
            handle, {bounds.x + bounds.width * 0.5F,
                     bounds.y + bounds.height * 0.5F, 0.0F});
        if (!status) return status;
        return system_.emitBurst(handle, count);
    }

    [[nodiscard]] framework::Status emitDamage(std::span<const Enemy> before,
                                                std::span<const Enemy> after,
                                                RoomId room) noexcept {
        const auto count = std::min(before.size(), after.size());
        for (std::size_t index = 0; index < count; ++index) {
            if (before[index].alive && before[index].room == room &&
                after[index].room == room && after[index].health < before[index].health) {
                const auto status = emit(impactEmitter_, after[index].bounds, 12);
                if (!status) return status;
                if (!after[index].alive) {
                    const auto explosionStatus =
                        emit(explosionEmitter_, after[index].bounds, 32);
                    if (!explosionStatus) return explosionStatus;
                }
            }
        }
        return framework::Status::success();
    }

    vfx::ParticleSystem system_{};
    vfx::EmitterHandle shotEmitter_{};
    vfx::EmitterHandle impactEmitter_{};
    vfx::EmitterHandle explosionEmitter_{};
    vfx::EmitterHandle fogEmitter_{};
    vfx::EmitterConfig fogConfig_{};
    std::size_t fogSpawnIndex_{};
    bool fogActive_{};
};

} // namespace echo::platform
