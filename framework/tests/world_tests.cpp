#include "echo/framework/world.hpp"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {
using namespace echo::framework;

int failures{};
void check(bool value, std::string_view message) {
    if (!value) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

struct Source {
    std::string id;
    std::string json;
};

Status parseInto(std::string_view source, JsonDocument& document) {
    JsonError error{};
    return JsonDocument::parse(source.data(), source.size(), document, error);
}

// Loads a manifest plus documents; returns the first failing status.
Status build(WorldModule& world, const std::vector<Source>& objects,
             const std::vector<Source>& rooms, std::string_view startRoom = "echo:room/a") {
    world.reset();
    std::string manifest = R"({"schemaVersion":1,"startRoom":")" + std::string(startRoom) + R"(","objects":[)";
    for (std::size_t index = 0; index < objects.size(); ++index) {
        manifest += (index == 0 ? "\"" : ",\"") + objects[index].id + "\"";
    }
    manifest += R"(],"rooms":[)";
    for (std::size_t index = 0; index < rooms.size(); ++index) {
        manifest += (index == 0 ? "\"" : ",\"") + rooms[index].id + "\"";
    }
    manifest += "]}";
    JsonDocument document{};
    Status status = parseInto(manifest, document);
    if (status) status = world.loadManifest(document.root());
    for (const Source& object : objects) {
        if (!status) return status;
        JsonDocument item{};
        status = parseInto(object.json, item);
        if (status) status = world.addObject(object.id, item.root());
    }
    for (const Source& room : rooms) {
        if (!status) return status;
        JsonDocument item{};
        status = parseInto(room.json, item);
        if (status) status = world.addRoom(room.id, item.root());
    }
    return status ? world.finalize() : status;
}

std::string row(char fill, int columns = 20) { return std::string(static_cast<std::size_t>(columns), fill); }

const std::string kBase = R"({"schemaVersion":1,"id":"echo:object/base","abstract":true,"category":"enemy",
  "sprite":{"sheet":"sprites/base.png","frameWidth":32,"frameHeight":32,"offset":[-2,-4],
    "material":{"normalSheet":"sprites/base_normal.png","emissiveSheet":"sprites/base_emissive.png",
      "emissiveColor":[0.4,0.5,0.6],"emissiveIntensity":2}},
  "bounds":[28,28],"spawn":{"position":[100,200],"facing":"left"},
  "animations":{"idle":{"row":0,"column":0,"frames":2,"fps":4},"walk":{"row":1,"frames":4,"fps":8}},
  "defaultAnimation":"idle","properties":{"health":2,"speed":45,"behavior":"patrol"}})";
const std::string kAdvanced = R"({"schemaVersion":1,"id":"echo:object/advanced","parent":"echo:object/base",
  "sprite":{"sheet":"sprites/advanced.png","material":{"emissiveSheet":"sprites/advanced_emissive.png",
    "emissiveIntensity":0.75,"emissiveCircuit":"echo:power/main"}},
  "animations":{"walk":{"frames":6},"attack":{"row":2,"frames":3,"fps":12,"loop":false}},
  "properties":{"health":5,"armored":true}})";

std::string room(std::string_view id, std::string_view extra, std::string_view parent = {}) {
    std::string json = R"({"schemaVersion":1,"id":")" + std::string(id) + "\"";
    if (!parent.empty()) json += R"(,"parent":")" + std::string(parent) + "\"";
    return json + std::string(extra) + "}";
}

std::string layers(std::string_view name, const std::vector<std::string>& rows) {
    std::string json = R"(,"layers":[{"name":")" + std::string(name) + R"(","rows":[)";
    for (std::size_t index = 0; index < rows.size(); ++index) {
        json += (index == 0 ? "\"" : ",\"") + rows[index] + "\"";
    }
    return json + "]}]";
}

const std::string kTemplate = room("echo:room/template",
    R"(,"abstract":true,"name":"Template","size":{"columns":20,"rows":3},"background":[1,2,3],
    "tilesets":[{"id":"station","sheet":"sprites/tiles/station.png","tileWidth":32,"tileHeight":32,
      "material":{"normalSheet":"sprites/tiles/station_normal.png"}}],
    "tiles":{"#":{"tileset":"station","x":0,"y":0,"collision":"solid"},
             "^":{"tileset":"station","x":1,"y":0,"collision":"hazard"},
             "~":{"tileset":"station","x":2,"y":0}},
    "exits":{"left":{}},"objects":[{"id":"guard","object":"echo:object/advanced","position":[64,64]}],
    "lighting":{"ambient":{"color":[0.7,0.8,1.0],"intensity":0.35},
      "directional":{"direction":[0.0,1.0],"color":[1.0,0.9,0.7],"intensity":0.2},
      "lights":[
        {"id":"echo:light/main_lamp","type":"point","position":[128,96],"radius":180,
         "color":[1.0,0.7,0.3],"intensity":1.5,"fadeSeconds":1.5,
         "attenuation":[1.0,0.0,0.25],
         "parent":"guard","circuit":"echo:power/main","enabled":true,
         "flicker":{"amount":0.15,"frequency":4.0},"persistence":"persistent"},
        {"id":"echo:light/spot","type":"spot","position":[256,96],"parent":"player",
         "followsAim":true,"coneAngle":50.0,"radius":220,"color":[0.6,0.8,1.0],"intensity":0.8},
        {"id":"echo:light/player","type":"point","position":[12,14],"radius":96,
         "color":[0.2,0.8,1.0],"intensity":0.5,"parent":"player"},
        {"id":"echo:light/enemy","type":"point","position":[16,8],"radius":72,
         "color":[1.0,0.2,0.1],"intensity":0.4,"parent":"enemy.guard"}]})");
}

int main() {
    auto world = std::make_unique<WorldModule>();

    const std::string roomA = room("echo:room/a",
        R"(,"name":"Alpha","tilesets":[{"id":"extra","sheet":"sprites/tiles/extra.png","tileWidth":16,"tileHeight":16,"margin":1,"spacing":2}],
        "tiles":{"=":{"tileset":"extra","x":3,"y":1,"rotation":90,"flipX":true,"collision":"solid"},"~":{"tileset":"station","x":2,"y":0,"collision":"hazard"}})" +
            std::string(R"(,"layers":[{"name":"back","scrollFactorX":0.35,"rows":[")") + row('.') + "\",\"" + row('~') + "\",\"" + row('.') +
            R"("]},{"name":"main","rows":[")" + row('.') + "\",\"" + row('#') + "\",\"=" + row('#', 19) + R"("]}])" +
            R"(,"exits":{"right":{"room":"echo:room/b","requiresAbility":"echo:ability/step"}},
            "objects":[{"id":"guard","object":"echo:object/advanced","position":[32,32],"facing":"right","properties":{"health":9}},
                       {"id":"plain","object":"echo:object/plain"}])",
        "echo:room/template");
    const std::string roomB = room("echo:room/b",
        R"(,"name":"Beta","projection":"isometric","size":{"columns":24,"rows":2},"exits":{"right":{"completesGame":true}})" +
            layers("main", {row('.', 24), row('#', 24)}),
        "echo:room/template");
    const std::string roomC = room("echo:room/c",
        R"(,"name":"Top Down Fixture","projection":"top-down","size":{"columns":20,"rows":3})" +
            layers("main", {row('.', 20), row('.', 20), row('.', 20)}),
        "echo:room/template");
    const std::string plain = R"({"schemaVersion":1,"id":"echo:object/plain","category":"prop",
      "sprite":{"sheet":"sprites/plain.png","frameWidth":16,"frameHeight":16},"bounds":[16,16],
      "animations":{"idle":{"frames":1,"fps":1}}})";

    const std::vector<Source> objects{{"echo:object/advanced", kAdvanced},
                                      {"echo:object/base", kBase},
                                      {"echo:object/plain", plain}};
    const std::vector<Source> rooms{{"echo:room/a", roomA}, {"echo:room/template", kTemplate},
                                    {"echo:room/b", roomB}, {"echo:room/c", roomC}};
    const Status loaded = build(*world, objects, rooms);
    check(static_cast<bool>(loaded), "valid world with inheritance loads");
    if (!loaded) {
        std::cerr << loaded.subject << ": " << loaded.detail << '\n';
        return EXIT_FAILURE;
    }

    const ObjectDefinition* advanced = world->findObject("echo:object/advanced");
    check(advanced != nullptr && !advanced->isAbstract() &&
              advanced->category() == ObjectCategory::Enemy,
          "child inherits the parent category and is concrete");
    check(advanced != nullptr && std::string_view(advanced->sprite().sheet.data()) == "sprites/advanced.png" &&
              advanced->sprite().frameWidth == 32 && advanced->sprite().offsetY == -4,
          "child overrides only the sprite sheet and keeps inherited frame data");
    check(advanced != nullptr && advanced->width() == 28.0F && advanced->spawnX() == 100.0F &&
              advanced->facing() == Facing::Left,
          "bounds, spawn position and facing are inherited");
    const AnimationSpec* walk = advanced != nullptr ? advanced->findAnimation("walk") : nullptr;
    check(walk != nullptr && walk->frames == 6 && walk->fps == 8 && walk->row == 1,
          "animations merge field by field");
    const AnimationSpec* attack = advanced != nullptr ? advanced->findAnimation("attack") : nullptr;
    check(attack != nullptr && !attack->loop && advanced->animations().size() == 3,
          "children add new animations");
    check(advanced != nullptr && advanced->animationOrDefault("missing") == advanced->findAnimation("idle"),
          "unknown animation names fall back to the default animation");
    const ObjectProperty* health = advanced != nullptr ? advanced->property("health") : nullptr;
    const ObjectProperty* behavior = advanced != nullptr ? advanced->property("behavior") : nullptr;
    const ObjectProperty* armored = advanced != nullptr ? advanced->property("armored") : nullptr;
    check(health != nullptr && health->integer == 5 && behavior != nullptr &&
              std::string_view(behavior->text.data()) == "patrol" && armored != nullptr &&
              armored->type == ObjectPropertyType::Boolean && armored->boolean,
          "properties merge by key");
    const ObjectDefinition* base = world->findObject("echo:object/base");
    check(base != nullptr && base->isAbstract() && base->property("armored") == nullptr,
          "parents are not modified by their children");

    // Non-adjacent cells, tint, rotation and spin, including inheritance of each.
    const std::string poseBase = R"({"schemaVersion":1,"id":"echo:object/pose","category":"prop",
      "sprite":{"sheet":"sprites/pose.png","frameWidth":16,"frameHeight":16,"tint":[1,0.5,0.25],"rotation":10},
      "bounds":[16,16],"animations":{"idle":{"cells":[[0,0],[3,0],[1,2]],"fps":4,"tint":[0.5,1,1,0.5],
        "rotation":45,"spin":90},"walk":{"row":1,"frames":2,"fps":4}}})";
    const std::string poseChild = R"({"schemaVersion":1,"id":"echo:object/pose_child","parent":"echo:object/pose",
      "sprite":{"rotation":-30},"animations":{"idle":{"fps":8},"walk":{"cells":[[5,5],[0,1]]}}})";
    const std::string poseReset = R"({"schemaVersion":1,"id":"echo:object/pose_reset","parent":"echo:object/pose",
      "animations":{"idle":{"frames":2,"column":4}}})";
    std::vector<Source> poseObjects = objects;
    poseObjects.push_back({"echo:object/pose", poseBase});
    poseObjects.push_back({"echo:object/pose_child", poseChild});
    poseObjects.push_back({"echo:object/pose_reset", poseReset});
    auto scratch = std::make_unique<WorldModule>();
    check(static_cast<bool>(build(*scratch, poseObjects, rooms)), "pose fixture builds");
    const ObjectDefinition* pose = scratch->findObject("echo:object/pose");
    const AnimationSpec* poseIdle = pose != nullptr ? pose->findAnimation("idle") : nullptr;
    check(poseIdle != nullptr && poseIdle->frames == 3 && poseIdle->cellCount == 3 &&
              poseIdle->cellAt(1).column == 3 && poseIdle->cellAt(2).row == 2 &&
              poseIdle->cellAt(4).column == 3 && poseIdle->tint[3] == 0.5F &&
              poseIdle->rotation == 45.0F && poseIdle->spin == 90.0F &&
              pose->sprite().tint[1] == 0.5F && pose->sprite().rotation == 10.0F,
          "animations list non-adjacent cells and carry tint, rotation and spin");
    const AnimationSpec* consecutive = pose != nullptr ? pose->findAnimation("walk") : nullptr;
    check(consecutive != nullptr && consecutive->cellAt(1).column == 1 && consecutive->cellAt(1).row == 1,
          "consecutive animations still address adjacent cells");
    const ObjectDefinition* poseChildObject = scratch->findObject("echo:object/pose_child");
    const AnimationSpec* childIdle = poseChildObject != nullptr ? poseChildObject->findAnimation("idle") : nullptr;
    const AnimationSpec* childWalk = poseChildObject != nullptr ? poseChildObject->findAnimation("walk") : nullptr;
    check(childIdle != nullptr && childIdle->fps == 8 && childIdle->cellCount == 3 &&
              childIdle->spin == 90.0F && poseChildObject->sprite().rotation == -30.0F &&
              poseChildObject->sprite().tint[2] == 0.25F,
          "children inherit cells, tint, spin and sprite tint while overriding single fields");
    check(childWalk != nullptr && childWalk->cellCount == 2 && childWalk->frames == 2 &&
              childWalk->cellAt(0).column == 5,
          "a child can replace consecutive frames with explicit cells");
    const ObjectDefinition* poseResetObject = scratch->findObject("echo:object/pose_reset");
    const AnimationSpec* resetIdle = poseResetObject != nullptr ? poseResetObject->findAnimation("idle") : nullptr;
    check(resetIdle != nullptr && resetIdle->cellCount == 0 && resetIdle->frames == 2 &&
              resetIdle->cellAt(1).column == 5,
          "a child can replace explicit cells with consecutive frames");
    const auto rejectsPose = [&](const char* animation, std::string_view message) {
        const std::string json = R"({"schemaVersion":1,"id":"echo:object/bad","category":"prop",
          "sprite":{"sheet":"sprites/pose.png","frameWidth":16,"frameHeight":16},"bounds":[16,16],
          "animations":{"idle":)" + std::string(animation) + "}}";
        std::vector<Source> candidate = objects;
        candidate.push_back({"echo:object/bad", json});
        check(!build(*scratch, candidate, rooms), message);
    };
    rejectsPose(R"({"cells":[[0,0]],"frames":2,"fps":4})", "cells exclude frames");
    rejectsPose(R"({"cells":[[0,0]],"column":1,"fps":4})", "cells exclude column and row");
    rejectsPose(R"({"cells":[],"fps":4})", "empty cell lists are rejected");
    rejectsPose(R"({"cells":[[0]],"fps":4})", "malformed cells are rejected");
    rejectsPose(R"({"cells":[[256,0]],"fps":4})", "out-of-range cells are rejected");
    rejectsPose(R"({"frames":1,"fps":4,"tint":[1,2,1]})", "tint channels above 1 are rejected");
    rejectsPose(R"({"frames":1,"fps":4,"tint":[1,1]})", "tints need three or four channels");
    rejectsPose(R"({"frames":1,"fps":4,"rotation":720})", "out-of-range rotation is rejected");

    const RoomDefinition* alpha = world->findRoom("echo:room/a");
    check(alpha != nullptr && alpha->name() == "Alpha" && alpha->columns() == 20 && alpha->rows() == 3 &&
              alpha->background()[2] == 3 && alpha->tilesets().size() == 2,
          "rooms inherit size, background and tilesets and add their own sheets");
    const TileType* rotated = alpha != nullptr ? alpha->findTileType('=') : nullptr;
    check(rotated != nullptr && rotated->tilesetIndex == 1 && rotated->column == 3 && rotated->row == 1 &&
              rotated->rotation == 90 && rotated->flipX && !rotated->flipY,
          "tile legend stores tileset, sheet position, rotation and flip");
    check(alpha != nullptr && alpha->tilesets()[1].margin == 1 && alpha->tilesets()[1].spacing == 2 &&
              alpha->tilesets()[1].tileWidth == 16,
          "tileset settings are kept per sheet");
    check(alpha != nullptr && alpha->tile(1, 0, 2) == rotated && alpha->tile(0, 0, 0) == nullptr &&
              alpha->tile(1, 20, 0) == nullptr && alpha->tile(4, 0, 0) == nullptr,
          "layer lookup handles empty, out-of-range and missing layers");
    check(alpha != nullptr && alpha->layers()[0].scrollFactorX == 0.35F &&
              alpha->layers()[1].scrollFactorX == 1.0F,
          "parallax factors load from JSON while omitted factors preserve full camera tracking");
    check(alpha != nullptr && alpha->collision(5, 1) == TileCollision::Hazard &&
              alpha->collision(5, 2) == TileCollision::Solid && alpha->collision(5, 0) == TileCollision::None,
          "hazards win over solids across layers");
    check(alpha != nullptr && !alpha->leftExit().present && alpha->rightExit().present &&
              std::string_view(alpha->rightExit().requiresAbility.data()) == "echo:ability/step",
          "an empty exit closes the side and child exits merge by side");
    const ObjectPlacement* guard = alpha != nullptr ? alpha->findPlacement("guard") : nullptr;
    check(guard != nullptr && guard->hasPosition && guard->x == 32.0F && guard->hasFacing &&
              guard->properties.find("health") != nullptr && alpha->placements().size() == 2,
          "placements override parent placements by id");
    const RoomDefinition* beta = world->findRoom("echo:room/b");
    const RoomDefinition* topDown = world->findRoom("echo:room/c");
    check(beta != nullptr && beta->columns() == 24 && beta->rightExit().completesGame &&
              beta->layers().size() == 1 && beta->findTileType('#') != nullptr,
          "child layers replace parent layers and keep the inherited legend");
    check(alpha != nullptr && alpha->projection() == RoomProjection::Side &&
              beta != nullptr && beta->projection() == RoomProjection::Isometric &&
              topDown != nullptr && topDown->projection() == RoomProjection::TopDown,
          "rooms default to side view and select top-down or isometric projections");
    const WorldPoint sideScreen = projectWorldPoint(
        RoomProjection::Side, {350.0F, 180.0F}, {260.0F, 100.0F});
    const WorldPoint topDownScreen = projectWorldPoint(
        RoomProjection::TopDown, {500.0F, 150.0F}, {180.0F, 70.0F});
    const WorldPoint isoScreen = projectWorldPoint(
        RoomProjection::Isometric, {600.0F, 100.0F}, {100.0F, 270.0F});
    const WorldPoint isoDirection = projectWorldDirection(
        RoomProjection::Isometric, {1.0F, -1.0F});
    const WorldPoint movedIsoScreen = projectWorldPoint(
        RoomProjection::Isometric, {600.0F, 100.0F}, {120.0F, 280.0F});
    check(sideScreen.x == 90.0F && sideScreen.y == 80.0F &&
              topDownScreen.x == 320.0F && topDownScreen.y == 80.0F &&
              isoScreen.x == 400.0F && isoScreen.y == 80.0F &&
              isoDirection.x == 2.0F && isoDirection.y == 0.0F &&
              movedIsoScreen.x == isoScreen.x - 20.0F &&
              movedIsoScreen.y == isoScreen.y - 10.0F,
          "projection-aware positions and directions preserve camera-relative light offsets");
    const RoomLighting* lighting = alpha != nullptr ? &alpha->lighting() : nullptr;
    check(lighting != nullptr && lighting->ambientIntensity == 0.35F &&
              lighting->ambientColor[2] == 1.0F && lighting->directionalIntensity == 0.2F,
          "room ambient and directional lighting inherit as room-space data");
    check(lighting != nullptr && lighting->lightCount == 4 &&
              lighting->lights[0].type == WorldLightType::Point &&
              lighting->lights[0].persistence == LightPersistence::Persistent &&
              lighting->lights[0].flickerFrequency == 4.0F &&
              lighting->lights[0].fadeSeconds == 1.5F &&
              lighting->lights[0].hasResolvedPosition &&
              lighting->lights[0].resolvedX == 160.0F &&
              lighting->lights[0].resolvedY == 128.0F &&
              std::string_view(lighting->lights[0].circuit.data()) == "echo:power/main" &&
              lighting->lights[1].type == WorldLightType::Spot &&
              lighting->lights[1].coneAngle == 50.0F &&
              lighting->lights[1].followsAim &&
              std::string_view(lighting->lights[2].parent.data()) == "player" &&
              std::string_view(lighting->lights[3].parent.data()) == "enemy.guard",
          "point, spot and player/enemy-attached lights retain bounded data-driven properties");
    const ObjectDefinition* advancedObject = world->findObject("echo:object/advanced");
    const WorldMaterialSpec* objectMaterial =
        advancedObject != nullptr ? &advancedObject->sprite().material : nullptr;
    const TilesetSpec* inheritedTileset = beta != nullptr && !beta->tilesets().empty()
        ? &beta->tilesets()[0] : nullptr;
    check(objectMaterial != nullptr && objectMaterial->configured &&
              objectMaterial->normalSheet[0] == '\0' &&
              std::string_view(objectMaterial->emissiveSheet.data()) ==
                  "sprites/advanced_emissive.png" &&
              std::string_view(objectMaterial->emissiveCircuit.data()) == "echo:power/main" &&
              objectMaterial->emissiveIntensity == 0.75F &&
              inheritedTileset != nullptr && inheritedTileset->hasMaterial &&
              std::string_view(inheritedTileset->material.normalSheet.data()) ==
                  "sprites/tiles/station_normal.png",
          "object material overrides and inherited tileset maps remain data-driven");
    JsonDocument globalLightingDocument{};
    WorldLighting globalLighting{};
    Status globalLightingStatus = parseInto(
        R"({"ambient":{"color":[1.0,0.8,0.6],"intensity":0.8,
             "dayNight":{"nightColor":[0.0,0.1,0.2],"nightIntensity":0.2,
                         "periodSeconds":240}},
            "directional":{"direction":[-0.4,0.8],"color":[0.7,0.8,1.0],"intensity":0.1}})",
        globalLightingDocument);
    if (globalLightingStatus) {
        globalLightingStatus = WorldModule::parseWorldLighting(
            globalLightingDocument.root(), globalLighting);
    }
    RoomLighting noRoomOverride{};
    std::array<float, 3> sampledColor{};
    float sampledIntensity{};
    const Status daySample = sampleAmbientLighting(
        globalLighting, noRoomOverride, 0.0, sampledColor, sampledIntensity);
    const bool dayValues = daySample && sampledColor[0] == 1.0F &&
                           std::abs(sampledIntensity - 0.8F) < 0.001F;
    const Status transitionSample = sampleAmbientLighting(
        globalLighting, noRoomOverride, 60.0, sampledColor, sampledIntensity);
    const bool smoothMidpoint = transitionSample &&
        std::abs(sampledColor[0] - 0.5F) < 0.001F &&
        std::abs(sampledIntensity - 0.5F) < 0.001F;
    const Status nightSample = sampleAmbientLighting(
        globalLighting, noRoomOverride, 120.0, sampledColor, sampledIntensity);
    const bool nightValues = nightSample && sampledColor[0] == 0.0F &&
                             std::abs(sampledIntensity - 0.2F) < 0.001F;
    RoomLighting roomOverride{};
    roomOverride.hasAmbient = true;
    roomOverride.ambientColor = {0.4F, 0.5F, 0.6F};
    roomOverride.ambientIntensity = 0.3F;
    const Status roomSample = sampleAmbientLighting(
        globalLighting, roomOverride, 120.0, sampledColor, sampledIntensity);
    check(globalLightingStatus && dayValues && smoothMidpoint && nightValues &&
              roomSample && std::abs(sampledIntensity - 0.3F) < 0.001F &&
              std::abs(sampledColor[0] - 0.4F) < 0.001F,
          "world ambient cycles smoothly while a room can override it independently");
    LightFadeTransition fade{};
    float fadedIntensity{};
    const bool initialFade = fade.update(1.0F, 0.0F, 2.0F, fadedIntensity) &&
                             fadedIntensity == 1.0F;
    const bool fadeMidpoint = fade.update(0.0F, 1.0F, 2.0F, fadedIntensity) &&
                              std::abs(fadedIntensity - 0.5F) < 0.001F;
    const bool fadeReversal = fade.update(1.0F, 0.0F, 2.0F, fadedIntensity) &&
                              std::abs(fadedIntensity - 0.5F) < 0.001F &&
                              fade.update(1.0F, 1.0F, 2.0F, fadedIntensity) &&
                              std::abs(fadedIntensity - 0.75F) < 0.001F;
    const bool fadeComplete = fade.update(1.0F, 2.0F, 2.0F, fadedIntensity) &&
                              fadedIntensity == 1.0F;
    const bool immediateOff = fade.update(0.0F, 0.0F, 0.0F, fadedIntensity) &&
                              fadedIntensity == 0.0F;
    check(initialFade && fadeMidpoint && fadeReversal && fadeComplete && immediateOff &&
              !fade.update(0.0F, 0.0F, 31.0F, fadedIntensity),
          "light fades smoothly, reverses without a jump, and supports immediate switching");
    check(world->startRoom() == "echo:room/a" && world->objects().size() == 3 && world->rooms().size() == 4,
          "finalized definitions become visible");

    std::array<char, 64> path{};
    check(WorldModule::contentPath("echo:object/enemy_patrol", path.data(), path.size()) &&
              std::string_view(path.data()) == "world/objects/enemy_patrol.json",
          "object IDs map to packaged files");
    check(WorldModule::contentPath("echo:room/landing_bay", path.data(), path.size()) &&
              std::string_view(path.data()) == "world/rooms/landing_bay.json",
          "room IDs map to packaged files");
    check(!WorldModule::contentPath("echo:object/Up.Case", path.data(), path.size()) &&
              !WorldModule::contentPath("echo:item/x", path.data(), path.size()),
          "unsafe names and other content types have no world path");
    check(WorldModule::validSpritePath("sprites/tiles/station.png") &&
              !WorldModule::validSpritePath("sprites/../save.png") &&
              !WorldModule::validSpritePath("C:/sprites/x.png") &&
              !WorldModule::validSpritePath("sprites//x.png") &&
              !WorldModule::validSpritePath("sprites/x.bmp"),
          "sprite paths stay inside packaged sprites");

    const auto rejects = [&](std::vector<Source> objectSet, std::vector<Source> roomSet, std::string_view message) {
        auto other = std::make_unique<WorldModule>();
        check(!build(*other, objectSet, roomSet), message);
        check(other->objects().empty() && other->findRoom("echo:room/a") == nullptr,
              "failed worlds expose no definitions");
    };
    const std::string minimalRoom = room("echo:room/a", R"(,"name":"A","size":{"columns":20,"rows":1},
        "tilesets":[{"id":"t","sheet":"sprites/t.png","tileWidth":32,"tileHeight":32}],"tiles":{"#":{"tileset":"t","x":0,"y":0}})" +
        layers("main", {row('#')}));
    const std::vector<Source> minimalRooms{{"echo:room/a", minimalRoom}};
    const std::string concrete = R"({"schemaVersion":1,"id":"echo:object/a","category":"prop","sprite":{"sheet":"sprites/a.png","frameWidth":8,"frameHeight":8},"bounds":[8,8],"animations":{"idle":{"frames":1,"fps":1}}})";

    rejects({{"echo:object/a", R"({"schemaVersion":1,"id":"echo:object/a","parent":"echo:object/b"})"},
             {"echo:object/b", R"({"schemaVersion":1,"id":"echo:object/b","parent":"echo:object/a"})"}},
            minimalRooms, "inheritance cycles are rejected");
    rejects({{"echo:object/a", R"({"schemaVersion":1,"id":"echo:object/a","parent":"echo:object/missing"})"}},
            minimalRooms, "missing parents are rejected");
    std::vector<Source> chain{{"echo:object/l0", concrete}};
    chain[0].json.replace(chain[0].json.find("echo:object/a"), 13, "echo:object/l0");
    for (int level = 1; level <= 9; ++level) {
        chain.push_back({"echo:object/l" + std::to_string(level),
                         R"({"schemaVersion":1,"id":"echo:object/l)" + std::to_string(level) +
                             R"(","parent":"echo:object/l)" + std::to_string(level - 1) + "\"}"});
    }
    rejects(chain, minimalRooms, "inheritance deeper than eight levels is rejected");
    chain.pop_back();
    auto eight = std::make_unique<WorldModule>();
    check(static_cast<bool>(build(*eight, chain, minimalRooms)), "eight inheritance levels are accepted");
    rejects({{"echo:object/a", R"({"schemaVersion":1,"id":"echo:object/a","category":"prop"})"}},
            minimalRooms, "incomplete concrete objects are rejected");
    rejects({{"echo:object/a", R"({"schemaVersion":1,"id":"echo:object/a","category":"prop","sprite":{"sheet":"../a.png","frameWidth":8,"frameHeight":8},"bounds":[8,8],"animations":{"idle":{"frames":1,"fps":1}}})"}},
            minimalRooms, "sprite path traversal is rejected");
    rejects({{"echo:object/a", R"({"schemaVersion":1,"id":"echo:object/a","category":"prop","sprite":{"sheet":"sprites/a.png","frameWidth":8,"frameHeight":8},"bounds":[8,8],"animations":{"idle":{"frames":1,"fps":1}},"script":"x"})"}},
            minimalRooms, "unknown object fields are rejected");
    rejects({{"echo:object/a", R"({"schemaVersion":1,"id":"echo:object/a","category":"prop","sprite":{"sheet":"sprites/a.png","frameWidth":8,"frameHeight":8},"bounds":[8,8],"animations":{"idle":{"frames":1,"fps":1}},"defaultAnimation":"run"})"}},
            minimalRooms, "undefined default animations are rejected");
    rejects({{"echo:object/a", R"({"schemaVersion":1,"id":"echo:object/a","category":"prop","sprite":{"sheet":"sprites/a.png","frameWidth":8,"frameHeight":8,"material":{"normalSheet":"sprites/../normal.png"}},"bounds":[8,8],"animations":{"idle":{"frames":1,"fps":1}}})"}},
            minimalRooms, "material maps reject paths outside the sprites directory");
    rejects({{"echo:object/a", R"({"schemaVersion":1,"id":"echo:object/a","category":"prop","sprite":{"sheet":"sprites/a.png","frameWidth":8,"frameHeight":8,"material":{"emissiveSheet":"sprites/a_emissive.png","emissiveIntensity":9}},"bounds":[8,8],"animations":{"idle":{"frames":1,"fps":1}}})"}},
            minimalRooms, "emissive material intensity is bounded");
    rejects({{"echo:object/b", concrete}}, minimalRooms, "file IDs must match manifest entries");
    const std::vector<Source> concreteObjects{{"echo:object/a", concrete}};
    const auto rejectsRoom = [&](const std::string& extra, std::string_view message) {
        rejects(concreteObjects, {{"echo:room/a", room("echo:room/a", extra)}}, message);
    };
    const std::string tileset = R"(,"name":"A","size":{"columns":20,"rows":1},"tilesets":[{"id":"t","sheet":"sprites/t.png","tileWidth":32,"tileHeight":32}])";
    rejectsRoom(tileset + R"(,"tiles":{"#":{"tileset":"t","x":0,"y":0,"rotation":45}})" + layers("main", {row('#')}),
                "non-right-angle rotations are rejected");
    rejectsRoom(tileset + R"(,"tiles":{"#":{"tileset":"missing","x":0,"y":0}})" + layers("main", {row('#')}),
                "tiles referencing unknown tilesets are rejected");
    rejectsRoom(tileset + R"(,"tiles":{"#":{"tileset":"t","x":0,"y":0}})" + layers("main", {row('#', 21)}),
                "rows longer than the room are rejected");
    rejectsRoom(tileset + R"(,"tiles":{"#":{"tileset":"t","x":0,"y":0}})" + layers("main", {row('#'), row('#')}),
                "extra rows are rejected");
    rejectsRoom(tileset + R"(,"tiles":{"#":{"tileset":"t","x":0,"y":0}})" + layers("main", {row('X')}),
                "undefined map symbols are rejected");
    rejectsRoom(tileset + R"(,"layers":[{"name":"back","scrollFactorX":1.1,"rows":[")" + row('.') +
                    R"("]}],"tiles":{"#":{"tileset":"t","x":0,"y":0}})",
                "parallax factors above one are rejected");
    rejectsRoom(tileset + R"(,"layers":[{"name":"back","scrollFactorX":-0.1,"rows":[")" + row('.') +
                    R"("]}],"tiles":{"#":{"tileset":"t","x":0,"y":0}})",
                "negative parallax factors are rejected");
    rejectsRoom(tileset + R"(,"projection":"isometric","layers":[{"name":"main","scrollFactorX":0.5,"rows":[")" +
                    row('#') + R"("]}],"tiles":{"#":{"tileset":"t","x":0,"y":0}})",
                "isometric rooms reject side-view parallax factors");
    rejectsRoom(tileset + R"(,"projection":"top-down","layers":[{"name":"main","scrollFactorX":0.5,"rows":[")" +
                    row('#') + R"("]}],"tiles":{"#":{"tileset":"t","x":0,"y":0}})",
                "top-down rooms reject side-view parallax factors");
    rejectsRoom(tileset + R"(,"tiles":{"#":{"tileset":"t","x":0,"y":0}},"exits":{"right":{"room":"echo:room/none"}})" +
                    layers("main", {row('#')}),
                "exits to unknown rooms are rejected");
    rejectsRoom(tileset + R"(,"tiles":{"#":{"tileset":"t","x":0,"y":0}},"objects":[{"id":"x","object":"echo:object/none"}])" +
                    layers("main", {row('#')}),
                "placements of unknown objects are rejected");
    rejectsRoom(tileset + R"(,"tiles":{"#":{"tileset":"t","x":0,"y":0}},"objects":[{"id":"x","object":"echo:object/a","position":[900,10]}])" +
                    layers("main", {row('#')}),
                "placements outside the room are rejected");
    rejectsRoom(tileset + R"(,"projection":"oblique","tiles":{"#":{"tileset":"t","x":0,"y":0}})" +
                    layers("main", {row('#')}),
                "unknown room projections are rejected");
    rejectsRoom(tileset + R"(,"lighting":{"ambient":{"color":[1,1,1],"intensity":9}},"tiles":{"#":{"tileset":"t","x":0,"y":0}})" +
                    layers("main", {row('#')}),
                "lighting intensities outside the supported bound are rejected");
    JsonDocument invalidCycleDocument{};
    WorldLighting invalidCycle{};
    Status invalidCycleStatus = parseInto(
        R"({"ambient":{"color":[1,1,1],"intensity":1,
             "dayNight":{"nightColor":[0,0,0],"nightIntensity":0.2,"periodSeconds":9}}})",
        invalidCycleDocument);
    if (invalidCycleStatus) {
        invalidCycleStatus = WorldModule::parseWorldLighting(
            invalidCycleDocument.root(), invalidCycle);
    }
    check(!invalidCycleStatus, "day/night periods below the supported bound are rejected");
    rejectsRoom(tileset + R"(,"lighting":{"lights":[{"id":"echo:light/spot","type":"spot","position":[10,10],"radius":80,"color":[1,1,1],"intensity":1}]},
        "tiles":{"#":{"tileset":"t","x":0,"y":0}})" + layers("main", {row('#')}),
                "spotlights without a direction and cone are rejected");
    rejectsRoom(tileset + R"(,"lighting":{"lights":[{"id":"echo:light/aim","type":"spot","position":[10,10],"radius":80,"color":[1,1,1],"intensity":1,"followsAim":true,"coneAngle":60}]},
        "tiles":{"#":{"tileset":"t","x":0,"y":0}})" + layers("main", {row('#')}),
                "aim-following spotlights require a player parent");
    rejectsRoom(tileset + R"(,"lighting":{"lights":[{"id":"echo:light/fade","type":"point","position":[10,10],"radius":80,"color":[1,1,1],"intensity":1,"fadeSeconds":31}]},
        "tiles":{"#":{"tileset":"t","x":0,"y":0}})" + layers("main", {row('#')}),
                "light fade durations above the bound are rejected");
    rejectsRoom(tileset + R"(,"lighting":{"lights":[{"id":"echo:light/attached","type":"point","position":[10,10],"radius":80,"color":[1,1,1],"intensity":1,"parent":"enemy.missing"}]},
        "tiles":{"#":{"tileset":"t","x":0,"y":0}})" + layers("main", {row('#')}),
                "entity-attached lights reject unknown placements");

    std::string manySheets = R"(,"name":"A","size":{"columns":20,"rows":1},"tilesets":[)";
    for (int index = 0; index < 17; ++index) {
        manySheets += (index == 0 ? "" : ",") + std::string(R"({"id":"t)") + std::to_string(index) +
                      R"(","sheet":"sprites/t.png","tileWidth":32,"tileHeight":32})";
    }
    rejectsRoom(manySheets + R"(],"tiles":{"#":{"tileset":"t0","x":0,"y":0}})" + layers("main", {row('#')}),
                "more than sixteen tilesets per room are rejected");
    manySheets.replace(manySheets.rfind(",{"), std::string::npos, "");
    auto sixteen = std::make_unique<WorldModule>();
    check(static_cast<bool>(build(*sixteen, concreteObjects,
                                  {{"echo:room/a", room("echo:room/a", manySheets + R"(],"tiles":{"#":{"tileset":"t15","x":0,"y":0}})" +
                                                                           layers("main", {row('#')}))}})),
          "sixteen tilesets per room are accepted");

    auto missing = std::make_unique<WorldModule>();
    JsonDocument manifest{};
    check(static_cast<bool>(parseInto(R"({"schemaVersion":1,"startRoom":"echo:room/a","objects":["echo:object/a"],"rooms":["echo:room/a"]})", manifest)) &&
              static_cast<bool>(missing->loadManifest(manifest.root())) && !missing->finalize(),
          "finalize requires every listed document");
    check(!missing->loadManifest(manifest.root()), "the manifest cannot be loaded twice");
    ModuleContext context{};
    check(!missing->initialize(context), "an unfinished world cannot initialize");
    check(static_cast<bool>(world->initialize(context)) && world->initialized(), "a finalized world initializes");
    world->reset();
    check(world->finalized(), "reset is ignored while initialized");
    world->shutdown();

    if (failures != 0) return EXIT_FAILURE;
    std::cout << "echo framework world tests passed\n";
    return EXIT_SUCCESS;
}
