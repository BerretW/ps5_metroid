#include "game/shmup.hpp"

#include <algorithm>
#include <cmath>
#include <initializer_list>

namespace echo {

namespace {

using framework::ErrorCode;
using framework::JsonValue;
using framework::Status;

constexpr float kPi = 3.14159265358979F;
constexpr float kPlayfieldTop = 24.0F;
constexpr float kBossEntrySpeed = 80.0F;

[[nodiscard]] Status bad(std::string_view subject, std::string_view detail) noexcept {
    return {ErrorCode::InvalidValue, subject, detail};
}

[[nodiscard]] bool onlyKeys(JsonValue object, std::initializer_list<std::string_view> allowed) noexcept {
    if (!object.isObject()) return false;
    for (std::size_t index = 0; index < object.size(); ++index) {
        std::string_view key;
        JsonValue value;
        if (!object.memberAt(index, key, value) ||
            std::find(allowed.begin(), allowed.end(), key) == allowed.end()) {
            return false;
        }
    }
    return true;
}

// Absent keys keep the default; present keys must be finite numbers inside [low, high].
[[nodiscard]] bool readNumber(JsonValue object, std::string_view key, float& output, float low,
                              float high) noexcept {
    const JsonValue value = object.find(key);
    if (!value.valid()) return true;
    double number = 0.0;
    std::int64_t integer = 0;
    if (value.integer(integer)) {
        number = static_cast<double>(integer);
    } else if (!value.number(number)) {
        return false;
    }
    if (!std::isfinite(number) || number < static_cast<double>(low) ||
        number > static_cast<double>(high)) {
        return false;
    }
    output = static_cast<float>(number);
    return true;
}

[[nodiscard]] bool readInt(JsonValue object, std::string_view key, int& output, int low,
                           int high) noexcept {
    float number = static_cast<float>(output);
    if (!readNumber(object, key, number, static_cast<float>(low), static_cast<float>(high))) {
        return false;
    }
    output = static_cast<int>(number);
    return true;
}

template <std::size_t N>
[[nodiscard]] bool readText(JsonValue object, std::string_view key, std::array<char, N>& output,
                            bool required, bool contentId) noexcept {
    const JsonValue value = object.find(key);
    if (!value.valid()) return !required;
    std::string_view text;
    if (!value.string(text) || text.empty() || text.size() >= N ||
        (contentId && !framework::isValidContentId(text))) {
        return false;
    }
    output = {};
    std::copy(text.begin(), text.end(), output.begin());
    return true;
}

[[nodiscard]] bool readColor(JsonValue object, std::string_view key,
                             std::array<std::uint8_t, 3>& output) noexcept {
    const JsonValue value = object.find(key);
    if (!value.valid()) return true;
    if (!value.isArray() || value.size() != 3) return false;
    for (std::size_t index = 0; index < 3; ++index) {
        std::int64_t channel = 0;
        if (!value.at(index).integer(channel) || channel < 0 || channel > 255) return false;
        output[index] = static_cast<std::uint8_t>(channel);
    }
    return true;
}

[[nodiscard]] bool readMotion(JsonValue value, bool boss, ShmupMotion& motion) noexcept {
    if (!onlyKeys(value, {"kind", "speed", "amplitude", "frequency", "anchorX"})) return false;
    std::string_view kind;
    if (!value.find("kind").string(kind)) return false;
    if (kind == "straight" && !boss) motion.kind = ShmupMotionKind::Straight;
    else if (kind == "sine") motion.kind = ShmupMotionKind::Sine;
    else if (kind == "dive" && !boss) motion.kind = ShmupMotionKind::Dive;
    else if (kind == "hold") motion.kind = ShmupMotionKind::Hold;
    else if (kind == "patrol" && boss) motion.kind = ShmupMotionKind::Patrol;
    else if (kind == "track" && boss) motion.kind = ShmupMotionKind::Track;
    else return false;
    return readNumber(value, "speed", motion.speed, 0.0F, 400.0F) &&
           readNumber(value, "amplitude", motion.amplitude, 0.0F, 320.0F) &&
           readNumber(value, "frequency", motion.frequency, 0.0F, 12.0F) &&
           readNumber(value, "anchorX", motion.anchorX, 0.0F, kShmupWidth);
}

[[nodiscard]] bool readAttack(JsonValue value, ShmupAttack& attack) noexcept {
    if (!onlyKeys(value, {"kind", "interval", "delay", "count", "spreadDegrees", "bulletSpeed",
                          "bulletSize"})) {
        return false;
    }
    std::string_view kind;
    if (!value.find("kind").string(kind)) return false;
    if (kind == "aimed") attack.kind = ShmupAttackKind::Aimed;
    else if (kind == "spread") attack.kind = ShmupAttackKind::Spread;
    else if (kind == "stream") attack.kind = ShmupAttackKind::Stream;
    else if (kind == "radial") attack.kind = ShmupAttackKind::Radial;
    else if (kind == "wall") attack.kind = ShmupAttackKind::Wall;
    else return false;
    float degrees = 0.0F;
    if (!readNumber(value, "interval", attack.interval, 0.1F, 10.0F) ||
        !readNumber(value, "delay", attack.delay, 0.0F, 10.0F) ||
        !readInt(value, "count", attack.count, 1, 24) ||
        !readNumber(value, "spreadDegrees", degrees, 0.0F, 120.0F) ||
        !readNumber(value, "bulletSpeed", attack.bulletSpeed, 20.0F, 500.0F) ||
        !readNumber(value, "bulletSize", attack.bulletSize, 3.0F, 24.0F)) {
        return false;
    }
    if (attack.kind == ShmupAttackKind::Wall && attack.count < 4) return false;
    attack.spread = degrees * kPi / 180.0F;
    return true;
}

[[nodiscard]] int findEnemyType(const ShmupStage& stage, std::string_view id) noexcept {
    for (std::size_t index = 0; index < stage.enemyTypeCount; ++index) {
        if (std::string_view(stage.enemyTypes[index].id.data()) == id) return static_cast<int>(index);
    }
    return -1;
}

[[nodiscard]] Status parseEnemyType(JsonValue value, ShmupStage& stage) noexcept {
    if (stage.enemyTypeCount == stage.enemyTypes.size()) {
        return {ErrorCode::CapacityExceeded, "enemyTypes", "A stage supports at most eight enemy types."};
    }
    ShmupEnemyType type{};
    if (!onlyKeys(value, {"id", "health", "width", "height", "score", "motion", "attack", "color", "object"}) ||
        !readText(value, "id", type.id, true, false) ||
        !readText(value, "object", type.visual.object, false, true) ||
        findEnemyType(stage, type.id.data()) >= 0 ||
        !readInt(value, "health", type.health, 1, 99) ||
        !readNumber(value, "width", type.width, 8.0F, 128.0F) ||
        !readNumber(value, "height", type.height, 8.0F, 128.0F) ||
        !readInt(value, "score", type.score, 0, 10000) || !readColor(value, "color", type.color) ||
        !readMotion(value.find("motion"), false, type.motion)) {
        return bad("enemyTypes", "Enemy types need a unique id, bounded stats and a valid motion.");
    }
    if (value.find("attack").valid() && !readAttack(value.find("attack"), type.attack)) {
        return bad("enemyTypes", "Enemy attack settings are invalid.");
    }
    stage.enemyTypes[stage.enemyTypeCount++] = type;
    return Status::success();
}

[[nodiscard]] Status parseBoss(JsonValue value, ShmupStage& stage) noexcept {
    ShmupBoss& boss = stage.boss;
    const JsonValue phases = value.find("phases");
    if (!onlyKeys(value, {"name", "health", "width", "height", "spawnTime", "y", "color", "phases",
                          "object", "hitAnimation"}) ||
        !readText(value, "name", boss.name, true, false) ||
        !readText(value, "object", boss.visual.object, false, true) ||
        !readText(value, "hitAnimation", boss.hitAnimation, false, false) ||
        !readInt(value, "health", boss.health, 1, 999) ||
        !readNumber(value, "width", boss.width, 16.0F, 200.0F) ||
        !readNumber(value, "height", boss.height, 16.0F, 200.0F) ||
        !readNumber(value, "spawnTime", boss.spawnTime, 0.0F, 600.0F) ||
        !readNumber(value, "y", boss.y, kPlayfieldTop, kShmupHeight) ||
        !readColor(value, "color", boss.color) || !phases.isArray() || phases.size() == 0 ||
        phases.size() > kMaxShmupPhases) {
        return bad("boss", "A boss needs bounded stats and one to four phases.");
    }
    float previous = 2.0F;
    for (std::size_t index = 0; index < phases.size(); ++index) {
        const JsonValue entry = phases.at(index);
        ShmupBossPhase phase{};
        if (!onlyKeys(entry, {"healthBelow", "transitionSeconds", "motion", "attacks", "summon",
                              "animation"}) ||
            !readText(entry, "animation", phase.animation, false, false) ||
            !readNumber(entry, "healthBelow", phase.healthBelow, 0.05F, 1.0F) ||
            !readNumber(entry, "transitionSeconds", phase.transitionSeconds, 0.0F, 5.0F) ||
            !readMotion(entry.find("motion"), true, phase.motion)) {
            return bad("boss.phases", "A boss phase has invalid settings.");
        }
        if ((index == 0 && phase.healthBelow != 1.0F) || phase.healthBelow >= previous) {
            return bad("boss.phases",
                       "Phases must start at healthBelow 1 and strictly decrease.");
        }
        previous = phase.healthBelow;
        const JsonValue attacks = entry.find("attacks");
        if (!attacks.isArray() || attacks.size() > kMaxShmupAttacks) {
            return bad("boss.phases", "A phase lists up to three attacks.");
        }
        for (std::size_t attack = 0; attack < attacks.size(); ++attack) {
            if (!readAttack(attacks.at(attack), phase.attacks[attack])) {
                return bad("boss.phases", "A boss attack has invalid settings.");
            }
            ++phase.attackCount;
        }
        const JsonValue summon = entry.find("summon");
        if (summon.valid()) {
            std::array<char, 32> enemy{};
            if (!onlyKeys(summon, {"enemy", "count", "interval"}) ||
                !readText(summon, "enemy", enemy, true, false) ||
                !readInt(summon, "count", phase.summonCount, 1, 6) ||
                !readNumber(summon, "interval", phase.summonInterval, 1.0F, 30.0F)) {
                return bad("boss.phases", "A phase summon has invalid settings.");
            }
            phase.summonType = findEnemyType(stage, enemy.data());
            if (phase.summonType < 0) {
                return bad("boss.phases", "A phase summons an unknown enemy type.");
            }
        }
        boss.phases[boss.phaseCount++] = phase;
    }
    return Status::success();
}

[[nodiscard]] Status parseStage(JsonValue value, ShmupStage& stage) noexcept {
    if (!onlyKeys(value, {"version", "id", "name", "background", "scrollSpeed", "player", "completionFact",
                          "rewardItem", "enemyTypes", "waves", "boss", "enemyShotObject", "layers",
                          "obstacles"}) ||
        !readText(value, "enemyShotObject", stage.enemyShot.object, false, true) ||
        !readText(value, "id", stage.id, true, true) ||
        !readText(value, "name", stage.name, true, false) ||
        !readColor(value, "background", stage.background) ||
        !readNumber(value, "scrollSpeed", stage.scrollSpeed, 0.0F, 400.0F) ||
        !readText(value, "completionFact", stage.completionFact, false, true) ||
        !readText(value, "rewardItem", stage.rewardItem, false, true)) {
        return bad("stages", "A stage needs a valid id, name and bounded settings.");
    }
    const JsonValue player = value.find("player");
    if (player.valid()) {
        ShmupPlayerSpec& spec = stage.player;
        if (!onlyKeys(player, {"health", "speed", "width", "height", "fireInterval", "bulletSpeed",
                               "bulletDamage", "invulnerability", "object", "shotObject"}) ||
            !readText(player, "object", spec.visual.object, false, true) ||
            !readText(player, "shotObject", spec.shot.object, false, true) ||
            !readInt(player, "health", spec.health, 1, 12) ||
            !readNumber(player, "speed", spec.speed, 40.0F, 400.0F) ||
            !readNumber(player, "width", spec.width, 8.0F, 64.0F) ||
            !readNumber(player, "height", spec.height, 6.0F, 64.0F) ||
            !readNumber(player, "fireInterval", spec.fireInterval, 0.04F, 2.0F) ||
            !readNumber(player, "bulletSpeed", spec.bulletSpeed, 100.0F, 900.0F) ||
            !readInt(player, "bulletDamage", spec.bulletDamage, 1, 20) ||
            !readNumber(player, "invulnerability", spec.invulnerability, 0.0F, 5.0F)) {
            return bad("stages.player", "The player ship settings are invalid.");
        }
    }
    const JsonValue layers = value.find("layers");
    if (layers.valid()) {
        if (!layers.isArray() || layers.size() > kMaxShmupLayers) {
            return bad("stages.layers", "A stage lists at most four background layers.");
        }
        for (std::size_t index = 0; index < layers.size(); ++index) {
            const JsonValue entry = layers.at(index);
            ShmupLayer layer{};
            if (!onlyKeys(entry, {"object", "scrollFactor", "y"}) ||
                !readText(entry, "object", layer.visual.object, true, true) ||
                !readNumber(entry, "scrollFactor", layer.scrollFactor, 0.0F, 4.0F) ||
                !readNumber(entry, "y", layer.y, 0.0F, kShmupHeight)) {
                return bad("stages.layers", "A background layer needs an object and bounded settings.");
            }
            stage.layers[stage.layerCount++] = layer;
        }
    }
    const JsonValue obstacles = value.find("obstacles");
    if (obstacles.valid()) {
        if (!obstacles.isArray() || obstacles.size() > kMaxShmupObstacles) {
            return bad("stages.obstacles", "A stage lists at most 32 obstacles.");
        }
        for (std::size_t index = 0; index < obstacles.size(); ++index) {
            const JsonValue entry = obstacles.at(index);
            ShmupObstacle obstacle{};
            std::string_view kind;
            if (!onlyKeys(entry, {"kind", "object", "x", "y", "width", "height", "health", "score"}) ||
                !entry.find("kind").string(kind) ||
                !readText(entry, "object", obstacle.visual.object, false, true) ||
                !readNumber(entry, "x", obstacle.x, 0.0F, 50000.0F) ||
                !readNumber(entry, "y", obstacle.y, 0.0F, kShmupHeight) ||
                !readNumber(entry, "width", obstacle.width, 8.0F, 640.0F) ||
                !readNumber(entry, "height", obstacle.height, 8.0F, 360.0F) ||
                !readInt(entry, "health", obstacle.health, 1, 99) ||
                !readInt(entry, "score", obstacle.score, 0, 10000) ||
                obstacle.y + obstacle.height > kShmupHeight) {
                return bad("stages.obstacles", "An obstacle needs a kind and bounded geometry.");
            }
            if (kind == "wall") obstacle.kind = ShmupObstacleKind::Wall;
            else if (kind == "debris") obstacle.kind = ShmupObstacleKind::Debris;
            else return bad("stages.obstacles", "Obstacle kind must be wall or debris.");
            stage.obstacles[stage.obstacleCount++] = obstacle;
        }
    }
    const JsonValue types = value.find("enemyTypes");
    if (!types.isArray() || types.size() == 0) {
        return bad("stages.enemyTypes", "A stage needs at least one enemy type.");
    }
    for (std::size_t index = 0; index < types.size(); ++index) {
        const Status status = parseEnemyType(types.at(index), stage);
        if (!status) return status;
    }
    const JsonValue waves = value.find("waves");
    if (!waves.isArray() || waves.size() > kMaxShmupWaves) {
        return bad("stages.waves", "A stage lists at most 24 waves.");
    }
    for (std::size_t index = 0; index < waves.size(); ++index) {
        const JsonValue entry = waves.at(index);
        ShmupWave wave{};
        std::array<char, 32> enemy{};
        if (!onlyKeys(entry, {"time", "enemy", "count", "y", "yStep", "spacing"}) ||
            !readText(entry, "enemy", enemy, true, false) ||
            !readNumber(entry, "time", wave.time, 0.0F, 600.0F) ||
            !readInt(entry, "count", wave.count, 1, 12) ||
            !readNumber(entry, "y", wave.y, kPlayfieldTop, kShmupHeight) ||
            !readNumber(entry, "yStep", wave.yStep, -200.0F, 200.0F) ||
            !readNumber(entry, "spacing", wave.spacing, 0.1F, 5.0F)) {
            return bad("stages.waves", "A wave has invalid settings.");
        }
        const int type = findEnemyType(stage, enemy.data());
        if (type < 0) return bad("stages.waves", "A wave references an unknown enemy type.");
        wave.type = static_cast<std::uint8_t>(type);
        stage.waves[stage.waveCount++] = wave;
    }
    const JsonValue boss = value.find("boss");
    if (!boss.valid()) return bad("stages.boss", "A stage needs a boss definition.");
    return parseBoss(boss, stage);
}

[[nodiscard]] bool overlap(float ax, float ay, float aw, float ah, float bx, float by, float bw,
                           float bh) noexcept {
    return ax < bx + bw && ax + aw > bx && ay < by + bh && ay + ah > by;
}

template <std::size_t N>
[[nodiscard]] ShmupBullet* freeBullet(std::array<ShmupBullet, N>& pool) noexcept {
    for (ShmupBullet& bullet : pool) {
        if (!bullet.active) return &bullet;
    }
    return nullptr;
}

} // namespace

const ShmupStage* ShmupLibrary::find(std::string_view id) const noexcept {
    const int index = indexOf(id);
    return index < 0 ? nullptr : &stages[static_cast<std::size_t>(index)];
}

int ShmupLibrary::indexOf(std::string_view id) const noexcept {
    for (std::size_t index = 0; index < stageCount; ++index) {
        if (stages[index].idText() == id) return static_cast<int>(index);
    }
    return -1;
}

Status parseShmupLibrary(JsonValue root, ShmupLibrary& output) noexcept {
    output = {};
    std::int64_t version = 0;
    const JsonValue stages = root.find("stages");
    if (!onlyKeys(root, {"version", "stages"}) || !root.find("version").integer(version) ||
        version != 1 || !stages.isArray() || stages.size() > kMaxShmupStages) {
        return bad("shmup", "SHMUP content needs version 1 and at most four stages.");
    }
    for (std::size_t index = 0; index < stages.size(); ++index) {
        ShmupStage stage{};
        const Status status = parseStage(stages.at(index), stage);
        if (!status) {
            output = {};
            return status;
        }
        if (output.indexOf(stage.idText()) >= 0) {
            output = {};
            return {ErrorCode::DuplicateId, "stages", "SHMUP stage IDs must be unique."};
        }
        output.stages[output.stageCount++] = stage;
    }
    return Status::success();
}

Status parseShmupStageFile(JsonValue root, ShmupLibrary& library) noexcept {
    std::int64_t version = 0;
    if (!root.find("version").integer(version) || version != 1) {
        return bad("shmup", "A SHMUP stage file needs version 1.");
    }
    if (library.stageCount == kMaxShmupStages) {
        return {ErrorCode::CapacityExceeded, "shmup", "At most four SHMUP stages are supported."};
    }
    ShmupStage& slot = library.stages[library.stageCount];
    slot = {};
    const Status status = parseStage(root, slot);
    if (!status) {
        slot = {};
        return status;
    }
    if (library.indexOf(slot.idText()) >= 0) {
        slot = {};
        return {ErrorCode::DuplicateId, "stages", "SHMUP stage IDs must be unique."};
    }
    ++library.stageCount;
    return Status::success();
}

Status resolveShmupObjects(const framework::WorldModule& world, ShmupLibrary& library) noexcept {
    const auto bind = [&](ShmupVisual& visual) -> Status {
        const std::string_view id = visual.object.data();
        if (id.empty()) return Status::success();
        visual.archetype = world.findObject(id);
        if (visual.archetype == nullptr || visual.archetype->isAbstract()) {
            return {ErrorCode::MissingDependency, id, "A SHMUP stage references an unknown world object."};
        }
        return Status::success();
    };
    const auto animationExists = [](const ShmupVisual& visual, const std::array<char, 24>& name) {
        const std::string_view text = name.data();
        return text.empty() || visual.archetype == nullptr ||
               visual.archetype->findAnimation(text) != nullptr;
    };
    for (std::size_t index = 0; index < library.stageCount; ++index) {
        ShmupStage& stage = library.stages[index];
        std::array<ShmupVisual*, 4> singles{&stage.player.visual, &stage.player.shot, &stage.enemyShot,
                                            &stage.boss.visual};
        for (ShmupVisual* visual : singles) {
            if (const Status status = bind(*visual); !status) return status;
        }
        for (std::size_t type = 0; type < stage.enemyTypeCount; ++type) {
            if (const Status status = bind(stage.enemyTypes[type].visual); !status) return status;
        }
        for (std::size_t layer = 0; layer < stage.layerCount; ++layer) {
            if (const Status status = bind(stage.layers[layer].visual); !status) return status;
        }
        for (std::size_t obstacle = 0; obstacle < stage.obstacleCount; ++obstacle) {
            ShmupObstacle& entry = stage.obstacles[obstacle];
            if (const Status status = bind(entry.visual); !status) return status;
            if (entry.visual.archetype == nullptr) continue;
            const framework::SpriteSheetSpec& sprite = entry.visual.archetype->sprite();
            if (sprite.frameWidth <= 0 || sprite.frameHeight <= 0 ||
                static_cast<int>(entry.width) % sprite.frameWidth != 0 ||
                static_cast<int>(entry.height) % sprite.frameHeight != 0) {
                return bad("stages.obstacles",
                           "Obstacle width and height must be multiples of the object frame size.");
            }
        }
        if (!animationExists(stage.boss.visual, stage.boss.hitAnimation)) {
            return bad("boss.hitAnimation", "The boss object has no animation with that name.");
        }
        for (std::size_t phase = 0; phase < stage.boss.phaseCount; ++phase) {
            if (!animationExists(stage.boss.visual, stage.boss.phases[phase].animation)) {
                return bad("boss.phases.animation", "The boss object has no animation with that name.");
            }
        }
    }
    return Status::success();
}

void ShmupSession::begin(const ShmupStage& stage) noexcept {
    *this = {};
    stage_ = &stage;
    health_ = stage.player.health;
    for (std::size_t index = 0; index < stage.obstacleCount; ++index) {
        obstacleHealth_[index] = stage.obstacles[index].health;
    }
    ship_ = {48.0F, kShmupHeight * 0.5F - stage.player.height * 0.5F};
}

float ShmupSession::bossHealthFraction() const noexcept {
    if (stage_ == nullptr) return 0.0F;
    if (boss_.defeated) return 0.0F;
    if (!boss_.active) return 1.0F;
    return static_cast<float>(boss_.health) / static_cast<float>(stage_->boss.health);
}

void ShmupSession::step(const ShmupInput& input, float dt) noexcept {
    if (stage_ == nullptr || result_ != ShmupResult::Running) return;
    const ShmupPlayerSpec& spec = stage_->player;
    time_ += dt;
    scroll_ += stage_->scrollSpeed * dt;
    invulnerable_ = std::max(0.0F, invulnerable_ - dt);
    fireCooldown_ = std::max(0.0F, fireCooldown_ - dt);

    float dx = static_cast<float>(std::clamp(input.moveX, -1, 1));
    float dy = static_cast<float>(std::clamp(input.moveY, -1, 1));
    if (dx != 0.0F && dy != 0.0F) {
        dx *= 0.70710678F;
        dy *= 0.70710678F;
    }
    ship_.x = std::clamp(ship_.x + dx * spec.speed * dt, 0.0F, kShmupWidth - spec.width);
    ship_.y = std::clamp(ship_.y + dy * spec.speed * dt, kPlayfieldTop, kShmupHeight - spec.height);

    if (input.shoot && fireCooldown_ <= 0.0F) {
        if (ShmupBullet* shot = freeBullet(playerShots_)) {
            *shot = {true, ship_.x + spec.width, ship_.y + spec.height * 0.5F - 2.0F,
                     spec.bulletSpeed, 0.0F, 5.0F};
            ++shotsFired_;
            fireCooldown_ = spec.fireInterval;
        }
    }

    spawnWaves();
    updateEnemies(dt);
    updateBoss(dt);
    updateShots(dt);
    resolveHits();
    if (health_ <= 0) {
        result_ = ShmupResult::Failed;
    } else if (boss_.defeated) {
        result_ = ShmupResult::Cleared;
    }
}

void ShmupSession::spawnWaves() noexcept {
    for (std::size_t index = 0; index < stage_->waveCount; ++index) {
        const ShmupWave& wave = stage_->waves[index];
        while (waveSpawned_[index] < wave.count &&
               time_ >= wave.time + static_cast<float>(waveSpawned_[index]) * wave.spacing) {
            const float y = wave.y + static_cast<float>(waveSpawned_[index]) * wave.yStep;
            if (!spawnEnemy(wave.type, y)) break;
            ++waveSpawned_[index];
        }
    }
    if (!boss_.active && !boss_.defeated && time_ >= stage_->boss.spawnTime) {
        boss_.active = true;
        boss_.health = stage_->boss.health;
        boss_.x = kShmupWidth;
        boss_.y = stage_->boss.y;
        boss_.phase = 0;
        for (std::size_t attack = 0; attack < kMaxShmupAttacks; ++attack) {
            boss_.attackTimers[attack] = stage_->boss.phases[0].attacks[attack].delay;
        }
        boss_.summonTimer = stage_->boss.phases[0].summonInterval;
    }
}

bool ShmupSession::spawnEnemy(std::size_t type, float y) noexcept {
    const ShmupEnemyType& definition = stage_->enemyTypes[type];
    for (ShmupEnemyState& enemy : enemies_) {
        if (enemy.active) continue;
        const float clamped =
            std::clamp(y, kPlayfieldTop, kShmupHeight - definition.height);
        enemy = {true, static_cast<std::uint8_t>(type), kShmupWidth, clamped, clamped, 0.0F,
                 definition.attack.delay, 0.0F, definition.health, false};
        return true;
    }
    return false;
}

void ShmupSession::addEnemyShot(float x, float y, float vx, float vy, float size) noexcept {
    if (ShmupBullet* bullet = freeBullet(enemyShots_)) *bullet = {true, x, y, vx, vy, size};
}

void ShmupSession::fire(const ShmupAttack& attack, float x, float y, int& counter) noexcept {
    const float centerX = ship_.x + stage_->player.width * 0.5F;
    const float centerY = ship_.y + stage_->player.height * 0.5F;
    const auto emit = [&](float angle) noexcept {
        addEnemyShot(x, y, std::cos(angle) * attack.bulletSpeed, std::sin(angle) * attack.bulletSpeed,
                     attack.bulletSize);
    };
    const float centered = attack.count > 1 ? static_cast<float>(attack.count - 1) * 0.5F : 0.0F;
    switch (attack.kind) {
    case ShmupAttackKind::None:
        break;
    case ShmupAttackKind::Aimed: {
        const float base = std::atan2(centerY - y, centerX - x);
        for (int index = 0; index < attack.count; ++index) {
            emit(base + (static_cast<float>(index) - centered) * attack.spread);
        }
        break;
    }
    case ShmupAttackKind::Spread:
        for (int index = 0; index < attack.count; ++index) {
            emit(kPi + (static_cast<float>(index) - centered) * attack.spread);
        }
        break;
    case ShmupAttackKind::Stream:
        emit(kPi);
        break;
    case ShmupAttackKind::Radial:
        for (int index = 0; index < attack.count; ++index) {
            emit(2.0F * kPi * static_cast<float>(index) / static_cast<float>(attack.count) +
                 static_cast<float>(counter) * 0.31F);
        }
        ++counter;
        break;
    case ShmupAttackKind::Wall: {
        const float slot = (kShmupHeight - kPlayfieldTop) / static_cast<float>(attack.count);
        const int gap = counter % attack.count;
        for (int index = 0; index < attack.count; ++index) {
            if (index == gap || index == (gap + 1) % attack.count) continue;
            addEnemyShot(x, kPlayfieldTop + slot * (static_cast<float>(index) + 0.5F),
                         -attack.bulletSpeed, 0.0F, attack.bulletSize);
        }
        counter += 3;
        break;
    }
    }
}

void ShmupSession::updateEnemies(float dt) noexcept {
    for (ShmupEnemyState& enemy : enemies_) {
        if (!enemy.active) continue;
        const ShmupEnemyType& type = stage_->enemyTypes[enemy.type];
        const ShmupMotion& motion = type.motion;
        enemy.age += dt;
        enemy.flash = std::max(0.0F, enemy.flash - dt);
        switch (motion.kind) {
        case ShmupMotionKind::Sine:
            enemy.x -= motion.speed * dt;
            enemy.y = enemy.baseY + std::sin(enemy.age * motion.frequency) * motion.amplitude;
            break;
        case ShmupMotionKind::Dive: {
            enemy.x -= motion.speed * dt;
            const float trigger = motion.amplitude > 0.0F ? motion.amplitude : 240.0F;
            if (enemy.x - ship_.x < trigger) enemy.diving = true;
            if (enemy.diving) {
                const float limit = motion.speed * 1.5F * dt;
                enemy.y += std::clamp(ship_.y - enemy.y, -limit, limit);
            }
            break;
        }
        case ShmupMotionKind::Hold:
            if (enemy.x > motion.anchorX) {
                enemy.x = std::max(motion.anchorX, enemy.x - motion.speed * dt);
            }
            enemy.y = enemy.baseY + std::sin(enemy.age * motion.frequency) * motion.amplitude;
            break;
        default:
            enemy.x -= motion.speed * dt;
            break;
        }
        enemy.y = std::clamp(enemy.y, kPlayfieldTop, kShmupHeight - type.height);
        if (enemy.x + type.width < -8.0F) {
            enemy.active = false;
            continue;
        }
        if (type.attack.kind != ShmupAttackKind::None && enemy.x < kShmupWidth - 8.0F) {
            enemy.attackTimer -= dt;
            if (enemy.attackTimer <= 0.0F) {
                int counter = static_cast<int>(enemy.age * 10.0F);
                fire(type.attack, enemy.x, enemy.y + type.height * 0.5F, counter);
                enemy.attackTimer += type.attack.interval;
            }
        }
    }
}

void ShmupSession::advanceBossPhase() noexcept {
    const ShmupBoss& boss = stage_->boss;
    ++boss_.phase;
    const ShmupBossPhase& phase = boss.phases[boss_.phase];
    boss_.transitionLeft = phase.transitionSeconds;
    boss_.phaseAge = 0.0F;
    boss_.wallStep = 0;
    boss_.summonTimer = phase.summonInterval;
    for (std::size_t attack = 0; attack < kMaxShmupAttacks; ++attack) {
        boss_.attackTimers[attack] = phase.attacks[attack].delay;
    }
    for (ShmupBullet& bullet : enemyShots_) bullet.active = false;
}

void ShmupSession::updateBoss(float dt) noexcept {
    if (!boss_.active) return;
    const ShmupBoss& boss = stage_->boss;
    const ShmupBossPhase& phase = boss.phases[boss_.phase];
    const ShmupMotion& motion = phase.motion;
    boss_.age += dt;
    boss_.flash = std::max(0.0F, boss_.flash - dt);
    if (!boss_.arrived) {
        boss_.x -= kBossEntrySpeed * dt;
        if (boss_.x <= motion.anchorX) {
            boss_.x = motion.anchorX;
            boss_.arrived = true;
        }
        return;
    }
    boss_.phaseAge += dt;
    // Phases may move the anchor; glide to it instead of snapping.
    const float glide = kBossEntrySpeed * dt;
    boss_.x += std::clamp(motion.anchorX - boss_.x, -glide, glide);
    const float range = kShmupHeight - kPlayfieldTop - boss.height;
    switch (motion.kind) {
    case ShmupMotionKind::Patrol: {
        const float travel = std::fmod(boss_.phaseAge * motion.speed, 2.0F * range);
        boss_.y = kPlayfieldTop + (travel < range ? travel : 2.0F * range - travel);
        break;
    }
    case ShmupMotionKind::Track: {
        const float target = ship_.y + stage_->player.height * 0.5F - boss.height * 0.5F;
        const float limit = motion.speed * dt;
        boss_.y += std::clamp(target - boss_.y, -limit, limit);
        break;
    }
    default:
        boss_.y = boss.y + std::sin(boss_.phaseAge * motion.frequency) * motion.amplitude;
        break;
    }
    boss_.y = std::clamp(boss_.y, kPlayfieldTop, kShmupHeight - boss.height);
    if (boss_.transitionLeft > 0.0F) {
        boss_.transitionLeft = std::max(0.0F, boss_.transitionLeft - dt);
        return;
    }
    const float originX = boss_.x;
    const float originY = boss_.y + boss.height * 0.5F;
    for (std::size_t index = 0; index < phase.attackCount; ++index) {
        boss_.attackTimers[index] -= dt;
        if (boss_.attackTimers[index] <= 0.0F) {
            fire(phase.attacks[index], originX, originY, boss_.wallStep);
            boss_.attackTimers[index] += phase.attacks[index].interval;
        }
    }
    if (phase.summonType >= 0) {
        boss_.summonTimer -= dt;
        if (boss_.summonTimer <= 0.0F) {
            const float spacing = (kShmupHeight - kPlayfieldTop - 40.0F) /
                                  static_cast<float>(phase.summonCount);
            for (int index = 0; index < phase.summonCount; ++index) {
                (void)spawnEnemy(static_cast<std::size_t>(phase.summonType),
                                 kPlayfieldTop + 10.0F + spacing * (static_cast<float>(index) + 0.5F));
            }
            boss_.summonTimer += phase.summonInterval;
        }
    }
}

void ShmupSession::updateShots(float dt) noexcept {
    const auto advance = [dt](auto& pool) noexcept {
        for (ShmupBullet& bullet : pool) {
            if (!bullet.active) continue;
            bullet.x += bullet.vx * dt;
            bullet.y += bullet.vy * dt;
            if (bullet.x < -24.0F || bullet.x > kShmupWidth + 24.0F || bullet.y < -24.0F ||
                bullet.y > kShmupHeight + 24.0F) {
                bullet.active = false;
            }
        }
    };
    advance(playerShots_);
    advance(enemyShots_);
}

void ShmupSession::hurtPlayer() noexcept {
    if (invulnerable_ > 0.0F) return;
    --health_;
    ++hitsTaken_;
    invulnerable_ = stage_->player.invulnerability;
}

void ShmupSession::resolveHits() noexcept {
    const ShmupPlayerSpec& spec = stage_->player;
    const ShmupBoss& boss = stage_->boss;
    for (ShmupBullet& shot : playerShots_) {
        if (!shot.active) continue;
        for (std::size_t index = 0; index < stage_->obstacleCount && shot.active; ++index) {
            const ShmupObstacle& obstacle = stage_->obstacles[index];
            if (!obstacleAlive(index) ||
                !overlap(shot.x, shot.y, shot.size * 2.0F, shot.size, obstacleX(index), obstacle.y,
                         obstacle.width, obstacle.height)) {
                continue;
            }
            shot.active = false;
            if (obstacle.kind == ShmupObstacleKind::Debris &&
                (obstacleHealth_[index] -= spec.bulletDamage) <= 0) {
                obstacleHealth_[index] = 0;
                ++kills_;
                score_ += obstacle.score;
            }
        }
        if (!shot.active) continue;
        for (ShmupEnemyState& enemy : enemies_) {
            if (!enemy.active) continue;
            const ShmupEnemyType& type = stage_->enemyTypes[enemy.type];
            if (!overlap(shot.x, shot.y, shot.size * 2.0F, shot.size, enemy.x, enemy.y, type.width,
                         type.height)) {
                continue;
            }
            shot.active = false;
            enemy.health -= spec.bulletDamage;
            enemy.flash = 0.08F;
            ++hitsLanded_;
            if (enemy.health <= 0) {
                enemy.active = false;
                ++kills_;
                score_ += type.score;
            }
            break;
        }
        if (!shot.active || !boss_.active || boss_.defeated || !boss_.arrived ||
            boss_.transitionLeft > 0.0F ||
            !overlap(shot.x, shot.y, shot.size * 2.0F, shot.size, boss_.x, boss_.y, boss.width,
                     boss.height)) {
            continue;
        }
        shot.active = false;
        boss_.health -= spec.bulletDamage;
        boss_.flash = 0.08F;
        ++hitsLanded_;
        if (boss_.health <= 0) {
            boss_.health = 0;
            boss_.active = false;
            boss_.defeated = true;
            ++kills_;
            score_ += 1000;
            for (ShmupBullet& bullet : enemyShots_) bullet.active = false;
            for (ShmupEnemyState& enemy : enemies_) enemy.active = false;
            return;
        }
        if (boss_.phase + 1 < boss.phaseCount &&
            bossHealthFraction() <= boss.phases[boss_.phase + 1].healthBelow) {
            advanceBossPhase();
        }
    }

    constexpr float kHitInset = 3.0F;
    const float hitX = ship_.x + kHitInset;
    const float hitY = ship_.y + kHitInset;
    const float hitW = spec.width - 2.0F * kHitInset;
    const float hitH = spec.height - 2.0F * kHitInset;
    for (ShmupBullet& bullet : enemyShots_) {
        if (!bullet.active) continue;
        for (std::size_t index = 0; index < stage_->obstacleCount; ++index) {
            const ShmupObstacle& obstacle = stage_->obstacles[index];
            if (obstacleAlive(index) &&
                overlap(bullet.x - bullet.size * 0.5F, bullet.y - bullet.size * 0.5F, bullet.size,
                        bullet.size, obstacleX(index), obstacle.y, obstacle.width, obstacle.height)) {
                bullet.active = false;
                break;
            }
        }
        if (bullet.active &&
            overlap(bullet.x - bullet.size * 0.5F, bullet.y - bullet.size * 0.5F, bullet.size,
                    bullet.size, hitX, hitY, hitW, hitH)) {
            bullet.active = false;
            hurtPlayer();
        }
    }
    for (std::size_t index = 0; index < stage_->obstacleCount; ++index) {
        const ShmupObstacle& obstacle = stage_->obstacles[index];
        if (obstacleAlive(index) &&
            overlap(obstacleX(index), obstacle.y, obstacle.width, obstacle.height, hitX, hitY, hitW,
                    hitH)) {
            hurtPlayer();
        }
    }
    for (const ShmupEnemyState& enemy : enemies_) {
        const ShmupEnemyType& type = stage_->enemyTypes[enemy.type];
        if (enemy.active &&
            overlap(enemy.x, enemy.y, type.width, type.height, hitX, hitY, hitW, hitH)) {
            hurtPlayer();
        }
    }
    if (boss_.active && boss_.arrived &&
        overlap(boss_.x, boss_.y, boss.width, boss.height, hitX, hitY, hitW, hitH)) {
        hurtPlayer();
    }
}

} // namespace echo
