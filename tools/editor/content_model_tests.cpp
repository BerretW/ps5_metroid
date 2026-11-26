#include "content_model.hpp"
#include "map_model.hpp"
#include "windows_storage.hpp"

#include "game/game.hpp"
#include "yyjson.h"

#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <string>

namespace {
int failures{};

void check(bool value, const char* text) {
    if (!value) {
        std::cerr << "FAIL: " << text << '\n';
        ++failures;
    }
}

std::string read(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        std::cerr << "Cannot open " << path << '\n';
        ++failures;
        return {};
    }
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

std::size_t find(const echo::editor::ContentModel& model, std::string_view label) {
    for (std::size_t i = 0; i < model.fields().size(); ++i)
        if (model.fields()[i].label == label) return i;
    return model.fields().size();
}

void testGame(const std::filesystem::path& assets) {
    using namespace echo::framework;
    const std::string source = read(assets / "framework" / "game.json");
    echo::editor::ContentModel model;
    std::string error;
    if (!model.open(source, error)) {
        std::cerr << error << '\n';
        ++failures;
        return;
    }
    const auto health = find(model, "echo:attribute/player.max_health");
    check(health < model.fields().size(), "player health is editable");
    check(!model.edit(health, "9", error) && model.source() == source,
          "out of range edits do not change document");
    check(model.edit(health, "4", error), "valid player health edit succeeds");
    JsonDocument document;
    JsonError jsonError{};
    check(static_cast<bool>(JsonDocument::parse(model.source().data(), model.source().size(),
                                                document, jsonError)), "edited game parses");
    AttributesModule attributes;
    ModuleContext context{};
    check(static_cast<bool>(attributes.load(document.root().find("attributes"))) &&
              attributes.initialize(context), "game attribute module accepts edit");
    echo::Game game;
    check(static_cast<bool>(game.configureAttributes(attributes)) && game.maximumHealth() == 4,
          "game uses edited maximum health");
    attributes.shutdown();

    using Doc = std::unique_ptr<yyjson_doc, decltype(&yyjson_doc_free)>;
    Doc before(yyjson_read(source.data(), source.size(), 0), yyjson_doc_free);
    Doc after(yyjson_read(model.source().data(), model.source().size(), 0), yyjson_doc_free);
    check(before && after && yyjson_equals(yyjson_obj_get(yyjson_doc_get_root(before.get()), "loot"),
                                           yyjson_obj_get(yyjson_doc_get_root(after.get()), "loot")),
          "unrelated game content survives serialization");

    const auto stalkerScript = find(model, "echo:script/stalker_ai / enemy_ai");
    check(stalkerScript < model.fields().size(), "stalker Lua binding is editable");
    const std::string beforeScript = model.source();
    check(!model.edit(stalkerScript, "", error) && model.source() == beforeScript,
          "empty script source is rejected");
    check(model.edit(stalkerScript,
                     "return function(input)\n return {move_x=0,fire=false,visible=true}\nend\n",
                     error),
          "bounded Lua source can be authored");

    const auto objective = find(model, "Quest echo:quest/main_route / key");
    const std::string previous = model.source();
    check(!model.edit(objective, "BAD!", error) && model.source() == previous,
          "unsupported font glyph is rejected without changing document");
    check(model.edit(objective, "FIND THE ENGINEER", error), "objective edit succeeds");
    document.reset();
    check(static_cast<bool>(JsonDocument::parse(model.source().data(), model.source().size(),
                                                document, jsonError)), "edited quest parses");
    QuestModule quests;
    check(static_cast<bool>(quests.load(document.root().find("quests"))),
          "game quest module accepts edit");
    const QuestDefinition* quest = quests.find("echo:quest/main_route");
    check(quest && evaluateQuest(*quest, {}).objective == "FIND THE ENGINEER",
          "game quest progress exposes edited UI text");
}

void testStalker(const std::filesystem::path& assets) {
    using namespace echo::framework;
    const std::string source = read(assets / "world" / "objects" / "enemy_stalker.json");
    echo::editor::ContentModel model;
    std::string error;
    if (!model.open(source, error)) {
        std::cerr << error << '\n';
        ++failures;
        return;
    }
    const auto speed = find(model, "Stalker / speed");
    check(!model.edit(speed, "401", error) && model.source() == source,
          "invalid stalker speed is rejected");
    check(model.edit(speed, "100", error), "stalker speed can be edited");

    auto world = std::make_unique<WorldModule>();
    auto load = [&](const std::string& data, auto apply) {
        JsonDocument doc;
        JsonError jsonError{};
        Status status = JsonDocument::parse(data.data(), data.size(), doc, jsonError);
        if (status) status = apply(doc.root());
        if (!status) std::cerr << status.subject << ": " << status.detail << '\n';
        return static_cast<bool>(status);
    };
    bool ok = load(read(assets / "world" / "world.json"),
                   [&](JsonValue root) { return world->loadManifest(root); });
    std::array<char, 160> path{};
    for (std::size_t i = 0; ok && i < world->manifestObjectCount(); ++i) {
        const auto id = world->manifestObject(i);
        ok = WorldModule::contentPath(id, path.data(), path.size()) &&
             load(id == "echo:object/enemy_stalker" ? model.source() : read(assets / path.data()),
                  [&](JsonValue root) { return world->addObject(id, root); });
    }
    for (std::size_t i = 0; ok && i < world->manifestRoomCount(); ++i) {
        const auto id = world->manifestRoom(i);
        ok = WorldModule::contentPath(id, path.data(), path.size()) &&
             load(read(assets / path.data()),
                  [&](JsonValue root) { return world->addRoom(id, root); });
    }
    if (ok) ok = static_cast<bool>(world->finalize());
    auto layout = std::make_unique<echo::WorldLayout>();
    if (ok) ok = static_cast<bool>(echo::buildWorldLayout(*world, *layout));
    check(ok && !layout->roamerList().empty() && layout->roamerList()[0].speed == 100.0F,
          "real world layout uses edited stalker speed");
}

void testStorage(const std::filesystem::path& assets) {
    std::array<wchar_t, MAX_PATH> directory{}, filename{};
    const DWORD length = GetTempPathW(static_cast<DWORD>(directory.size()), directory.data());
    if (length == 0 || length >= directory.size() ||
        !GetTempFileNameW(directory.data(), L"eco", 0, filename.data())) {
        check(false, "temporary storage fixture created");
        return;
    }

    const std::string source = read(assets / "framework" / "game.json");
    {
        std::ofstream file(std::filesystem::path(filename.data()), std::ios::binary | std::ios::trunc);
        file.write(source.data(), static_cast<std::streamsize>(source.size()));
        check(static_cast<bool>(file), "temporary fixture written");
    }
    std::string original;
    std::wstring path, problem;
    DWORD code{};
    const bool opened = echo::editor::readContent(filename.data(), original, &path, code);
    check(opened && original == source, "storage opens and resolves selected file");
    echo::editor::ContentModel model;
    std::string error;
    const bool edited = model.open(original, error) &&
                        model.edit(find(model, "echo:attribute/player.max_health"), "4", error);
    check(edited, "storage fixture edited");
    if (opened && edited) {
        check(echo::editor::replaceContent(path, original, model.source(), problem),
              "storage atomically replaces selected file");
        std::string saved;
        check(echo::editor::readContent(path, saved, nullptr, code) && saved == model.source(),
              "saved content persists");
        check(!echo::editor::replaceContent(path, original, source, problem),
              "storage refuses externally changed content");
        std::string after;
        check(echo::editor::readContent(path, after, nullptr, code) && after == saved,
              "conflict preserves newer content");
    }
    check(DeleteFileW(filename.data()) != FALSE, "temporary storage fixture removed");
}

void testMap(const std::filesystem::path& assets) {
    const auto path = assets / "world" / "rooms" / "landing_bay.json";
    std::string source;
    std::wstring resolved;
    DWORD code{};
    if (!echo::editor::readContent(path.wstring(), source, &resolved, code)) {
        check(false, "real room fixture opened");
        return;
    }
    echo::editor::MapModel map;
    std::string error;
    if (!map.open(resolved, source, error)) {
        std::cerr << "map: " << error << '\n';
        ++failures;
        return;
    }
    std::size_t main = map.layers().size();
    for (std::size_t i = 0; i < map.layers().size(); ++i)
        if (map.layers()[i].name == "main") main = i;
    check(main < map.layers().size(), "room exposes main tile layer");
    check(!map.paint(main, 1, 1, '?', error), "unknown brush rejected");
    check(map.paint(main, 1, 1, '#', error), "tile can be painted");
    check(map.undo() && map.layers()[main].rows[1][1] == ' ',
          "undo restores the previous tile");
    check(map.paint(main, 1, 1, '#', error), "tile can be painted again");
    std::string candidate;
    check(map.serialize(candidate, error) && map.validate(candidate, error),
          "painted room validates against the complete game world");
    echo::framework::JsonDocument document;
    echo::framework::JsonError jsonError{};
    check(static_cast<bool>(echo::framework::JsonDocument::parse(
              candidate.data(), candidate.size(), document, jsonError)), "painted room parses");
    std::string_view row;
    check(document.root().find("layers").at(main).find("rows").at(1).string(row) &&
              row.size() > 1 && row[1] == '#', "painted tile is saved in actual room layer");
    using Doc = std::unique_ptr<yyjson_doc, decltype(&yyjson_doc_free)>;
    Doc before(yyjson_read(source.data(), source.size(), 0), yyjson_doc_free);
    Doc after(yyjson_read(candidate.data(), candidate.size(), 0), yyjson_doc_free);
    check(before && after && yyjson_equals(yyjson_obj_get(yyjson_doc_get_root(before.get()), "objects"),
                                           yyjson_obj_get(yyjson_doc_get_root(after.get()), "objects")),
          "painting preserves room objects");
    auto world = std::make_unique<echo::framework::WorldModule>();
    auto load = [&](const std::string& data, auto apply) {
        echo::framework::JsonDocument doc;
        echo::framework::JsonError err{};
        auto status = echo::framework::JsonDocument::parse(data.data(), data.size(), doc, err);
        if (status) status = apply(doc.root());
        return static_cast<bool>(status);
    };
    bool ok = load(read(assets / "world" / "world.json"),
                   [&](auto root) { return world->loadManifest(root); });
    std::array<char, 160> contentPath{};
    for (std::size_t i = 0; ok && i < world->manifestObjectCount(); ++i) {
        const auto id = world->manifestObject(i);
        ok = echo::framework::WorldModule::contentPath(id, contentPath.data(), contentPath.size()) &&
             load(read(assets / contentPath.data()),
                  [&](auto root) { return world->addObject(id, root); });
    }
    for (std::size_t i = 0; ok && i < world->manifestRoomCount(); ++i) {
        const auto id = world->manifestRoom(i);
        ok = echo::framework::WorldModule::contentPath(id, contentPath.data(), contentPath.size()) &&
             load(id == "echo:room/landing_bay" ? candidate : read(assets / contentPath.data()),
                  [&](auto root) { return world->addRoom(id, root); });
    }
    if (ok) ok = static_cast<bool>(world->finalize());
    auto layout = std::make_unique<echo::WorldLayout>();
    if (ok) ok = static_cast<bool>(echo::buildWorldLayout(*world, *layout));
    check(ok && layout->rooms[0].tiles[echo::kMaxLayoutColumns + 1] == echo::Tile::Solid,
          "game world collision uses the painted tile");
}
} // namespace

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    const std::filesystem::path assets(argv[1]);
    testGame(assets);
    testStalker(assets);
    testStorage(assets);
    testMap(assets);
    return failures == 0 ? 0 : 1;
}
