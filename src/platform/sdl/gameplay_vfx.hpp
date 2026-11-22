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
    };

    [[nodiscard]] static Snapshot capture(const Game& game) noexcept {
        Snapshot result{};
        result.room = game.roomId();
        result.deaths = game.deaths();
        std::copy(game.enemies().begin(), game.enemies().end(), result.enemies.begin());
        const auto roamers = game.roamers();
        result.roamerCount = roamers.size();
        std::copy(roamers.begin(), roamers.end(), result.roamers.begin());
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
        return system_.createEmitter(impact, impactEmitter_);
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
            return reset();
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

private:
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
            }
        }
        return framework::Status::success();
    }

    vfx::ParticleSystem system_{};
    vfx::EmitterHandle shotEmitter_{};
    vfx::EmitterHandle impactEmitter_{};
};

} // namespace echo::platform
