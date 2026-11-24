#pragma once

#include "echo/framework/core.hpp"
#include "echo/framework/data.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace echo {

// Fixed capacities keep the SHMUP mode free of dynamic allocation.
inline constexpr std::size_t kMaxShmupStages = 4;
inline constexpr std::size_t kMaxShmupEnemyTypes = 8;
inline constexpr std::size_t kMaxShmupWaves = 24;
inline constexpr std::size_t kMaxShmupPhases = 4;
inline constexpr std::size_t kMaxShmupAttacks = 3;
inline constexpr std::size_t kMaxShmupEnemies = 24;
inline constexpr std::size_t kMaxShmupPlayerShots = 16;
inline constexpr std::size_t kMaxShmupEnemyShots = 80;
inline constexpr float kShmupWidth = 640.0F;
inline constexpr float kShmupHeight = 360.0F;

// How an enemy or boss moves; the kind is chosen in JSON and parameterised there.
enum class ShmupMotionKind : std::uint8_t {
    Straight, // drifts left at `speed`
    Sine,     // drifts left while bobbing vertically
    Dive,     // drifts left, then homes on the player's height once near
    Hold,     // flies in to `anchorX`, then bobs in place
    Patrol,   // bosses: sweeps between the top and bottom of the playfield
    Track,    // bosses: follows the player's height at `speed`
};

enum class ShmupAttackKind : std::uint8_t {
    None,
    Aimed,  // `count` shots toward the player, fanned by `spread`
    Spread, // `count` shots fanned around the leftward direction
    Stream, // one shot straight left
    Radial, // `count` shots evenly around a full circle
    Wall,   // a vertical column of `count` shots with a moving gap
};

struct ShmupMotion {
    ShmupMotionKind kind{ShmupMotionKind::Straight};
    float speed{60.0F};
    float amplitude{0.0F};
    float frequency{0.0F};
    float anchorX{480.0F};
};

struct ShmupAttack {
    ShmupAttackKind kind{ShmupAttackKind::None};
    float interval{1.5F};
    float delay{0.5F};
    int count{1};
    float spread{0.0F}; // radians
    float bulletSpeed{150.0F};
    float bulletSize{6.0F};
};

struct ShmupEnemyType {
    std::array<char, 32> id{};
    int health{1};
    float width{24.0F};
    float height{18.0F};
    int score{10};
    ShmupMotion motion{};
    ShmupAttack attack{};
    std::array<std::uint8_t, 3> color{220, 90, 80};
};

struct ShmupWave {
    float time{};
    std::uint8_t type{};
    int count{1};
    float y{};
    float yStep{};
    float spacing{0.4F};
};

struct ShmupBossPhase {
    // The phase starts once boss health drops to this fraction of its maximum.
    float healthBelow{1.0F};
    float transitionSeconds{1.0F};
    ShmupMotion motion{};
    std::array<ShmupAttack, kMaxShmupAttacks> attacks{};
    std::size_t attackCount{};
    // Optional adds summoned during the phase; type index or -1 for none.
    int summonType{-1};
    int summonCount{1};
    float summonInterval{4.0F};
};

struct ShmupBoss {
    std::array<char, 32> name{};
    int health{40};
    float width{96.0F};
    float height{96.0F};
    float spawnTime{30.0F};
    float y{132.0F};
    std::array<std::uint8_t, 3> color{150, 70, 200};
    std::array<ShmupBossPhase, kMaxShmupPhases> phases{};
    std::size_t phaseCount{};
};

struct ShmupPlayerSpec {
    int health{5};
    float speed{200.0F};
    float width{26.0F};
    float height{14.0F};
    float fireInterval{0.14F};
    float bulletSpeed{460.0F};
    int bulletDamage{1};
    float invulnerability{1.2F};
};

struct ShmupStage {
    std::array<char, 96> id{};
    std::array<char, 32> name{};
    std::array<std::uint8_t, 3> background{6, 8, 20};
    float scrollSpeed{70.0F};
    ShmupPlayerSpec player{};
    // Fact set when the boss falls, and the optional reward item granted once.
    std::array<char, 96> completionFact{};
    std::array<char, 96> rewardItem{};
    std::array<ShmupEnemyType, kMaxShmupEnemyTypes> enemyTypes{};
    std::size_t enemyTypeCount{};
    std::array<ShmupWave, kMaxShmupWaves> waves{};
    std::size_t waveCount{};
    ShmupBoss boss{};

    [[nodiscard]] std::string_view idText() const noexcept { return id.data(); }
};

struct ShmupLibrary {
    std::array<ShmupStage, kMaxShmupStages> stages{};
    std::size_t stageCount{};

    [[nodiscard]] const ShmupStage* find(std::string_view id) const noexcept;
    [[nodiscard]] int indexOf(std::string_view id) const noexcept;
};

// Parses and strictly validates a stage library (assets/shmup/stages.json).
[[nodiscard]] framework::Status parseShmupLibrary(framework::JsonValue root,
                                                  ShmupLibrary& output) noexcept;

struct ShmupInput {
    int moveX{};
    int moveY{};
    bool shoot{};
};

enum class ShmupResult : std::uint8_t { Running, Cleared, Failed };

struct ShmupShip {
    float x{};
    float y{};
};

struct ShmupEnemyState {
    bool active{};
    std::uint8_t type{};
    float x{};
    float y{};
    float baseY{};
    float age{};
    float attackTimer{};
    float flash{};
    int health{};
    bool diving{};
};

struct ShmupBullet {
    bool active{};
    float x{};
    float y{};
    float vx{};
    float vy{};
    float size{6.0F};
};

struct ShmupBossState {
    bool active{};
    bool defeated{};
    bool arrived{};
    int health{};
    std::size_t phase{};
    float phaseAge{};
    float transitionLeft{};
    float age{};
    float x{};
    float y{};
    float flash{};
    float summonTimer{};
    std::array<float, kMaxShmupAttacks> attackTimers{};
    int wallStep{};
};

// Deterministic, SDL-free SHMUP simulation driven entirely by a ShmupStage.
class ShmupSession {
public:
    void begin(const ShmupStage& stage) noexcept;
    void step(const ShmupInput& input, float deltaSeconds) noexcept;

    [[nodiscard]] bool active() const noexcept { return stage_ != nullptr; }
    [[nodiscard]] const ShmupStage* stage() const noexcept { return stage_; }
    [[nodiscard]] ShmupResult result() const noexcept { return result_; }
    [[nodiscard]] float time() const noexcept { return time_; }
    [[nodiscard]] float scroll() const noexcept { return scroll_; }
    [[nodiscard]] const ShmupShip& ship() const noexcept { return ship_; }
    [[nodiscard]] int health() const noexcept { return health_; }
    [[nodiscard]] float invulnerable() const noexcept { return invulnerable_; }
    [[nodiscard]] int score() const noexcept { return score_; }
    [[nodiscard]] const std::array<ShmupEnemyState, kMaxShmupEnemies>& enemies() const noexcept {
        return enemies_;
    }
    [[nodiscard]] const std::array<ShmupBullet, kMaxShmupPlayerShots>& playerShots() const noexcept {
        return playerShots_;
    }
    [[nodiscard]] const std::array<ShmupBullet, kMaxShmupEnemyShots>& enemyShots() const noexcept {
        return enemyShots_;
    }
    [[nodiscard]] const ShmupBossState& boss() const noexcept { return boss_; }
    [[nodiscard]] float bossHealthFraction() const noexcept;
    // Monotonic counters used by the platform layer to derive feedback.
    [[nodiscard]] std::uint32_t shotsFired() const noexcept { return shotsFired_; }
    [[nodiscard]] std::uint32_t hitsLanded() const noexcept { return hitsLanded_; }
    [[nodiscard]] std::uint32_t hitsTaken() const noexcept { return hitsTaken_; }
    [[nodiscard]] std::uint32_t kills() const noexcept { return kills_; }

private:
    void spawnWaves() noexcept;
    [[nodiscard]] bool spawnEnemy(std::size_t type, float y) noexcept;
    void fire(const ShmupAttack& attack, float x, float y, int& counter) noexcept;
    void addEnemyShot(float x, float y, float vx, float vy, float size) noexcept;
    void updateEnemies(float dt) noexcept;
    void updateBoss(float dt) noexcept;
    void updateShots(float dt) noexcept;
    void resolveHits() noexcept;
    void hurtPlayer() noexcept;
    void advanceBossPhase() noexcept;

    const ShmupStage* stage_{};
    ShmupResult result_{ShmupResult::Running};
    ShmupShip ship_{};
    ShmupBossState boss_{};
    std::array<ShmupEnemyState, kMaxShmupEnemies> enemies_{};
    std::array<ShmupBullet, kMaxShmupPlayerShots> playerShots_{};
    std::array<ShmupBullet, kMaxShmupEnemyShots> enemyShots_{};
    // Per-wave progress: how many ships of the wave have spawned.
    std::array<int, kMaxShmupWaves> waveSpawned_{};
    float time_{};
    float scroll_{};
    float fireCooldown_{};
    float invulnerable_{};
    int health_{};
    int score_{};
    std::uint32_t shotsFired_{};
    std::uint32_t hitsLanded_{};
    std::uint32_t hitsTaken_{};
    std::uint32_t kills_{};
};

} // namespace echo
