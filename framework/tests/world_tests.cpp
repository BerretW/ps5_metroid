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
  "sprite":{"sheet":"sprites/base.png","frameWidth":32,"frameHeight":32,"offset":[-2,-4]},
  "bounds":[28,28],"spawn":{"position":[100,200],"facing":"left"},
  "animations":{"idle":{"row":0,"column":0,"frames":2,"fps":4},"walk":{"row":1,"frames":4,"fps":8}},
  "defaultAnimation":"idle","properties":{"health":2,"speed":45,"behavior":"patrol"}})";
const std::string kAdvanced = R"({"schemaVersion":1,"id":"echo:object/advanced","parent":"echo:object/base",
  "sprite":{"sheet":"sprites/advanced.png"},"animations":{"walk":{"frames":6},"attack":{"row":2,"frames":3,"fps":12,"loop":false}},
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
    "tilesets":[{"id":"station","sheet":"sprites/tiles/station.png","tileWidth":32,"tileHeight":32}],
    "tiles":{"#":{"tileset":"station","x":0,"y":0,"collision":"solid"},
             "^":{"tileset":"station","x":1,"y":0,"collision":"hazard"},
             "~":{"tileset":"station","x":2,"y":0}},
    "exits":{"left":{}},"objects":[{"id":"guard","object":"echo:object/advanced","position":[64,64]}])");
}

int main() {
    auto world = std::make_unique<WorldModule>();

    const std::string roomA = room("echo:room/a",
        R"(,"name":"Alpha","tilesets":[{"id":"extra","sheet":"sprites/tiles/extra.png","tileWidth":16,"tileHeight":16,"margin":1,"spacing":2}],
        "tiles":{"=":{"tileset":"extra","x":3,"y":1,"rotation":90,"flipX":true,"collision":"solid"},"~":{"tileset":"station","x":2,"y":0,"collision":"hazard"}})" +
            std::string(R"(,"layers":[{"name":"back","rows":[")") + row('.') + "\",\"" + row('~') + "\",\"" + row('.') +
            R"("]},{"name":"main","rows":[")" + row('.') + "\",\"" + row('#') + "\",\"=" + row('#', 19) + R"("]}])" +
            R"(,"exits":{"right":{"room":"echo:room/b","requiresAbility":"echo:ability/step"}},
            "objects":[{"id":"guard","object":"echo:object/advanced","position":[32,32],"facing":"right","properties":{"health":9}},
                       {"id":"plain","object":"echo:object/plain"}])",
        "echo:room/template");
    const std::string roomB = room("echo:room/b",
        R"(,"name":"Beta","projection":"isometric","size":{"columns":24,"rows":2},"exits":{"right":{"completesGame":true}})" +
            layers("main", {row('.', 24), row('#', 24)}),
        "echo:room/template");
    const std::string plain = R"({"schemaVersion":1,"id":"echo:object/plain","category":"prop",
      "sprite":{"sheet":"sprites/plain.png","frameWidth":16,"frameHeight":16},"bounds":[16,16],
      "animations":{"idle":{"frames":1,"fps":1}}})";

    const std::vector<Source> objects{{"echo:object/advanced", kAdvanced},
                                      {"echo:object/base", kBase},
                                      {"echo:object/plain", plain}};
    const std::vector<Source> rooms{{"echo:room/a", roomA}, {"echo:room/template", kTemplate},
                                    {"echo:room/b", roomB}};
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
    check(beta != nullptr && beta->columns() == 24 && beta->rightExit().completesGame &&
              beta->layers().size() == 1 && beta->findTileType('#') != nullptr,
          "child layers replace parent layers and keep the inherited legend");
    check(alpha != nullptr && alpha->projection() == RoomProjection::Side &&
              beta != nullptr && beta->projection() == RoomProjection::Isometric,
          "rooms default to side view and may select the isometric projection");
    check(world->startRoom() == "echo:room/a" && world->objects().size() == 3 && world->rooms().size() == 3,
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
