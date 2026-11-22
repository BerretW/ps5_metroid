#include "platform/sdl/assets.hpp"
#include "platform/sdl/bitmap_font.hpp"
#include "platform/sdl/framework_integration.hpp"
#include "platform/sdl/game_persistence.hpp"
#include "platform/sdl/gameplay_vfx.hpp"
#include "platform/sdl/renderer.hpp"
#include "platform/sdl/settings_persistence.hpp"

#include <SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {

int failures = 0;

float linearToSrgb(float value) noexcept {
    return value <= 0.0031308F ? value * 12.92F
                               : 1.055F * std::pow(value, 1.0F / 2.4F) - 0.055F;
}

void check(bool condition, std::string_view message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

void testFixedStepParticles() {
    echo::vfx::ParticleSystem first{};
    echo::vfx::ParticleSystem second{};
    echo::vfx::EmitterConfig config{};
    config.maximumParticles = 8;
    config.minimumLifetime = 0.5F;
    config.maximumLifetime = 0.5F;
    config.initialVelocity = {30.0F, 0.0F, 0.0F};
    config.randomSeed = 0x4543484FU;
    echo::vfx::EmitterHandle firstEmitter{};
    echo::vfx::EmitterHandle secondEmitter{};
    const bool created =
        static_cast<bool>(first.createEmitter(config, firstEmitter)) &&
        static_cast<bool>(second.createEmitter(config, secondEmitter));
    check(created, "fixed-step particle emitters initialize");
    if (!created) return;

    const bool prepared =
        static_cast<bool>(first.setPosition(firstEmitter, {10.0F, 20.0F, 0.0F})) &&
        static_cast<bool>(second.setPosition(secondEmitter, {10.0F, 20.0F, 0.0F})) &&
        static_cast<bool>(first.emitBurst(firstEmitter, 1)) &&
        static_cast<bool>(second.emitBurst(secondEmitter, 1));
    check(prepared, "fixed-step particle burst initializes at the requested origin");
    if (!prepared) return;

    const bool updated = static_cast<bool>(first.update(echo::kFixedStep)) &&
                         static_cast<bool>(second.update(echo::kFixedStep));
    const auto firstParticles = first.particles(firstEmitter);
    const auto secondParticles = second.particles(secondEmitter);
    check(updated && firstParticles.size() == 1 && secondParticles.size() == 1 &&
              firstParticles[0].age == echo::kFixedStep &&
              firstParticles[0].position.x == 10.0F + 30.0F * echo::kFixedStep &&
              firstParticles[0].position.x == secondParticles[0].position.x &&
              firstParticles[0].velocity.y == secondParticles[0].velocity.y,
          "fixed-step bursts advance deterministically by the shared simulation step");
}

void testDamageFlash() {
    echo::platform::DamageFlash flash{};
    check(flash.opacity() == 0, "damage flash starts invisible");
    flash.trigger();
    const auto initial = flash.opacity();
    flash.step();
    check(initial > 0 && flash.opacity() < initial && flash.opacity() > 0,
          "damage flash fades on a fixed simulation step");
    flash.trigger();
    check(flash.opacity() == initial, "repeated damage restarts the flash");
    for (unsigned index = 0; index < echo::platform::DamageFlash::kDurationSteps; ++index) {
        flash.step();
    }
    check(flash.opacity() == 0, "damage flash expires after its fixed duration");
    flash.trigger();
    flash.clear();
    check(flash.opacity() == 0, "reset clears the damage flash");
}

void testGameplayVfx() {
    using echo::platform::GameplayVfx;
    auto effects = std::make_unique<GameplayVfx>();
    auto duplicate = std::make_unique<GameplayVfx>();
    check(static_cast<bool>(effects->reset()) && static_cast<bool>(duplicate->reset()),
          "gameplay particle emitters initialize");
    GameplayVfx::Snapshot before{};
    before.room = echo::RoomId::MaintenanceTunnel;
    before.enemies[0].room = before.room;
    before.enemies[0].health = 2;
    before.enemies[0].bounds = {100.0F, 80.0F, 20.0F, 30.0F};
    before.roamerCount = 1;
    before.roamers[0].room = before.room;
    before.roamers[0].health = 3;
    before.roamers[0].bounds = {150.0F, 90.0F, 20.0F, 20.0F};
    auto after = before;
    after.enemies[0].health = 0;
    after.enemies[0].alive = false;
    after.roamers[0].health = 2;
    check(static_cast<bool>(effects->step(before, after)) &&
              static_cast<bool>(duplicate->step(before, after)) &&
              effects->impacts().size() == 24 && duplicate->impacts().size() == 24 &&
              effects->explosions().size() == 32 &&
              duplicate->explosions().size() == 32,
          "enemy defeat emits an explosion alongside ordinary impact bursts");
    if (effects->impacts().size() == 24 && duplicate->impacts().size() == 24) {
        check(effects->impacts()[0].position.x == 110.0F &&
                  effects->impacts()[0].position.y == 95.0F &&
                  effects->impacts()[12].position.x == 160.0F &&
                  effects->impacts()[12].position.y == 100.0F &&
                  effects->impacts()[0].velocity.x == duplicate->impacts()[0].velocity.x,
              "impact bursts use enemy centers and deterministic velocities");
    }
    if (effects->explosions().size() == 32 && duplicate->explosions().size() == 32) {
        check(effects->explosions()[0].position.x == 110.0F &&
                  effects->explosions()[0].position.y == 95.0F &&
                  effects->explosions()[0].velocity.x ==
                      duplicate->explosions()[0].velocity.x,
              "enemy explosions use the defeat location and deterministic velocities");
    }
    echo::Projectile projectile{};
    projectile.bounds = {30.0F, 40.0F, 8.0F, 8.0F};
    check(static_cast<bool>(effects->shot(projectile)) && effects->shots().size() == 8 &&
              effects->shots()[0].position.x == 34.0F &&
              effects->shots()[0].position.y == 44.0F,
          "shot burst starts at the projectile center");
    check(static_cast<bool>(effects->step(after, after)) &&
              effects->impacts().size() == 24 &&
              effects->explosions().size() == 32 &&
              effects->impacts()[0].age == echo::kFixedStep,
          "particles advance once without repeating unchanged damage");
    auto transition = after;
    transition.room = echo::RoomId::ResearchLab;
    transition.roamers[0].health = 0;
    check(static_cast<bool>(effects->step(after, transition)) &&
              effects->shots().empty() && effects->impacts().empty() &&
              effects->explosions().empty() && effects->fog().empty(),
          "room transition clears effects without a false roamer hit");
    check(static_cast<bool>(effects->shot(projectile)) &&
              static_cast<bool>(effects->reset()) && effects->shots().empty() &&
              static_cast<bool>(effects->shot(projectile)) && effects->shots().size() == 8,
          "new or loaded game resets and recreates usable emitters");
    auto respawn = before;
    ++respawn.deaths;
    respawn.enemies[0].health = 0;
    check(static_cast<bool>(effects->step(before, respawn)) &&
              effects->shots().empty() && effects->impacts().empty() &&
              effects->explosions().empty() && effects->fog().empty(),
          "death and respawn clear effects without false hits");

    auto cargoSnapshot = before;
    cargoSnapshot.room = echo::RoomId::CargoHold;
    check(static_cast<bool>(effects->reset()) && static_cast<bool>(duplicate->reset()) &&
              static_cast<bool>(effects->step(before, cargoSnapshot)) &&
              static_cast<bool>(duplicate->step(before, cargoSnapshot)) &&
              effects->fog().size() == 128 && duplicate->fog().size() == 128 &&
              effects->fog()[0].position.x == 96.0F &&
              effects->fog()[0].position.y == 48.0F &&
              effects->fog()[127].position.x == 672.0F &&
              effects->fog()[127].position.y == 336.0F &&
              effects->fog()[0].velocity.x == duplicate->fog()[0].velocity.x,
          "Cargo Hold fog starts as a deterministic full-room mist field");
    check(static_cast<bool>(effects->step(cargoSnapshot, cargoSnapshot)) &&
              effects->fog().size() == 128 &&
              effects->fog()[0].age == echo::kFixedStep,
          "Cargo Hold fog advances only on a fixed gameplay step");
    check(static_cast<bool>(effects->step(cargoSnapshot, transition)) &&
              effects->fog().empty(),
          "leaving the Cargo Hold clears its ambient fog");

    echo::Game hunter;
    auto hunt = hunter.checkpoint();
    hunt.room = echo::RoomId::CargoHold;
    hunt.playerPosition = {470.0F, 118.0F};
    check(hunter.restore(hunt), "isometric impact fixture restores");
    check(static_cast<bool>(effects->reset()), "isometric impact fixture resets effects");
    bool realRoamerHit = false;
    bool updatesOk = true;
    for (int tick = 0; tick < 181; ++tick) {
        const auto snapshot = GameplayVfx::capture(hunter);
        echo::Input input{};
        if (tick == 30) {
            input.moveX = -1;
            input.moveY = -1;
        } else if (tick > 30) {
            input.shootHeld = true;
        }
        updatesOk = static_cast<bool>(hunter.update(input)) && updatesOk;
        const auto next = GameplayVfx::capture(hunter);
        updatesOk = static_cast<bool>(effects->step(snapshot, next)) && updatesOk;
        if (snapshot.room == next.room && snapshot.deaths == next.deaths &&
            snapshot.roamerCount > 0 && next.roamerCount > 0 &&
            next.roamers[0].health < snapshot.roamers[0].health &&
            !effects->impacts().empty()) {
            realRoamerHit = true;
        }
    }
    check(updatesOk && realRoamerHit,
          "real isometric combat produces a roamer impact through captured game states");

    echo::Game fighter;
    for (int tick = 0; tick < 1800 && fighter.roomId() != echo::RoomId::MaintenanceTunnel; ++tick) {
        updatesOk = static_cast<bool>(fighter.update(
            {.moveX = 1, .jumpPressed = true, .jumpHeld = true, .shootHeld = true})) && updatesOk;
    }
    for (int tick = 0; tick < 140; ++tick) {
        updatesOk = static_cast<bool>(fighter.update({.moveX = 1})) && updatesOk;
    }
    check(static_cast<bool>(effects->reset()), "side-view impact fixture resets effects");
    bool realEnemyHit = false;
    for (int tick = 0; tick < 120; ++tick) {
        const auto snapshot = GameplayVfx::capture(fighter);
        updatesOk = static_cast<bool>(fighter.update({.shootHeld = true})) && updatesOk;
        const auto next = GameplayVfx::capture(fighter);
        updatesOk = static_cast<bool>(effects->step(snapshot, next)) && updatesOk;
        for (std::size_t index = 0; index < snapshot.enemies.size(); ++index) {
            if (snapshot.room == next.room && snapshot.deaths == next.deaths &&
                snapshot.enemies[index].room == snapshot.room &&
                next.enemies[index].health < snapshot.enemies[index].health &&
                !effects->impacts().empty()) {
                realEnemyHit = true;
            }
        }
    }
    check(updatesOk && realEnemyHit,
          "real side-view combat produces an enemy impact through captured game states");
}

void testSettingsPersistence() {
    using echo::framework::GameState;
    using echo::framework::StateScope;
    using echo::framework::StateValue;

    GameState saved{};
    const echo::platform::Bindings original{SDLK_f, SDLK_g, SDLK_h};
    const auto czech = echo::framework::DialogueLanguage::Czech;
    check(static_cast<bool>(echo::platform::encodeSettings(70, original, czech, saved)),
          "settings encode into framework state");
    int volume = 20;
    echo::platform::Bindings bindings{};
    auto language = echo::framework::DialogueLanguage::English;
    check(static_cast<bool>(echo::platform::decodeSettings(saved, volume, bindings, language)) &&
              volume == 70 && bindings.jump == SDLK_f &&
              bindings.shoot == SDLK_g && bindings.interact == SDLK_h && language == czech,
          "settings round-trip volume, keyboard bindings and dialogue language");

    GameState legacy = saved;
    check(legacy.erase({StateScope::Global, {}, "dialogue.language"}) &&
              static_cast<bool>(echo::platform::decodeSettings(legacy, volume, bindings, language)) &&
              language == echo::framework::DialogueLanguage::English,
          "settings without a dialogue language retain the legacy English default");

    GameState invalid = saved;
    check(static_cast<bool>(invalid.set(
              {StateScope::Global, {}, "audio.volume"}, StateValue::fromInteger(101))),
          "invalid volume fixture is stored");
    const int previousVolume = volume;
    const echo::platform::Bindings previousBindings = bindings;
    const auto previousLanguage = language;
    check(!echo::platform::decodeSettings(invalid, volume, bindings, language) &&
              volume == previousVolume && bindings.jump == previousBindings.jump &&
              bindings.shoot == previousBindings.shoot &&
              bindings.interact == previousBindings.interact && language == previousLanguage,
          "invalid settings are rejected without partial application");

    GameState wrongType = saved;
    check(static_cast<bool>(wrongType.set(
              {StateScope::Global, {}, "input.jump"}, StateValue::fromBoolean(true))),
          "invalid key type fixture is stored");
    check(!echo::platform::decodeSettings(wrongType, volume, bindings, language),
          "settings decoder rejects values with incorrect types");

    GameState invalidLanguage = saved;
    check(static_cast<bool>(invalidLanguage.set(
              {StateScope::Global, {}, "dialogue.language"}, StateValue::fromInteger(2))),
          "invalid dialogue language fixture is stored");
    check(!echo::platform::decodeSettings(invalidLanguage, volume, bindings, language),
          "settings decoder rejects unknown dialogue languages");

    using echo::platform::DisplayMode;
    GameState video{};
    check(static_cast<bool>(echo::platform::encodeSettings(70, original, czech, video,
                                                           DisplayMode::Fullscreen)),
          "settings encode the display mode");
    auto display = DisplayMode::Window720;
    check(static_cast<bool>(echo::platform::decodeSettings(video, volume, bindings, language,
                                                           display)) &&
              display == DisplayMode::Fullscreen,
          "settings round-trip the display mode");
    GameState legacyVideo = video;
    check(legacyVideo.erase({StateScope::Global, {}, "video.display"}) &&
              static_cast<bool>(echo::platform::decodeSettings(legacyVideo, volume, bindings,
                                                               language, display)) &&
              display == DisplayMode::Window720,
          "settings without a display mode default to the 1280x720 window");
    GameState invalidVideo = video;
    display = DisplayMode::Window1080;
    check(static_cast<bool>(invalidVideo.set({StateScope::Global, {}, "video.display"},
                                             StateValue::fromInteger(3))) &&
              !echo::platform::decodeSettings(invalidVideo, volume, bindings, language,
                                              display) &&
              display == DisplayMode::Window1080,
          "settings decoder rejects unknown display modes without applying them");
    GameState debugSettings{};
    bool renderDebug = false;
    check(static_cast<bool>(echo::platform::encodeSettings(
              70, original, czech, debugSettings, DisplayMode::Fullscreen, true)) &&
              static_cast<bool>(echo::platform::decodeSettings(
                  debugSettings, volume, bindings, language, display, renderDebug)) &&
              renderDebug,
          "render debug setting round-trips");
    GameState legacyDebug = debugSettings;
    check(legacyDebug.erase({StateScope::Global, {}, "video.render_debug"}) &&
              static_cast<bool>(echo::platform::decodeSettings(
                  legacyDebug, volume, bindings, language, display, renderDebug)) &&
              !renderDebug,
          "older settings default render debug to off");
    GameState invalidDebug = debugSettings;
    renderDebug = true;
    volume = 23;
    check(static_cast<bool>(invalidDebug.set({StateScope::Global, {}, "video.render_debug"},
                                             StateValue::fromInteger(2))) &&
              !echo::platform::decodeSettings(
                  invalidDebug, volume, bindings, language, display, renderDebug) &&
              renderDebug && volume == 23,
          "invalid render debug setting is rejected without partial application");
    check(static_cast<bool>(invalidDebug.set({StateScope::Global, {}, "video.render_debug"},
                                             StateValue::fromBoolean(true))) &&
              !echo::platform::decodeSettings(
                  invalidDebug, volume, bindings, language, display, renderDebug),
          "render debug setting rejects incorrect value types");
}

void testBitmapFontUtf8() {
    check(echo::platform::supportsBitmapText(
              "\x50\xC5\x98\xC3\x8D\x4C\x49\xC5\xA0 \xC5\xBD\x4C\x55\xC5\xA4"
              "\x4F\x55\xC4\x8C\x4B\xC3\x9D \x4B\xC5\xAE\xC5\x87", 24),
          "bitmap font accepts supported Czech UTF-8 characters by code point");
    check(!echo::platform::supportsBitmapText("ŽLUŤ", 3),
          "bitmap font character limits count UTF-8 code points");
    const std::string_view malformed{"\xC0\xAF", 2};
    check(!echo::platform::supportsBitmapText(malformed, 8),
          "bitmap font rejects malformed UTF-8");
    check(!echo::platform::supportsBitmapText("HELLO?", 8),
          "bitmap font rejects characters without a glyph");
}

void testGameCheckpointPersistence(echo::platform::FrameworkIntegration& integration) {
    using echo::framework::GameState;
    using echo::framework::StateScope;
    using echo::framework::StateValue;

    echo::Game game;
    check(static_cast<bool>(integration.configureGame(game)), "checkpoint game binds dialogue");
    check(static_cast<bool>(game.update({.interactPressed = true})) &&
              static_cast<bool>(game.update({.interactPressed = true})) &&
              static_cast<bool>(game.update({.interactPressed = true})),
          "checkpoint fixture acquires the key through the loaded dialogue");
    echo::GameCheckpoint checkpoint = game.checkpoint();
    check(checkpoint.inventory.add(echo::ItemId::HealthCell, 3, 2) &&
              game.restore(checkpoint),
          "checkpoint fixture adds a stack of catalogued health cells");
    check(static_cast<bool>(game.update({.moveX = 1})) &&
              static_cast<bool>(game.update({.interactPressed = true})) &&
              game.questBranch() == 2,
          "checkpoint fixture selects and persists the sentry quest route");
    GameState saved{};
    check(static_cast<bool>(echo::platform::encodeGameCheckpoint(game, saved)),
          "game checkpoint encodes to framework state");
    echo::GameCheckpoint restored{};
    check(static_cast<bool>(echo::platform::decodeGameCheckpoint(saved, restored)) &&
              restored.inventory.contains(echo::ItemId::MaintenanceKey) &&
              restored.inventory.quantity(echo::ItemId::HealthCell) == 2 &&
              restored.questBranch == 2 && game.restore(restored) &&
              game.questProgress().completed == 1 && game.questBranch() == 2,
          "current checkpoint round-trip preserves inventory, quest route and progress");
    echo::Game claimedGame;
    auto claimedCheckpoint = claimedGame.checkpoint();
    claimedCheckpoint.room = echo::RoomId::CentralChamber;
    claimedCheckpoint.playerPosition = {100.0F, 292.0F};
    claimedCheckpoint.hasDoubleJump = true;
    claimedCheckpoint.abilityCollected = true;
    claimedCheckpoint.completed = true;
    claimedCheckpoint.enemyHealth[4] = 0;
    claimedCheckpoint.questBranch = 2;
    claimedCheckpoint.questRewardClaimed = true;
    claimedCheckpoint.inventory.clear();
    GameState claimedState{};
    check(static_cast<bool>(integration.configureGame(claimedGame)) &&
              claimedCheckpoint.inventory.add(echo::ItemId::MaintenanceKey) &&
              claimedCheckpoint.inventory.add(echo::ItemId::MedKit, 3, 1) &&
              claimedGame.restore(claimedCheckpoint) &&
              echo::platform::encodeGameCheckpoint(claimedGame, claimedState) &&
              echo::platform::decodeGameCheckpoint(claimedState, restored) &&
              restored.questBranch == 2 && restored.questRewardClaimed &&
              restored.completed && restored.inventory.quantity(echo::ItemId::MedKit) == 1 &&
              claimedGame.restore(restored) && claimedGame.update({}) &&
              claimedGame.itemQuantity(echo::ItemId::MedKit) == 1,
          "format six preserves a claimed route reward without granting it twice");

    GameState legacyInventory = saved;
    check(legacyInventory.erase({StateScope::Global, {}, "inventory.layout"}) &&
              legacyInventory.erase({StateScope::Global, {}, "inventory.active"}) &&
              legacyInventory.erase({StateScope::Global, {}, "inventory.active.quantity"}),
          "legacy inventory fixture omits the new layout and active fields");
    for (std::size_t slot = 0; slot < echo::kEquipmentSlotCount; ++slot) {
        check(legacyInventory.erase(
                  {StateScope::Global, {},
                   echo::platform::game_persistence_detail::kEquipmentKeys[slot]}) &&
                  legacyInventory.erase(
                  {StateScope::Global, {},
                   echo::platform::game_persistence_detail::kEquipmentQuantityKeys[slot]}),
              "legacy fixture omits equipment fields");
    }
    for (std::size_t slot = 8; slot < echo::Inventory::kCapacity; ++slot) {
        check(legacyInventory.erase(
                  {StateScope::Global, {},
                   echo::platform::game_persistence_detail::kInventoryKeys[slot]}) &&
                  legacyInventory.erase(
                  {StateScope::Global, {},
                   echo::platform::game_persistence_detail::kInventoryQuantityKeys[slot]}),
              "legacy fixture retains its original eight-slot capacity");
    }
    check(legacyInventory.erase(
              {StateScope::Global, {}, "inventory.quantity.0"}) &&
              static_cast<bool>(echo::platform::migrateGameCheckpointSave(
                  2, legacyInventory, nullptr)) &&
              static_cast<bool>(echo::platform::decodeGameCheckpoint(
                  legacyInventory, restored)) &&
              restored.inventory.quantities[0] == 1,
          "pre-stack saves default missing item quantities to one");

    checkpoint = game.checkpoint();
    checkpoint.equipment[static_cast<std::size_t>(echo::EquipmentSlot::Body)] =
        {echo::ItemId::KineticCoil, 1};
    check(game.restore(checkpoint) &&
              game.moveInventorySlot(0, 19) &&
              game.moveInventorySlot(1, echo::Game::kActiveItemSlot) &&
              echo::platform::encodeGameCheckpoint(game, saved) &&
              echo::platform::decodeGameCheckpoint(saved, restored) &&
              restored.inventory.items[19] == echo::ItemId::MaintenanceKey &&
              restored.inventory.items[0] == echo::ItemId::None &&
              restored.equipment[static_cast<std::size_t>(echo::EquipmentSlot::Body)].item ==
                  echo::ItemId::KineticCoil &&
              restored.activeItem.quantity == 2,
          "slot-layout round-trip preserves holes, equipment and the active stack");

    checkpoint = game.checkpoint();
    checkpoint.room = echo::RoomId::MaintenanceTunnel;
    checkpoint.enemyHealth[0] = 0;
    checkpoint.lootDrops[0] = {
        echo::RoomId::MaintenanceTunnel, {120.0F, 304.0F},
        {echo::ItemId::Scrap, 2}, true};
    check(game.restore(checkpoint) &&
              echo::platform::encodeGameCheckpoint(game, saved) &&
              echo::platform::decodeGameCheckpoint(saved, restored) &&
              restored.lootDrops[0].active &&
              restored.lootDrops[0].stack == echo::ItemStack{echo::ItemId::Scrap, 2},
          "checkpoint round-trip preserves uncollected enemy loot");
    auto invalidLoot = saved;
    check(invalidLoot.set({StateScope::Global, {}, "loot.0.item"},
                          StateValue::fromInteger(static_cast<int>(echo::ItemId::HealthCell))) &&
              !echo::platform::decodeGameCheckpoint(invalidLoot, restored),
          "checkpoint decoder rejects tampered enemy loot types");

    GameState invalid = saved;
    check(static_cast<bool>(invalid.set(
              {StateScope::Global, {}, "inventory.item.0"}, StateValue::fromInteger(99))),
          "invalid inventory fixture is stored");
    echo::GameCheckpoint unchanged{};
    unchanged.playerHealth = 3;
    check(!echo::platform::decodeGameCheckpoint(invalid, unchanged) &&
              unchanged.playerHealth == 3 && unchanged.inventory.count == 0,
          "checkpoint decoder rejects invalid inventory without partial output");
    GameState invalidQuantity = saved;
    check(static_cast<bool>(invalidQuantity.set(
              {StateScope::Global, {}, "inventory.quantity.19"},
              StateValue::fromInteger(0))),
          "invalid inventory quantity fixture is stored");
    check(echo::platform::decodeGameCheckpoint(invalidQuantity, unchanged).code ==
              echo::framework::ErrorCode::CorruptData &&
              unchanged.playerHealth == 3 && unchanged.inventory.count == 0,
          "checkpoint decoder rejects invalid quantities without partial output");
    GameState invalidQuest = saved;
    check(invalidQuest.set({StateScope::Global, {}, "quest.branch"},
                           StateValue::fromInteger(3)) &&
              !echo::platform::decodeGameCheckpoint(invalidQuest, unchanged),
          "checkpoint decoder rejects an unlisted quest branch");
    invalidQuest = saved;
    check(invalidQuest.set({StateScope::Global, {}, "quest.reward_claimed"},
                           StateValue::fromBoolean(true)) &&
              !echo::platform::decodeGameCheckpoint(invalidQuest, unchanged),
          "checkpoint decoder rejects a premature quest reward claim");
    auto badGear = std::make_unique<GameState>(saved);
    check(badGear->set({StateScope::Global, {}, "equipment.head"},
                      StateValue::fromInteger(static_cast<int>(echo::ItemId::HealthCell))) &&
              badGear->set({StateScope::Global, {}, "equipment.head.quantity"},
                          StateValue::fromInteger(1)) &&
              !echo::platform::decodeGameCheckpoint(*badGear, unchanged) &&
              unchanged.playerHealth == 3,
          "decoder rejects incompatible gear without partial output");
    check(badGear->replaceValues(saved) &&
              badGear->erase({StateScope::Global, {}, "inventory.layout"}) &&
              !echo::platform::decodeGameCheckpoint(*badGear, unchanged),
          "slot fields cannot silently fall back to the legacy layout");
    check(badGear->replaceValues(legacyInventory) &&
              badGear->set({StateScope::Global, {}, "inventory.item.19"},
                          StateValue::fromInteger(1)) &&
              !echo::platform::decodeGameCheckpoint(*badGear, unchanged),
          "legacy saves reject expanded backpack fields without a layout version");

    constexpr std::array<echo::framework::ModuleVersion, 2> saveModules{{
        {"echo.state", 1, true}, {"echo.project_echo", 1, true}}};
    auto oldState = std::make_unique<GameState>();
    auto decodedState = std::make_unique<GameState>();
    auto buffer = std::make_unique<std::array<char, echo::framework::kMaxSaveBytes>>();
    for (const std::uint32_t version : {1U, 2U, 3U, 4U, 5U}) {
        const GameState& versionState = version >= 3 ? saved : legacyInventory;
        check(static_cast<bool>(oldState->replaceValues(versionState)),
              "legacy save fixture copies its state");
        check(oldState->erase({StateScope::Global, {}, "quest.branch"}) &&
                  oldState->erase({StateScope::Global, {}, "quest.reward_claimed"}),
              "pre-branch saves exclude quest route and reward claims");
        if (version < 5) {
            for (std::size_t index = 2; index < echo::kEnemyCount; ++index) {
                check(oldState->erase({StateScope::Global, {},
                                      echo::platform::game_persistence_detail::kEnemyKeys[index]}),
                      "old save excludes new enemies");
            }
            for (std::size_t index = 0; index < 10; ++index) {
                check(oldState->erase({StateScope::Global, {},
                                      echo::platform::game_persistence_detail::kExtraLootActiveKeys[index]}) &&
                          oldState->erase({StateScope::Global, {},
                                      echo::platform::game_persistence_detail::kExtraLootXKeys[index]}),
                      "old save excludes new drops");
            }
            for (std::string_view key : {"effect.speed", "effect.jump", "effect.shield"}) {
                check(oldState->erase({StateScope::Global, {}, key}),
                      "old save excludes new effects");
            }
        }
        if (version == 1) {
            check(oldState->erase({StateScope::Global, {}, "inventory.count"}),
                  "version-one save has no inventory");
            for (std::size_t slot = 0; slot < 8; ++slot) {
                echo::framework::StateValue value{};
                const auto itemKey =
                    echo::platform::game_persistence_detail::kInventoryKeys[slot];
                const auto quantityKey =
                    echo::platform::game_persistence_detail::kInventoryQuantityKeys[slot];
                if (oldState->get({StateScope::Global, {}, itemKey}, value)) {
                    check(oldState->erase({StateScope::Global, {}, itemKey}),
                          "version-one fixture removes inventory items");
                }
                if (oldState->get({StateScope::Global, {}, quantityKey}, value)) {
                    check(oldState->erase({StateScope::Global, {}, quantityKey}),
                          "version-one fixture removes inventory quantities");
                }
            }
        }
        if (version == 3) {
            for (std::size_t slot = 0; slot < 2; ++slot) {
                for (const std::string_view key : {
                         echo::platform::game_persistence_detail::kLootActiveKeys[slot],
                         echo::platform::game_persistence_detail::kLootRoomKeys[slot],
                         echo::platform::game_persistence_detail::kLootItemKeys[slot],
                         echo::platform::game_persistence_detail::kLootQuantityKeys[slot],
                         echo::platform::game_persistence_detail::kLootXKeys[slot],
                         echo::platform::game_persistence_detail::kLootYKeys[slot]}) {
                    check(oldState->erase({StateScope::Global, {}, key}),
                          "version-three fixture removes fields added by loot support");
                }
            }
        }
        std::size_t written{};
        echo::framework::SaveHeader header{};
        check(echo::framework::SaveCodec::encode(
                  "echo.project_echo", version, saveModules, *oldState,
                  buffer->data(), buffer->size(), written) &&
                  echo::framework::SaveCodec::decode(
                  buffer->data(), written, "echo.project_echo", 6, saveModules,
                  *decodedState, header, &echo::platform::migrateGameCheckpointSave) &&
                  header.formatVersion == 6 &&
                  echo::platform::decodeGameCheckpoint(*decodedState, restored) &&
                  game.restore(restored) && game.hasItem(echo::ItemId::MaintenanceKey) &&
                  game.questProgress().completed == 1 &&
                  game.questBranch() == 0 &&
                  ((version == 1 && !game.lootDrops()[0].active) ||
                   (version == 2 &&
                    game.inventorySlot(echo::Game::kActiveItemSlot).quantity == 2) ||
                   (version == 3 && !game.lootDrops()[0].active) ||
                   (version == 4 && game.lootDrops()[0].active) ||
                   (version == 5 && game.lootDrops()[0].active)),
              "version-one through five saves migrate route fields and preserve prior progress");
    }

    GameState migrated{};
    check(echo::platform::migrateGameCheckpointSave(1, migrated, nullptr),
          "version-one gameplay save migration succeeds");
    StateValue migratedCount{};
    StateValue migratedItem{};
    check(migrated.get({StateScope::Global, {}, "inventory.count"}, migratedCount) &&
              migratedCount.integer == 1 &&
              migrated.get({StateScope::Global, {}, "inventory.item.0"}, migratedItem) &&
              migratedItem.integer == static_cast<std::int64_t>(echo::ItemId::MaintenanceKey),
          "legacy saves receive the key to preserve their previously unlocked route");
    check(echo::platform::migrateGameCheckpointSave(3, migrated, nullptr) &&
              echo::platform::migrateGameCheckpointSave(5, migrated, nullptr) &&
              !echo::platform::migrateGameCheckpointSave(6, migrated, nullptr),
          "unknown future save versions are not migrated");
    GameState versionFour = saved;
    echo::GameCheckpoint oldDrop{};
    check(echo::platform::migrateGameCheckpointSave(4, versionFour, nullptr) &&
              echo::platform::decodeGameCheckpoint(versionFour, oldDrop) &&
              oldDrop.lootDrops[0].active &&
              oldDrop.lootDrops[0].stack == echo::ItemStack{echo::ItemId::Scrap, 2} &&
              oldDrop.enemyHealth[2] == 3 && oldDrop.enemyHealth[3] == 6 &&
              oldDrop.enemyHealth[4] == 4,
          "version four preserves existing loot and adds the new enemies");
    check(static_cast<bool>(game.update({.interactPressed = true})) &&
              game.questBranch() == 1,
          "the migrated save fixture selects a quest route before resuming gameplay");
    auto newDrop = game.checkpoint();
    newDrop.enemyHealth[2] = 0;
    newDrop.lootDrops[2] = {
        echo::RoomId::MaintenanceTunnel, {400.0F, 304.0F},
        {echo::ItemId::JumpVisor, 1}, true};
    newDrop.speedBoostSeconds = 4.0F;
    check(game.restore(newDrop) && echo::platform::encodeGameCheckpoint(game, saved) &&
              echo::platform::decodeGameCheckpoint(saved, restored) &&
              restored.lootDrops[2].active &&
              restored.lootDrops[2].stack.item == echo::ItemId::JumpVisor &&
              restored.speedBoostSeconds == 4.0F && game.restore(restored),
          "new loot and active effects survive checkpoint serialization");
    auto invalidNewLoot = saved;
    check(invalidNewLoot.set({StateScope::Global, {}, "enemy.2.health"},
                             StateValue::fromInteger(3)) &&
              !echo::platform::decodeGameCheckpoint(invalidNewLoot, restored),
          "new loot is rejected when its enemy is still alive");
    newDrop = game.checkpoint();
    newDrop.room = echo::RoomId::MaintenanceTunnel;
    newDrop.playerPosition = {400.0F, 292.0F};
    newDrop.playerVelocity = {};
    check(game.restore(newDrop) && game.update({}) &&
              game.hasItem(echo::ItemId::JumpVisor) && !game.lootDrops()[2].active &&
              echo::platform::encodeGameCheckpoint(game, saved) &&
              echo::platform::decodeGameCheckpoint(saved, restored) &&
              game.restore(restored) && game.hasItem(echo::ItemId::JumpVisor) &&
              !game.lootDrops()[2].active,
          "collecting restored new loot persists without duplication");
}

SDL_Window* createdWindow(SDL_Window* previous = nullptr) {
    // The isolated dummy-video session creates only these two windows, in order.
    const Uint32 id = previous == nullptr ? 1 : SDL_GetWindowID(previous) + 1;
    SDL_Window* window = SDL_GetWindowFromID(id);
    if (window == nullptr || SDL_GetRenderer(window) == nullptr) {
        throw std::runtime_error("Created test renderer window is unavailable.");
    }
    return window;
}

std::vector<Uint32> capture(SDL_Window* window, SDL_Rect logicalRegion) {
    SDL_Renderer* renderer = SDL_GetRenderer(window);
    int width = 0;
    int height = 0;
    if (SDL_GetRendererOutputSize(renderer, &width, &height) != 0) {
        throw std::runtime_error(SDL_GetError());
    }
    const SDL_Rect region{logicalRegion.x * width / 640, logicalRegion.y * height / 360,
                          logicalRegion.w * width / 640, logicalRegion.h * height / 360};
    std::vector<Uint32> pixels(static_cast<std::size_t>(region.w) * region.h);
    if (SDL_RenderReadPixels(renderer, &region, SDL_PIXELFORMAT_ARGB8888, pixels.data(),
                             region.w * static_cast<int>(sizeof(Uint32))) != 0) {
        throw std::runtime_error(SDL_GetError());
    }
    return pixels;
}

class SdlMenuSink final : public echo::rendering::ICanvas2D {
public:
    explicit SdlMenuSink(SDL_Renderer* renderer) noexcept : renderer_(renderer) {}
    bool clear(echo::rendering::Color2D color) noexcept override {
        return SDL_SetRenderDrawColor(renderer_, color.r, color.g, color.b, color.a) == 0 &&
               SDL_RenderClear(renderer_) == 0;
    }
    bool fill(echo::rendering::Rect2D bounds, echo::rendering::Color2D color) noexcept override {
        const SDL_Rect rect{bounds.x, bounds.y, bounds.w, bounds.h};
        return SDL_SetRenderDrawColor(renderer_, color.r, color.g, color.b, color.a) == 0 &&
               SDL_RenderFillRect(renderer_, &rect) == 0;
    }
    bool registerTexture(echo::rendering::TextureHandle handle, SDL_Texture* texture,
                         int width, int height) noexcept {
        if (!handle.valid() || texture == nullptr || width <= 0 || height <= 0) return false;
        for (auto& entry : textures_) {
            if (!entry.texture) {
                entry = {handle, texture, width, height};
                return true;
            }
        }
        return false;
    }
    bool texture(echo::rendering::TextureHandle handle, echo::rendering::Rect2D bounds,
                 echo::rendering::Vec4 uv, echo::rendering::Vec4 tint,
                 float rotation) noexcept override {
        const auto found = std::find_if(textures_.begin(), textures_.end(),
            [handle](const Texture& entry) { return entry.texture && entry.handle == handle; });
        if (found == textures_.end() || bounds.w <= 0 || bounds.h <= 0 ||
            !std::isfinite(uv.x) || !std::isfinite(uv.y) ||
            !std::isfinite(uv.z) || !std::isfinite(uv.w) ||
            !std::isfinite(tint.x) || !std::isfinite(tint.y) ||
            !std::isfinite(tint.z) || !std::isfinite(tint.w) ||
            !std::isfinite(rotation) || uv.z == 0.0F || uv.w == 0.0F ||
            uv.x < 0.0F || uv.x > 1.0F || uv.y < 0.0F || uv.y > 1.0F ||
            uv.x + uv.z < 0.0F || uv.x + uv.z > 1.0F ||
            uv.y + uv.w < 0.0F || uv.y + uv.w > 1.0F ||
            tint.x < 0.0F || tint.x > 1.0F || tint.y < 0.0F || tint.y > 1.0F ||
            tint.z < 0.0F || tint.z > 1.0F || tint.w < 0.0F || tint.w > 1.0F) return false;
        const int x0 = static_cast<int>(std::lround(uv.x * found->width));
        const int x1 = static_cast<int>(std::lround((uv.x + uv.z) * found->width));
        const int y0 = static_cast<int>(std::lround(uv.y * found->height));
        const int y1 = static_cast<int>(std::lround((uv.y + uv.w) * found->height));
        const SDL_Rect source{std::min(x0, x1), std::min(y0, y1),
                              std::abs(x1 - x0), std::abs(y1 - y0)};
        const SDL_Rect destination{bounds.x, bounds.y, bounds.w, bounds.h};
        const auto channel = [](float value) {
            return static_cast<Uint8>(std::lround(linearToSrgb(value) * 255.0F));
        };
        if (source.w <= 0 || source.h <= 0 ||
            SDL_SetTextureColorMod(found->texture, channel(tint.x), channel(tint.y),
                                   channel(tint.z)) != 0 ||
            SDL_SetTextureAlphaMod(found->texture,
                static_cast<Uint8>(std::lround(tint.w * 255.0F))) != 0) return false;
        const auto flip = static_cast<SDL_RendererFlip>(
            (uv.z < 0.0F ? SDL_FLIP_HORIZONTAL : 0) |
            (uv.w < 0.0F ? SDL_FLIP_VERTICAL : 0));
        const int result = SDL_RenderCopyEx(renderer_, found->texture, &source, &destination,
                                             rotation, nullptr, flip);
        const bool restored = SDL_SetTextureColorMod(found->texture, 255, 255, 255) == 0 &&
                              SDL_SetTextureAlphaMod(found->texture, 255) == 0;
        return result == 0 && restored;
    }
private:
    struct Texture {
        echo::rendering::TextureHandle handle{};
        SDL_Texture* texture{};
        int width{}, height{};
    };
    SDL_Renderer* renderer_;
    std::array<Texture, 16> textures_{};
};

class FailingMenuSink final : public echo::rendering::ICanvas2D {
public:
    bool clear(echo::rendering::Color2D) noexcept override { return true; }
    bool fill(echo::rendering::Rect2D, echo::rendering::Color2D) noexcept override { ++calls; return false; }
    bool texture(echo::rendering::TextureHandle, echo::rendering::Rect2D,
                 echo::rendering::Vec4, echo::rendering::Vec4, float) noexcept override { return false; }
    int calls{};
};

class FailingClearSink final : public echo::rendering::ICanvas2D {
public:
    bool clear(echo::rendering::Color2D) noexcept override { return false; }
    bool fill(echo::rendering::Rect2D, echo::rendering::Color2D) noexcept override {
        ++fills;
        return true;
    }
    bool texture(echo::rendering::TextureHandle, echo::rendering::Rect2D,
                 echo::rendering::Vec4, echo::rendering::Vec4, float) noexcept override { return false; }
    int fills{};
};

class SpriteProbeSink final : public echo::rendering::ICanvas2D {
public:
    bool clear(echo::rendering::Color2D) noexcept override { return true; }
    bool fill(echo::rendering::Rect2D, echo::rendering::Color2D) noexcept override { return true; }
    bool texture(echo::rendering::TextureHandle handle, echo::rendering::Rect2D destination,
                 echo::rendering::Vec4 uv, echo::rendering::Vec4 tint,
                 float rotation) noexcept override {
        if (calls == 0) firstDestination = destination;
        ++calls;
        lastHandle = handle;
        lastUv = uv;
        lastTint = tint;
        lastRotation = rotation;
        mirroredCalls += uv.z < 0.0F || uv.w < 0.0F ? 1 : 0;
        rotatedCalls += std::abs(rotation) > 0.001F ? 1 : 0;
        translucentCalls += tint.w < 0.999F ? 1 : 0;
        return !fail;
    }
    bool fail{};
    int calls{};
    int mirroredCalls{};
    int rotatedCalls{};
    int translucentCalls{};
    echo::rendering::Rect2D firstDestination{};
    echo::rendering::TextureHandle lastHandle{};
    echo::rendering::Vec4 lastUv{};
    echo::rendering::Vec4 lastTint{};
    float lastRotation{};
};

} // namespace

int main() {
    SDL_SetMainReady();
    if (SDL_setenv("SDL_VIDEODRIVER", "dummy", 1) != 0 ||
        SDL_SetHintWithPriority(SDL_HINT_RENDER_DRIVER, "software", SDL_HINT_OVERRIDE) != SDL_TRUE ||
        SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::cerr << "SDL test initialization failed: " << SDL_GetError() << '\n';
        return EXIT_FAILURE;
    }

    using echo::platform::ControlScheme;
    echo::platform::Renderer keyboard{ControlScheme::Keyboard};
    echo::platform::Renderer pad{ControlScheme::PlayStation};
    try {
        echo::platform::FrameworkIntegration integration;
        const auto status = integration.initialize(
            ECHO_SOURCE_ASSETS "framework/game.json",
            ECHO_SOURCE_ASSETS "framework/engineer.json",
            ECHO_SOURCE_ASSETS "framework/items.json", ECHO_SOURCE_ASSETS);
        if (!status || !integration.beginGame()) {
            throw std::runtime_error("Framework test content failed to initialize.");
        }
        testSettingsPersistence();
        testFixedStepParticles();
        testDamageFlash();
        testGameplayVfx();
        testBitmapFontUtf8();
        testGameCheckpointPersistence(integration);
        if (!keyboard.create()) {
            throw std::runtime_error(SDL_GetError());
        }
        SDL_Window* keyboardWindow = createdWindow();
        if (!pad.create()) {
            throw std::runtime_error(SDL_GetError());
        }
        SDL_Window* padWindow = createdWindow(keyboardWindow);
        echo::Game game;

        keyboard.drawGame(game, false, false, "SPACE", "J", "E");
        const auto keyboardHint = capture(keyboardWindow, {12, 340, 616, 16});
        keyboard.drawGame(game, false, false, "K", "L", "F");
        check(keyboardHint != capture(keyboardWindow, {12, 340, 616, 16}),
              "keyboard gameplay hints follow rebound keys");

        pad.drawGame(game, false, false, "SPACE", "J", "E");
        const auto padHint = capture(padWindow, {12, 340, 616, 16});
        pad.drawGame(game, false, false, "K", "L", "F");
        check(padHint == capture(padWindow, {12, 340, 616, 16}),
              "PlayStation gameplay hints do not expose keyboard bindings");
        check(padHint != keyboardHint, "platform selection changes the gameplay prompts");

        echo::Game cutsceneGame;
        check(static_cast<bool>(integration.configureGame(cutsceneGame)),
              "cutscene render game binds loaded framework content");
        auto cutsceneCheckpoint = cutsceneGame.checkpoint();
        cutsceneCheckpoint.room = echo::RoomId::PowerStation;
        cutsceneCheckpoint.playerPosition = {
            static_cast<float>(echo::kRoomWidth) - 20.0F, 292.0F};
        cutsceneCheckpoint.hasDoubleJump = true;
        cutsceneCheckpoint.abilityCollected = true;
        check(cutsceneCheckpoint.inventory.add(echo::ItemId::MaintenanceKey) &&
                  cutsceneGame.restore(cutsceneCheckpoint) &&
                  static_cast<bool>(cutsceneGame.update({.interactPressed = true})) &&
                  static_cast<bool>(cutsceneGame.update({.moveX = 1})) &&
                  cutsceneGame.cutsceneActive(),
              "entering Central Chamber starts a cutscene for offscreen rendering");
        keyboard.drawGame(cutsceneGame, false, false, "SPACE", "J", "E");
        const auto firstCutsceneCard = capture(keyboardWindow, {64, 210, 512, 106});
        check(static_cast<bool>(cutsceneGame.update({.interactPressed = true})),
              "cutscene input advances the displayed card");
        keyboard.drawGame(cutsceneGame, false, false, "SPACE", "J", "E");
        check(firstCutsceneCard != capture(keyboardWindow, {64, 210, 512, 106}),
              "offscreen renderer updates the cutscene card");

        echo::Game dialogueGame;
        check(static_cast<bool>(integration.configureGame(dialogueGame)),
              "renderer game binds loaded dialogue content");
        keyboard.drawGame(dialogueGame, false, false, "SPACE", "J", "E");
        const auto emptyInventory = capture(keyboardWindow, {206, 12, 152, 38});
        const auto firstObjective = capture(keyboardWindow, {12, 54, 350, 8});
        const auto closedDialogue = capture(keyboardWindow, {64, 234, 512, 82});
        check(static_cast<bool>(dialogueGame.update({.interactPressed = true})),
              "engineer conversation starts");
        check(dialogueGame.dialogueActive(), "nearby engineer can be talked to");
        keyboard.drawGame(dialogueGame, false, false, "SPACE", "J", "E");
        const auto englishDialogue = capture(keyboardWindow, {64, 234, 512, 82});
        check(closedDialogue != englishDialogue,
              "engineer dialogue renders in the game viewport");
        check(static_cast<bool>(dialogueGame.setDialogueLanguage(
                  echo::framework::DialogueLanguage::Czech)),
              "dialogue language switches while a conversation is active");
        keyboard.drawGame(dialogueGame, false, false, "SPACE", "J", "E");
        check(englishDialogue != capture(keyboardWindow, {64, 234, 512, 82}),
              "Czech UTF-8 dialogue glyphs render into the game viewport");
        check(static_cast<bool>(dialogueGame.setDialogueLanguage(
                  echo::framework::DialogueLanguage::English)),
              "dialogue language switches back to English");
        keyboard.drawGame(dialogueGame, false, false, "SPACE", "J", "E");
        check(static_cast<bool>(dialogueGame.update({.interactPressed = true})) &&
                  dialogueGame.dialogueChoiceCount() == 2,
              "engineer dialogue shows choices");
        keyboard.drawGame(dialogueGame, false, false, "SPACE", "J", "E");
        const auto firstChoice = capture(keyboardWindow, {82, 264, 340, 14});
        check(static_cast<bool>(dialogueGame.update({.moveX = 1})),
              "dialogue choice navigation succeeds");
        keyboard.drawGame(dialogueGame, false, false, "SPACE", "J", "E");
        check(firstChoice != capture(keyboardWindow, {82, 264, 340, 14}),
              "the selected dialogue choice changes on screen");
        const auto keyboardChoiceHint = capture(keyboardWindow, {82, 296, 200, 12});
        pad.drawGame(dialogueGame, false, false, "SPACE", "J", "E");
        check(keyboardChoiceHint != capture(padWindow, {82, 296, 200, 12}),
              "dialogue choice navigation uses platform-specific prompts");
        check(static_cast<bool>(dialogueGame.update({.moveX = -1})) &&
                  static_cast<bool>(dialogueGame.update({.interactPressed = true})),
              "selecting the key choice succeeds");
        keyboard.drawGame(dialogueGame, false, false, "SPACE", "J", "E");
        check(firstObjective != capture(keyboardWindow, {12, 54, 350, 8}),
              "the quest objective changes in the HUD after the key is granted");
        const auto keyInventory = capture(keyboardWindow, {206, 12, 152, 38});
        check(emptyInventory != keyInventory,
              "inventory HUD updates when the maintenance key is acquired");
        check(static_cast<bool>(dialogueGame.update({.interactPressed = true})) &&
                  !dialogueGame.dialogueActive(),
              "engineer dialogue closes before the quest route choice is shown");
        keyboard.drawGame(dialogueGame, false, false, "SPACE", "J", "E");
        const auto firstRouteChoice = capture(keyboardWindow, {122, 258, 396, 62});
        check(static_cast<bool>(dialogueGame.update({.moveX = 1})),
              "quest route selection navigates offscreen");
        keyboard.drawGame(dialogueGame, false, false, "SPACE", "J", "E");
        check(firstRouteChoice != capture(keyboardWindow, {122, 258, 396, 62}),
              "the selected quest route is rendered distinctly");
        echo::GameCheckpoint healthCellCheckpoint = dialogueGame.checkpoint();
        healthCellCheckpoint.playerHealth = 4;
        check(healthCellCheckpoint.inventory.add(echo::ItemId::HealthCell, 3, 2) &&
                  dialogueGame.restore(healthCellCheckpoint) &&
                  dialogueGame.moveInventorySlot(1, echo::Game::kActiveItemSlot),
              "renderer fixture restores an injured player carrying health cells");
        keyboard.drawGame(dialogueGame, false, false, "SPACE", "J", "E");
        const auto healthCellInventory = capture(keyboardWindow, {206, 12, 152, 38});
        const auto keyboardHealPrompt = capture(keyboardWindow, {481, 54, 80, 14});
        pad.drawGame(dialogueGame, false, false, "SPACE", "J", "E");
        const auto playStationHealPrompt = capture(padWindow, {481, 54, 80, 14});
        check(keyInventory != healthCellInventory,
              "inventory HUD displays the consumable stack");
        check(keyboardHealPrompt != std::vector<Uint32>{} &&
                  playStationHealPrompt != std::vector<Uint32>{} &&
                  keyboardHealPrompt != playStationHealPrompt,
              "the heal prompt shows keyboard and PlayStation-specific controls");

        echo::platform::InventoryMenu inventoryMenu{};
        check(inventoryMenu.confirm(dialogueGame) &&
                  inventoryMenu.source == 0,
              "inventory confirmation picks up the selected bag item");
        inventoryMenu.navigate(1, 0);
        check(inventoryMenu.confirm(dialogueGame) &&
                  dialogueGame.inventory().items[0] == echo::ItemId::None &&
                  dialogueGame.inventory().items[1] == echo::ItemId::MaintenanceKey,
              "shared inventory navigation moves an item without compacting the bag");
        keyboard.drawInventory(dialogueGame, inventoryMenu, "ITEM MOVED");
        const auto keyboardInventoryPrompt = capture(keyboardWindow, {28, 331, 584, 12});
        const auto activeSlotPixels = capture(keyboardWindow, {492, 264, 108, 36});
        pad.drawInventory(dialogueGame, inventoryMenu, "ITEM MOVED");
        check(keyboardInventoryPrompt != capture(padWindow, {28, 331, 584, 12}),
              "inventory uses keyboard and PlayStation-specific navigation prompts");
        keyboard.drawInventory(game, inventoryMenu, "");
        check(activeSlotPixels != capture(keyboardWindow, {492, 264, 108, 36}),
              "the dedicated active slot renders the selected consumable stack");
        check(static_cast<bool>(inventoryMenu.confirm(dialogueGame)),
              "inventory selects the moved key again");
        inventoryMenu.selected = echo::Game::kFirstEquipmentSlot;
        check(inventoryMenu.confirm(dialogueGame).code ==
                  echo::framework::ErrorCode::InvalidValue &&
                  inventoryMenu.source == 1 &&
                  dialogueGame.inventory().items[1] == echo::ItemId::MaintenanceKey &&
                  !inventoryMenu.back() && inventoryMenu.back(),
              "incompatible moves preserve the item and cancel precedes closing");
        inventoryMenu.selected = 19;
        inventoryMenu.navigate(0, 1);
        check(inventoryMenu.selected == echo::Game::kActiveItemSlot,
              "navigation reaches the final active slot from the last backpack slot");

        echo::Game lootRendererGame = dialogueGame;
        echo::GameCheckpoint lootRenderCheckpoint = lootRendererGame.checkpoint();
        lootRenderCheckpoint.room = echo::RoomId::MaintenanceTunnel;
        lootRenderCheckpoint.enemyHealth[0] = 0;
        lootRenderCheckpoint.lootDrops[0] = {
            echo::RoomId::MaintenanceTunnel, {150.0F, 304.0F},
            {echo::ItemId::Scrap, 2}, true};
        check(lootRendererGame.restore(lootRenderCheckpoint),
              "renderer fixture restores an uncollected enemy drop");
        keyboard.drawGame(game, false, false, "SPACE", "J", "E");
        const auto emptyLootRegion = capture(keyboardWindow, {140, 300, 45, 20});
        keyboard.drawGame(lootRendererGame, false, false, "SPACE", "J", "E");
        check(emptyLootRegion != capture(keyboardWindow, {140, 300, 45, 20}),
              "enemy loot is visibly rendered in its world position");

        pad.drawOptions(0, 60, echo::framework::DialogueLanguage::English,
                        false, "SPACE", "J", "E");
        const auto padOptions = capture(padWindow, {74, 38, 492, 284});
        const auto cross = capture(padWindow, {390, 132, 10, 14});
        const auto square = capture(padWindow, {390, 154, 10, 14});
        const auto triangle = capture(padWindow, {390, 176, 10, 14});
        const auto visibleSymbol = [](const std::vector<Uint32>& pixels) {
            return !pixels.empty() && std::any_of(pixels.begin(), pixels.end(),
                [first = pixels.front()](Uint32 pixel) { return pixel != first; });
        };
        check(visibleSymbol(cross) && visibleSymbol(square) && visibleSymbol(triangle),
              "each PlayStation button symbol has a visible bitmap");
        check(cross != square && square != triangle && cross != triangle,
              "PlayStation actions render distinct button symbols");
        pad.drawOptions(0, 60, echo::framework::DialogueLanguage::English,
                        true, "K", "L", "F");
        check(padOptions == capture(padWindow, {74, 38, 492, 284}),
              "PlayStation options do not show keyboard bindings or rebinding instructions");
        pad.drawOptions(4, 60, echo::framework::DialogueLanguage::Czech,
                        false, "SPACE", "J", "E");
        const auto czechOptions = capture(padWindow, {390, 198, 20, 14});
        pad.drawOptions(4, 60, echo::framework::DialogueLanguage::English,
                        false, "SPACE", "J", "E");
        check(czechOptions != capture(padWindow, {390, 198, 20, 14}),
              "Options displays the selected dialogue language");

        keyboard.drawOptions(5, 60, echo::framework::DialogueLanguage::English,
                             false, "SPACE", "J", "E", echo::platform::DisplayMode::Window720);
        const auto windowedRow = capture(keyboardWindow, {390, 220, 160, 14});
        keyboard.drawOptions(5, 60, echo::framework::DialogueLanguage::English,
                             false, "SPACE", "J", "E", echo::platform::DisplayMode::Fullscreen);
        check(echo::platform::Renderer::optionCount() == 8 &&
                  windowedRow != capture(keyboardWindow, {390, 220, 160, 14}),
              "Windows Options lists the selected display mode");
        keyboard.drawOptions(echo::platform::Renderer::kRenderDebugOption, 60,
                             echo::framework::DialogueLanguage::English, false,
                             "SPACE", "J", "E", echo::platform::DisplayMode::Window720, false);
        const auto debugOff = capture(keyboardWindow, {390, 242, 50, 14});
        keyboard.drawOptions(echo::platform::Renderer::kRenderDebugOption, 60,
                             echo::framework::DialogueLanguage::English, false,
                             "SPACE", "J", "E", echo::platform::DisplayMode::Window720, true);
        check(debugOff != capture(keyboardWindow, {390, 242, 50, 14}),
              "Options visibly displays the render debug toggle");

        for (int tick = 0; tick < 2400; ++tick) {
            if (game.roomId() == echo::RoomId::MaintenanceTunnel &&
                game.player().position.x > 930.0F && game.player().position.x < 1040.0F) {
                break;
            }
            const auto update = game.update(
                {.moveX = 1, .jumpPressed = true, .jumpHeld = true, .shootHeld = true});
            if (!update) {
                check(false, "door prompt gameplay fixture updates");
                break;
            }
        }
        check(game.roomId() == echo::RoomId::MaintenanceTunnel &&
                  game.player().position.x > 930.0F && game.player().position.x < 1040.0F,
              "door prompt fixture reaches the laboratory entrance");
        keyboard.drawGame(game, false, false, "SPACE", "J", "E");
        const auto keyboardDoor = capture(keyboardWindow, {520, 285, 12, 14});
        keyboard.drawGame(game, false, false, "SPACE", "J", "F");
        check(keyboardDoor != capture(keyboardWindow, {520, 285, 12, 14}),
              "keyboard door prompt follows the interact binding");
        pad.drawGame(game, false, false, "SPACE", "J", "E");
        const auto padDoor = capture(padWindow, {520, 285, 12, 14});
        pad.drawGame(game, false, false, "SPACE", "J", "F");
        check(padDoor == capture(padWindow, {520, 285, 12, 14}) && padDoor != keyboardDoor,
              "PlayStation door prompt uses the controller symbol");

        keyboard.drawMenu(0);
        pad.drawMenu(0);
        check(capture(keyboardWindow, {86, 326, 492, 16}) != capture(padWindow, {86, 326, 492, 16}),
              "main menu uses platform-specific navigation prompts");
        int menuWidth{}, menuHeight{};
        if (SDL_GetRendererOutputSize(SDL_GetRenderer(keyboardWindow), &menuWidth, &menuHeight) != 0)
            throw std::runtime_error(SDL_GetError());
        std::unique_ptr<SDL_Surface, decltype(&SDL_FreeSurface)> menuSurface{
            SDL_CreateRGBSurfaceWithFormat(0, menuWidth, menuHeight, 32, SDL_PIXELFORMAT_ARGB8888),
            SDL_FreeSurface};
        if (!menuSurface) throw std::runtime_error(SDL_GetError());
        std::unique_ptr<SDL_Renderer, decltype(&SDL_DestroyRenderer)> menuRenderer{
            SDL_CreateSoftwareRenderer(menuSurface.get()), SDL_DestroyRenderer};
        if (!menuRenderer || SDL_RenderSetLogicalSize(menuRenderer.get(), 640, 360) != 0 ||
            SDL_RenderSetIntegerScale(menuRenderer.get(), SDL_TRUE) != 0 ||
            SDL_SetRenderDrawBlendMode(menuRenderer.get(), SDL_BLENDMODE_BLEND) != 0)
            throw std::runtime_error(SDL_GetError());
        SdlMenuSink menuSink{menuRenderer.get()};
        std::vector<Uint32> recordedMenu(static_cast<std::size_t>(menuWidth) * menuHeight);
        const Uint32 whitePixel = SDL_MapRGBA(menuSurface->format, 255, 255, 255, 255);
        std::unique_ptr<SDL_Texture, decltype(&SDL_DestroyTexture)> canvasTexture{
            SDL_CreateTexture(menuRenderer.get(), SDL_PIXELFORMAT_ARGB8888,
                              SDL_TEXTUREACCESS_STATIC, 1, 1), SDL_DestroyTexture};
        if (!canvasTexture ||
            SDL_UpdateTexture(canvasTexture.get(), nullptr, &whitePixel, sizeof(whitePixel)) != 0 ||
            SDL_SetTextureBlendMode(canvasTexture.get(), SDL_BLENDMODE_BLEND) != 0 ||
            !menuSink.registerTexture({0, 1}, canvasTexture.get(), 1, 1) ||
            !menuSink.clear({0, 0, 0, 255}) ||
            !menuSink.texture({0, 1}, {20, 20, 32, 32}, {0, 0, 1, 1},
                              {1, 1, 1, 1}, 0.0F))
            throw std::runtime_error("Portable canvas textured draw failed");
        Uint32 canvasPixel{};
        SDL_Rect canvasSample{20 * menuWidth / 640, 20 * menuHeight / 360, 1, 1};
        if (SDL_RenderReadPixels(menuRenderer.get(), &canvasSample, SDL_PIXELFORMAT_ARGB8888,
                                 &canvasPixel, sizeof(canvasPixel)) != 0)
            throw std::runtime_error(SDL_GetError());
        Uint8 canvasR{}, canvasG{}, canvasB{}, canvasA{};
        SDL_GetRGBA(canvasPixel, menuSurface->format, &canvasR, &canvasG, &canvasB, &canvasA);
        check(canvasR == 255 && canvasG == 255 && canvasB == 255 && canvasA == 255,
              "portable 2D canvas draws a texture with normalized UV coordinates");
        check(!menuSink.texture({0, 1}, {20, 20, 32, 32}, {0.75F, 0, 0.5F, 1},
                                {1, 1, 1, 1}, 0.0F),
              "portable 2D canvas rejects UV regions outside the texture");
        const auto checkRecordedMenu = [&](echo::platform::Renderer& renderer, SDL_Window* window,
                                           int selection, bool canContinue) {
            renderer.drawMenu(selection, canContinue);
            const auto original = capture(window, {0, 0, 640, 360});
            if (!renderer.recordMenu(menuSink, selection, canContinue) ||
                SDL_RenderReadPixels(menuRenderer.get(), nullptr, SDL_PIXELFORMAT_ARGB8888,
                                     recordedMenu.data(), menuWidth * static_cast<int>(sizeof(Uint32))) != 0)
                throw std::runtime_error("Menu recording or readback failed");
            check(original == recordedMenu, "recorded menu exactly matches existing SDL rendering");
        };
        checkRecordedMenu(keyboard, keyboardWindow, 0, false);
        checkRecordedMenu(pad, padWindow, 3, true);
        FailingMenuSink failingMenu{};
        check(!keyboard.recordMenu(failingMenu, 0) && failingMenu.calls == 1,
              "menu recording propagates the first sink failure");
        FailingClearSink failingClear{};
        check(!keyboard.recordMenu(failingClear, 0) && failingClear.fills == 0,
              "failed canvas clear stops recording before any fills");
        checkRecordedMenu(keyboard, keyboardWindow, 2, true);
        echo::Game recordedGame;
        const auto checkRecordedScreen = [&](SDL_Window* window, bool recorded,
                                             std::string_view description) {
            const auto original = capture(window, {0, 0, 640, 360});
            if (!recorded || SDL_RenderReadPixels(menuRenderer.get(), nullptr,
                    SDL_PIXELFORMAT_ARGB8888, recordedMenu.data(),
                    menuWidth * static_cast<int>(sizeof(Uint32))) != 0)
                throw std::runtime_error("Screen recording or readback failed");
            check(original == recordedMenu, description);
        };
        pad.drawOptions(4, 60, echo::framework::DialogueLanguage::Czech,
                        false, "SPACE", "J", "E");
        checkRecordedScreen(padWindow, pad.recordOptions(menuSink, 4, 60,
            echo::framework::DialogueLanguage::Czech, false, "SPACE", "J", "E"),
            "recorded options exactly match SDL");
        pad.drawOptions(echo::platform::Renderer::kRenderDebugOption, 60,
                        echo::framework::DialogueLanguage::English, false, "SPACE", "J", "E",
                        echo::platform::DisplayMode::Window720, true);
        checkRecordedScreen(padWindow, pad.recordOptions(
            menuSink, echo::platform::Renderer::kRenderDebugOption, 60,
            echo::framework::DialogueLanguage::English, false, "SPACE", "J", "E",
            echo::platform::DisplayMode::Window720, true),
            "recorded enabled render debug option matches SDL");
        echo::platform::InventoryMenu recordedInventory{};
        recordedInventory.selected = 2;
        pad.drawInventory(recordedGame, recordedInventory, "TEST MESSAGE");
        checkRecordedScreen(padWindow, pad.recordInventory(menuSink, recordedGame,
            recordedInventory, "TEST MESSAGE"), "recorded inventory exactly matches SDL");
        echo::input::ControllerSnapshot labFixture{};
        labFixture.connected = true;
        echo::input::setText(labFixture.name, "Simulated DualSense");
        labFixture.axes[SDL_CONTROLLER_AXIS_LEFTX] = 16000;
        labFixture.touchpadApiAvailable = true;
        echo::input::TouchSample labTouch{0, 0, true, 0.5F, 0.5F, 0.75F};
        labFixture.touches = &labTouch;
        labFixture.touchSampleCount = 1;
        pad.drawDualSenseLab(labFixture, 1);
        checkRecordedScreen(padWindow, pad.recordDualSenseLab(menuSink, labFixture, 1),
            "recorded DualSense Lab exactly matches SDL including stick and touch outlines");
        FailingMenuSink failingOptions{};
        check(!pad.recordOptions(failingOptions, 0, 60, echo::framework::DialogueLanguage::English,
                                 false, "SPACE", "J", "E") && failingOptions.calls == 1,
              "options recording propagates the first sink failure");
        const auto checkRecordedGame = [&](bool showMap) {
            keyboard.drawGame(recordedGame, showMap, false, "SPACE", "J", "E");
            const auto original = capture(keyboardWindow, {0, 0, 640, 360});
            if (!keyboard.recordGame(menuSink, recordedGame, showMap, false, "SPACE", "J", "E") ||
                SDL_RenderReadPixels(menuRenderer.get(), nullptr, SDL_PIXELFORMAT_ARGB8888,
                                     recordedMenu.data(), menuWidth * static_cast<int>(sizeof(Uint32))) != 0)
                throw std::runtime_error("Game recording or readback failed");
            check(original == recordedMenu, "recorded fallback gameplay exactly matches SDL");
        };
        checkRecordedGame(false);
        checkRecordedGame(true);
        echo::Game calmShakeGame = recordedGame;
        auto shakeCheckpoint = calmShakeGame.checkpoint();
        shakeCheckpoint.room = echo::RoomId::MaintenanceTunnel;
        shakeCheckpoint.playerPosition = {760.0F, 292.0F};
        shakeCheckpoint.enemyHealth[2] = 0;
        check(calmShakeGame.restore(shakeCheckpoint),
              "camera-shake render fixture restores beside an enemy");
        echo::Game shakeGame = calmShakeGame;
        const int shakeHealth = shakeGame.player().health;
        const auto shakeUpdate = shakeGame.update({});
        const echo::Vec2 shakeOffset = shakeGame.cameraShakeOffset();
        check(static_cast<bool>(shakeUpdate) && shakeGame.player().health < shakeHealth &&
                  std::abs(shakeOffset.x) >= 6.0F && std::abs(shakeOffset.y) >= 3.0F,
              "accepted render-fixture damage produces visible camera shake");
        keyboard.drawGame(calmShakeGame, false, false, "SPACE", "J", "E");
        const auto calmWorld = capture(keyboardWindow, {220, 200, 200, 140});
        const auto calmHud = capture(keyboardWindow, {470, 12, 158, 38});
        keyboard.drawGame(shakeGame, false, false, "SPACE", "J", "E");
        const auto shakenWorld = capture(keyboardWindow, {220, 200, 200, 140});
        const auto shakenHud = capture(keyboardWindow, {470, 12, 158, 38});
        const auto shakenScreen = capture(keyboardWindow, {0, 0, 640, 360});
        check(shakenWorld != calmWorld && shakenHud == calmHud,
              "camera shake moves world imagery without shifting the HUD");
        check(keyboard.recordGame(menuSink, shakeGame, false, false, "SPACE", "J", "E") &&
                  SDL_RenderReadPixels(menuRenderer.get(), nullptr, SDL_PIXELFORMAT_ARGB8888,
                                       recordedMenu.data(),
                                       menuWidth * static_cast<int>(sizeof(Uint32))) == 0 &&
                  shakenScreen == recordedMenu,
              "SDL and abstract canvas render camera shake identically");
        keyboard.drawGame(recordedGame, false, false, "SPACE", "J", "E");
        const auto unflashed = capture(keyboardWindow, {0, 100, 640, 100});
        keyboard.drawGame(recordedGame, false, false, "SPACE", "J", "E", {}, nullptr, 64);
        const auto flashed = capture(keyboardWindow, {0, 0, 640, 360});
        check(unflashed != capture(keyboardWindow, {0, 100, 640, 100}),
              "damage flash visibly tints the gameplay world");
        check(keyboard.recordGame(menuSink, recordedGame, false, false, "SPACE", "J", "E",
                                  nullptr, nullptr, {}, nullptr, 64) &&
                  SDL_RenderReadPixels(menuRenderer.get(), nullptr, SDL_PIXELFORMAT_ARGB8888,
                                       recordedMenu.data(),
                                       menuWidth * static_cast<int>(sizeof(Uint32))) == 0 &&
                  flashed == recordedMenu,
              "damage flash matches in SDL and abstract canvas");
        echo::platform::RenderDebugInfo debugInfo{};
        debugInfo.vulkan = true;
        debugInfo.timingAvailable = true;
        debugInfo.backendStatsAvailable = true;
        debugInfo.fps = 60;
        debugInfo.frameTenthsMs = 167;
        debugInfo.drawCalls = 12;
        debugInfo.triangles = 123;
        keyboard.drawGame(recordedGame, false, false, "SPACE", "J", "E");
        const auto withoutDebug = capture(keyboardWindow, {470, 68, 158, 53});
        keyboard.drawGame(recordedGame, false, false, "SPACE", "J", "E", {}, &debugInfo);
        const auto withDebug = capture(keyboardWindow, {0, 0, 640, 360});
        check(withoutDebug != capture(keyboardWindow, {470, 68, 158, 53}),
              "render debug overlay appears only when enabled");
        check(keyboard.recordGame(menuSink, recordedGame, false, false, "SPACE", "J", "E",
                                  nullptr, nullptr, {}, &debugInfo) &&
                  SDL_RenderReadPixels(menuRenderer.get(), nullptr, SDL_PIXELFORMAT_ARGB8888,
                                       recordedMenu.data(),
                                       menuWidth * static_cast<int>(sizeof(Uint32))) == 0 &&
                  withDebug == recordedMenu,
              "render debug overlay matches in SDL and abstract canvas");
        const auto firstStats = capture(keyboardWindow, {478, 88, 140, 21});
        debugInfo.fps = 30;
        debugInfo.drawCalls = 99;
        keyboard.drawGame(recordedGame, false, false, "SPACE", "J", "E", {}, &debugInfo);
        check(firstStats != capture(keyboardWindow, {478, 88, 140, 21}),
              "render debug overlay displays updated timing and draw statistics");
        echo::vfx::Particle spark{};
        spark.position = {recordedGame.player().position.x + 10.0F,
                          recordedGame.player().position.y + 14.0F, 0.0F};
        spark.color = {0.15F, 0.9F, 1.0F, 0.9F};
        spark.scale = 1.0F;
        const std::array<echo::vfx::Particle, 1> recordedParticles{spark};
        keyboard.drawGame(recordedGame, false, false, "SPACE", "J", "E", recordedParticles);
        const auto particleScreen = capture(keyboardWindow, {0, 0, 640, 360});
        check(keyboard.recordGame(menuSink, recordedGame, false, false, "SPACE", "J", "E",
                                  nullptr, nullptr, recordedParticles) &&
                  SDL_RenderReadPixels(menuRenderer.get(), nullptr, SDL_PIXELFORMAT_ARGB8888,
                                       recordedMenu.data(),
                                       menuWidth * static_cast<int>(sizeof(Uint32))) == 0 &&
                  particleScreen == recordedMenu,
              "SDL and abstract canvas render gameplay particles identically");
        spark.position.x += 20.0F;
        spark.color = {1.0F, 0.6F, 0.1F, 1.0F};
        const std::array<echo::vfx::Particle, 1> impacts{spark};
        keyboard.drawGame(recordedGame, false, false, "SPACE", "J", "E",
                          recordedParticles, nullptr, 0, impacts);
        const auto impactScreen = capture(keyboardWindow, {0, 0, 640, 360});
        check(impactScreen != particleScreen &&
                  keyboard.recordGame(menuSink, recordedGame, false, false, "SPACE", "J", "E",
                                      nullptr, nullptr, recordedParticles, nullptr, 0, impacts) &&
                  SDL_RenderReadPixels(menuRenderer.get(), nullptr, SDL_PIXELFORMAT_ARGB8888,
                                       recordedMenu.data(),
                                       menuWidth * static_cast<int>(sizeof(Uint32))) == 0 &&
                  impactScreen == recordedMenu,
              "SDL and abstract canvas render impact and shot particles identically");
        FailingMenuSink failingGame{};
        check(!keyboard.recordGame(failingGame, recordedGame, false, false, "SPACE", "J", "E") &&
                  failingGame.calls == 1,
              "game recording propagates sink failures");
        echo::platform::CanvasSprites canvasSprites{};
        canvasSprites[static_cast<std::size_t>(echo::platform::Sprite::Player)] = {{73, 1}, 192, 32};
        SpriteProbeSink spriteProbe{};
        check(keyboard.recordGame(spriteProbe, recordedGame, false, false, "SPACE", "J", "E",
                                  &canvasSprites) &&
                  spriteProbe.calls == 1 && spriteProbe.lastHandle.index == 73 &&
                  std::abs(spriteProbe.lastUv.z - 0.125F) < 0.0001F,
              "game recording submits the player sprite with its atlas frame");
        spriteProbe = {};
        spriteProbe.fail = true;
        check(!keyboard.recordGame(spriteProbe, recordedGame, false, false, "SPACE", "J", "E",
                                   &canvasSprites) && spriteProbe.calls == 1,
              "game recording propagates texture submission failure");
        canvasSprites[static_cast<std::size_t>(echo::platform::Sprite::Player)].width = 1;
        check(!keyboard.recordGame(spriteProbe, recordedGame, false, false, "SPACE", "J", "E",
                                   &canvasSprites),
              "game recording rejects invalid sprite sheet dimensions");
        keyboard.drawMenu(3);
        const auto labMenuEntry = capture(keyboardWindow, {120, 246, 230, 24});
        keyboard.drawMenu(2);
        check(labMenuEntry != capture(keyboardWindow, {120, 246, 230, 24}),
              "main menu exposes a distinct DualSense Lab entry");

        echo::input::ControllerSnapshot simulatedController;
        simulatedController.connected = true;
        echo::input::setText(simulatedController.name, "Simulated DualSense");
        simulatedController.battery = SDL_JOYSTICK_POWER_FULL;
        keyboard.drawDualSenseLab(simulatedController, 1);
        const auto idleCross = capture(keyboardWindow, {229, 181, 44, 16});
        const auto centeredStick = capture(keyboardWindow, {63, 267, 38, 38});
        simulatedController.buttons[SDL_CONTROLLER_BUTTON_A] = true;
        simulatedController.axes[SDL_CONTROLLER_AXIS_LEFTX] = 16000;
        keyboard.drawDualSenseLab(simulatedController, 1);
        check(idleCross != capture(keyboardWindow, {229, 181, 44, 16}),
              "DualSense Lab highlights an observed button press");
        check(centeredStick != capture(keyboardWindow, {63, 267, 38, 38}),
              "DualSense Lab moves the live stick indicator");
        simulatedController.rumbleAvailable = true;
        keyboard.drawDualSenseLab(simulatedController, 1);
        const auto rumbleAvailable = capture(keyboardWindow, {344, 246, 270, 10});
        simulatedController.pendingConfirmation = echo::input::OutputTest::Rumble;
        keyboard.drawDualSenseLab(simulatedController, 1);
        const auto rumblePending = capture(keyboardWindow, {344, 246, 270, 10});
        simulatedController.pendingConfirmation = echo::input::OutputTest::None;
        simulatedController.rumbleHardwareConfirmed = true;
        keyboard.drawDualSenseLab(simulatedController, 1);
        const auto rumbleConfirmed = capture(keyboardWindow, {344, 246, 270, 10});
        check(rumblePending != rumbleAvailable && rumbleConfirmed != rumbleAvailable &&
                 rumbleConfirmed != rumblePending,
              "DualSense Lab shows pending and recorded output confirmations");
        keyboard.drawGame(game, false, true, "SPACE", "J", "E");
        pad.drawGame(game, false, true, "SPACE", "J", "E");
        check(capture(keyboardWindow, {195, 199, 260, 14}) != capture(padWindow, {195, 199, 260, 14}),
              "pause menu uses platform-specific resume and back prompts");

        const SDL_Rect playerRegion{static_cast<int>(game.player().position.x - game.cameraX()) - 2,
                                    static_cast<int>(game.player().position.y) - 2, 24, 32};
        pad.drawGame(game, false, false, "SPACE", "J", "E");
        const auto fallbackPlayer = capture(padWindow, playerRegion);
        check(pad.loadSprites("Z:/project-echo-missing-assets/") == 0 &&
                  !pad.hasSprite(echo::platform::Sprite::Player),
              "missing asset directory keeps the built-in art");
        pad.drawGame(game, false, false, "SPACE", "J", "E");
        check(fallbackPlayer == capture(padWindow, playerRegion), "fallback art renders without sprites");
        echo::platform::RgbaImage decodedPlayer{};
        check(echo::platform::loadPngRgba(ECHO_SOURCE_ASSETS "sprites/player.png", decodedPlayer) &&
                  decodedPlayer.pixels != nullptr && decodedPlayer.width >= 24 &&
                  decodedPlayer.width % 24 == 0 && decodedPlayer.height == 32,
              "shared RGBA PNG decoder returns bounded player atlas pixels");
        echo::platform::freePngRgba(decodedPlayer);
        check(decodedPlayer.pixels == nullptr && decodedPlayer.width == 0 &&
                  decodedPlayer.height == 0,
              "decoded PNG storage releases and resets its output");
        check(pad.loadSprites(ECHO_SOURCE_ASSETS) == static_cast<int>(echo::platform::Sprite::Count),
              "all packaged PNG sprite sheets decode with expected dimensions");
        check(!pad.recordGame(menuSink, recordedGame, false, false, "SPACE", "J", "E"),
              "game recording explicitly rejects loaded SDL textures");
        pad.drawGame(game, false, false, "SPACE", "J", "E");
        check(fallbackPlayer != capture(padWindow, playerRegion), "player is drawn from the PNG sprite sheet");

        const echo::WorldLayout* worldLayout = integration.worldLayout();
        check(worldLayout != nullptr, "the packaged game selects JSON world data");
        if (worldLayout != nullptr) {
            echo::Game worldGame;
            check(static_cast<bool>(worldGame.configureWorld(*worldLayout)),
                  "the packaged world layout configures a game");
            canvasSprites[static_cast<std::size_t>(echo::platform::Sprite::Player)].width = 192;
            check(pad.loadWorldSprites(ECHO_SOURCE_ASSETS, worldGame.world()) == 20 &&
                      pad.worldSheetCount() == 20,
                  "every world sprite sheet and tileset referenced by JSON decodes");
            echo::platform::CanvasWorldSprites canvasWorldSprites{};
            const auto addWorldSprite = [&](std::string_view sheetPath) {
                if (sheetPath.empty()) return true;
                for (std::size_t index = 0; index < canvasWorldSprites.count; ++index) {
                    if (sheetPath == canvasWorldSprites.sheets[index].path.data()) return true;
                }
                if (canvasWorldSprites.count == canvasWorldSprites.sheets.size() ||
                    sheetPath.size() >= echo::platform::CanvasWorldSprite{}.path.size()) {
                    return false;
                }
                echo::platform::CanvasWorldSprite& entry =
                    canvasWorldSprites.sheets[canvasWorldSprites.count];
                std::copy(sheetPath.begin(), sheetPath.end(), entry.path.begin());
                std::array<char, 512> pngPath{};
                const int written = std::snprintf(pngPath.data(), pngPath.size(), "%s%.*s",
                    ECHO_SOURCE_ASSETS, static_cast<int>(sheetPath.size()), sheetPath.data());
                echo::platform::RgbaImage decoded{};
                if (written <= 0 || written >= static_cast<int>(pngPath.size()) ||
                    !echo::platform::loadPngRgba(pngPath.data(), decoded)) {
                    return false;
                }
                entry.image = {{static_cast<std::uint32_t>(canvasWorldSprites.count + 101), 1},
                               static_cast<std::uint32_t>(decoded.width),
                               static_cast<std::uint32_t>(decoded.height)};
                echo::platform::freePngRgba(decoded);
                ++canvasWorldSprites.count;
                return true;
            };
            const auto addObjectSprite = [&](const echo::framework::ObjectDefinition* object) {
                return object == nullptr || addWorldSprite(object->sprite().sheet.data());
            };
            const echo::WorldLayout& loadedWorld = worldGame.world();
            const auto& archetypes = loadedWorld.archetypes;
            bool worldSpritesLoaded =
                addObjectSprite(archetypes.player) && addObjectSprite(archetypes.playerIso) &&
                addObjectSprite(archetypes.engineer) && addObjectSprite(archetypes.pickup) &&
                addObjectSprite(archetypes.projectile) &&
                addObjectSprite(archetypes.enemyProjectile) && addObjectSprite(archetypes.loot);
            for (const echo::EnemySpawn& enemy : loadedWorld.enemies) {
                worldSpritesLoaded = worldSpritesLoaded && addObjectSprite(enemy.archetype);
            }
            for (const echo::EnemySpawn& enemy : loadedWorld.roamerList()) {
                worldSpritesLoaded = worldSpritesLoaded && addObjectSprite(enemy.archetype);
            }
            for (const echo::DoorLayout& door : loadedWorld.doorList()) {
                worldSpritesLoaded = worldSpritesLoaded && addObjectSprite(door.archetype);
            }
            for (const echo::PropLayout& prop : loadedWorld.propList()) {
                worldSpritesLoaded = worldSpritesLoaded && addObjectSprite(prop.archetype);
            }
            for (const echo::RoomLayout& room : loadedWorld.rooms) {
                if (room.definition == nullptr) continue;
                for (const echo::framework::TilesetSpec& tileset : room.definition->tilesets()) {
                    worldSpritesLoaded =
                        worldSpritesLoaded && addWorldSprite(tileset.sheet.data());
                }
            }
            check(worldSpritesLoaded && canvasWorldSprites.count == 20,
                  "world sprite handles are populated from every JSON-referenced sheet");
            spriteProbe = {};
            check(keyboard.recordGame(spriteProbe, worldGame, false, false, "SPACE", "J", "E",
                                      &canvasSprites, &canvasWorldSprites) &&
                      spriteProbe.calls > 1 && spriteProbe.rotatedCalls > 0,
                  "recording submits JSON room tiles and object atlas frames to canvas handles");
            const auto landingCheckpoint = worldGame.checkpoint();
            const auto* landingDefinition = worldGame.currentRoom().definition;
            const float initialLayerX =
                static_cast<float>(spriteProbe.firstDestination.x);
            const auto expectedFirstLayerX = [&](float cameraX) {
                if (landingDefinition == nullptr || landingDefinition->layers().empty()) return -1.0F;
                const auto& layer = landingDefinition->layers()[0];
                const float layerCameraX = cameraX * layer.scrollFactorX;
                const int firstColumn = static_cast<int>(layerCameraX) / 32;
                for (int row = 0; row < landingDefinition->rows(); ++row) {
                    for (int column = firstColumn; column < landingDefinition->columns(); ++column) {
                        if (landingDefinition->tile(0, column, row) != nullptr) {
                            return static_cast<float>(column * 32) - layerCameraX;
                        }
                    }
                }
                return -1.0F;
            };
            check(landingDefinition != nullptr && worldGame.cameraX() == 0.0F &&
                      std::abs(initialLayerX - expectedFirstLayerX(worldGame.cameraX())) < 0.1F,
                  "the Landing Bay background layer starts at its declared parallax offset");
            auto scrolledCheckpoint = landingCheckpoint;
            scrolledCheckpoint.playerPosition = {1000.0F, 280.0F};
            check(static_cast<bool>(worldGame.restore(scrolledCheckpoint)),
                  "parallax fixture moves the player camera through the Landing Bay");
            spriteProbe = {};
            const bool recordedScrolledBay =
                keyboard.recordGame(spriteProbe, worldGame, false, false, "SPACE", "J", "E",
                                    &canvasSprites, &canvasWorldSprites);
            check(recordedScrolledBay &&
                      std::abs(static_cast<float>(spriteProbe.firstDestination.x) -
                               expectedFirstLayerX(worldGame.cameraX())) < 0.1F &&
                      std::abs(static_cast<float>(spriteProbe.firstDestination.x) -
                               initialLayerX) > 1.0F &&
                      static_cast<bool>(worldGame.restore(landingCheckpoint)),
                  "canvas recording applies the JSON parallax factor as the camera moves");
            echo::platform::CanvasWorldSprites invalidWorldSprites = canvasWorldSprites;
            invalidWorldSprites.sheets[0].path[0] = '\\';
            check(!keyboard.recordGame(spriteProbe, worldGame, false, false, "SPACE", "J", "E",
                                       &canvasSprites, &invalidWorldSprites),
                  "recording rejects unsafe world sprite paths");
            const SDL_Rect floorRegion{0, 320, 640, 32};
            pad.drawGame(game, false, false, "SPACE", "J", "E");
            const auto builtInFloor = capture(padWindow, floorRegion);
            pad.drawGame(worldGame, false, false, "SPACE", "J", "E");
            check(builtInFloor != capture(padWindow, floorRegion),
                  "JSON tile layers draw the room from its tilesets");

            echo::GameCheckpoint cargo = worldGame.checkpoint();
            cargo.room = echo::RoomId::CargoHold;
            cargo.playerPosition = {600.0F, 170.0F};
            echo::Game cargoGame;
            check(static_cast<bool>(cargoGame.configureWorld(*worldLayout)) &&
                      cargoGame.restore(cargo) && cargoGame.isometric(),
                  "the JSON Cargo Hold is an isometric room");
            spriteProbe = {};
            check(keyboard.recordGame(spriteProbe, cargoGame, false, false, "SPACE", "J", "E",
                                      &canvasSprites, &canvasWorldSprites) &&
                      spriteProbe.calls > 1,
                  "isometric JSON tiles and directional object atlases record through canvas");
            echo::vfx::Particle isoImpact{};
            isoImpact.position = {cargoGame.player().position.x + 30.0F,
                                  cargoGame.player().position.y + 10.0F, 0.0F};
            isoImpact.color = {1.0F, 0.6F, 0.1F, 1.0F};
            isoImpact.scale = 1.0F;
            const std::array<echo::vfx::Particle, 1> isoImpacts{isoImpact};
            auto isoExplosion = isoImpact;
            isoExplosion.color = {1.0F, 0.92F, 0.45F, 0.9F};
            isoExplosion.scale = 1.8F;
            const std::array<echo::vfx::Particle, 1> isoExplosions{isoExplosion};
            auto isoFog = isoImpact;
            isoFog.position = {330.0F, 180.0F, 0.0F};
            isoFog.color = {0.66F, 0.78F, 0.84F, 0.18F};
            isoFog.scale = 2.5F;
            const std::array<echo::vfx::Particle, 1> isoFogParticles{isoFog};
            keyboard.drawGame(cargoGame, false, false, "SPACE", "J", "E");
            const auto isoWithoutImpact = capture(keyboardWindow, {0, 0, 640, 360});
            keyboard.drawGame(cargoGame, false, false, "SPACE", "J", "E",
                              {}, nullptr, 0, isoImpacts, isoExplosions, isoFogParticles);
            const auto isoWithImpact = capture(keyboardWindow, {0, 0, 640, 360});
            check(isoWithImpact != isoWithoutImpact &&
                      keyboard.recordGame(menuSink, cargoGame, false, false, "SPACE", "J", "E",
                                          nullptr, nullptr, {}, nullptr, 0, isoImpacts,
                                          isoExplosions, isoFogParticles) &&
                      SDL_RenderReadPixels(menuRenderer.get(), nullptr, SDL_PIXELFORMAT_ARGB8888,
                                           recordedMenu.data(),
                                           menuWidth * static_cast<int>(sizeof(Uint32))) == 0 &&
                      isoWithImpact == recordedMenu,
                  "isometric impacts, explosions and fog match in SDL and abstract canvas");
            const SDL_Rect centre{260, 120, 120, 120};
            pad.drawGame(worldGame, false, false, "SPACE", "J", "E");
            const auto sideCentre = capture(padWindow, centre);
            pad.drawGame(cargoGame, false, false, "SPACE", "J", "E");
            const auto isoCentre = capture(padWindow, centre);
            cargo.playerPosition = {200.0F, 280.0F};
            check(cargoGame.restore(cargo), "isometric camera fixture moves the player");
            pad.drawGame(cargoGame, false, false, "SPACE", "J", "E");
            check(isoCentre != sideCentre && isoCentre != capture(padWindow, centre),
                  "isometric rooms render a player-following diamond grid");
        }

        echo::platform::SoundBuffer sound;
        check(echo::platform::loadWavMono16(ECHO_SOURCE_ASSETS "sounds/shot.wav", 48000, sound) &&
                  sound.count == 4320,
              "48 kHz WAV loads without resampling");
        check(echo::platform::loadWavMono16(ECHO_SOURCE_ASSETS "sounds/shot.wav", 44100, sound) &&
                  sound.count > 3900 && sound.count < 4100,
              "WAV is converted to the audio device rate");
        echo::platform::freeSound(sound);
        check(echo::platform::loadPngTexture(SDL_GetRenderer(padWindow), ECHO_SOURCE_ASSETS "sounds/shot.wav") ==
                  nullptr,
              "non-PNG data is rejected by the sprite decoder");
        check(!echo::platform::loadPngRgba(ECHO_SOURCE_ASSETS "sounds/shot.wav", decodedPlayer) &&
                  decodedPlayer.pixels == nullptr,
              "shared RGBA PNG decoder rejects non-PNG asset data");
    } catch (const std::exception& error) {
        ++failures;
        std::cerr << "SDL prompt test failed: " << error.what() << '\n';
    }
    pad.destroy();
    keyboard.destroy();
    SDL_Quit();
    if (failures != 0) {
        return EXIT_FAILURE;
    }
    std::cout << "Platform-specific SDL prompts passed\n";
    return EXIT_SUCCESS;
}
