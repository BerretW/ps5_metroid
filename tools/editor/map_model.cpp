#include "map_model.hpp"
#include "windows_storage.hpp"
#include "game/game.hpp"
#include "yyjson.h"

#include <algorithm>
#include <array>
#include <cstdlib>

namespace echo::editor {
namespace {
using namespace echo::framework;

bool parse(const std::string& data, JsonDocument& document, std::string& error) {
    JsonError jsonError{};
    const Status status = JsonDocument::parse(data.data(), data.size(), document, jsonError);
    if (!status) {
        error = std::string(status.subject) + ": " + std::string(status.detail);
        return false;
    }
    return true;
}

bool applyStatus(Status status, std::string& error) {
    if (status) return true;
    error = std::string(status.subject) + ": " + std::string(status.detail);
    return false;
}
} // namespace

bool MapModel::loadWorld(const std::string& candidate, std::unique_ptr<WorldModule>& result,
                         std::string& error) const {
    auto world = std::make_unique<WorldModule>();
    auto loadFile = [&](const std::filesystem::path& path, auto apply) {
        std::string data;
        DWORD code{};
        if (!readContent(path.wstring(), data, nullptr, code)) {
            error = "Cannot read world content (Windows error " + std::to_string(code) + ").";
            return false;
        }
        if (data.size() > kMaxWorldJsonBytes) {
            error = "World content exceeds the 32 KiB runtime limit.";
            return false;
        }
        JsonDocument document;
        return parse(data, document, error) && applyStatus(apply(document.root()), error);
    };
    if (!loadFile(assets_ / L"world" / L"world.json",
                  [&](JsonValue value) { return world->loadManifest(value); })) return false;
    std::array<char, 160> path{};
    const auto contentPath = [&]() {
        std::string relative(path.data());
        std::replace(relative.begin(), relative.end(), '/', '\\');
        return assets_ / relative;
    };
    bool found{};
    for (std::size_t i = 0; i < world->manifestObjectCount(); ++i) {
        const auto id = world->manifestObject(i);
        if (!WorldModule::contentPath(id, path.data(), path.size())) {
            error = "Invalid object path in world manifest.";
            return false;
        }
        if (!loadFile(contentPath(), [&](JsonValue root) { return world->addObject(id, root); }))
            return false;
    }
    for (std::size_t i = 0; i < world->manifestRoomCount(); ++i) {
        const auto id = world->manifestRoom(i);
        if (!WorldModule::contentPath(id, path.data(), path.size())) {
            error = "Invalid room path in world manifest.";
            return false;
        }
        if (id == id_) {
            std::string ignored;
            std::wstring finalPath;
            DWORD code{};
            if (!readContent(contentPath().wstring(), ignored, &finalPath, code) ||
                _wcsicmp(finalPath.c_str(), filename_.c_str()) != 0) {
                error = "Selected room does not match the world manifest.";
                return false;
            }
            JsonDocument doc;
            if (candidate.size() > kMaxWorldJsonBytes) {
                error = "Room exceeds the 32 KiB runtime limit.";
                return false;
            }
            if (!parse(candidate, doc, error) ||
                !applyStatus(world->addRoom(id, doc.root()), error)) return false;
            found = true;
        } else if (!loadFile(contentPath(), [&](JsonValue root) { return world->addRoom(id, root); })) {
            return false;
        }
    }
    if (!found) { error = "Room is not listed in the world manifest."; return false; }
    if (!applyStatus(world->finalize(), error)) return false;
    auto layout = std::make_unique<echo::WorldLayout>();
    if (!applyStatus(echo::buildWorldLayout(*world, *layout), error)) return false;
    result = std::move(world);
    return true;
}

bool MapModel::open(const std::wstring& filename, const std::string& source, std::string& error) {
    JsonDocument document;
    if (!parse(source, document, error)) return false;
    std::string_view id;
    if (!document.root().find("id").string(id)) { error = "Room ID is missing."; return false; }
    std::filesystem::path selected(filename);
    if (_wcsicmp(selected.parent_path().filename().c_str(), L"rooms") != 0 ||
        _wcsicmp(selected.parent_path().parent_path().filename().c_str(), L"world") != 0) {
        error = "Open a room inside the game's assets\\world\\rooms directory.";
        return false;
    }
    MapModel candidate;
    candidate.filename_ = filename;
    candidate.assets_ = selected.parent_path().parent_path().parent_path();
    candidate.id_ = id;
    candidate.source_ = source;
    const JsonValue layers = document.root().find("layers");
    if (!layers.isArray() || layers.size() == 0) {
        error = "This room has no locally defined editable tile layers.";
        return false;
    }
    for (std::size_t i = 0; i < layers.size(); ++i) {
        std::string_view name;
        const JsonValue layer = layers.at(i);
        const JsonValue rows = layer.find("rows");
        if (!layer.find("name").string(name) || !rows.isArray()) {
            error = "Room layer is invalid.";
            return false;
        }
        MapLayer entry;
        entry.name = name;
        for (std::size_t r = 0; r < rows.size(); ++r) {
            std::string_view row;
            if (!rows.at(r).string(row)) { error = "Tile row is invalid."; return false; }
            entry.rows.emplace_back(row);
        }
        candidate.layers_.push_back(std::move(entry));
    }
    if (!candidate.loadWorld(source, candidate.world_, error)) return false;
    *this = std::move(candidate);
    return true;
}

bool MapModel::paint(std::size_t layer, std::size_t column, std::size_t row, char symbol,
                     std::string& error) {
    if (!room() || layer >= layers_.size() || row >= layers_[layer].rows.size() ||
        column >= layers_[layer].rows[row].size() ||
        (symbol != ' ' && symbol != '.' && !room()->findTileType(symbol))) {
        error = "Tile position or brush is invalid.";
        return false;
    }
    char& cell = layers_[layer].rows[row][column];
    if (cell == symbol) return true;
    if (history_.size() == 512) history_.erase(history_.begin());
    history_.push_back({layer, column, row, cell});
    cell = symbol;
    return true;
}

bool MapModel::undo() {
    if (history_.empty()) return false;
    const Change change = history_.back();
    history_.pop_back();
    layers_[change.layer].rows[change.row][change.column] = change.previous;
    return true;
}

bool MapModel::serialize(std::string& output, std::string& error) const {
    using Document = std::unique_ptr<yyjson_doc, decltype(&yyjson_doc_free)>;
    using Mutable = std::unique_ptr<yyjson_mut_doc, decltype(&yyjson_mut_doc_free)>;
    Document document(yyjson_read(source_.data(), source_.size(), 0), yyjson_doc_free);
    if (!document) { error = "Cannot read the room document."; return false; }
    Mutable edited(yyjson_doc_mut_copy(document.get(), nullptr), yyjson_mut_doc_free);
    if (!edited) { error = "Cannot copy the room document."; return false; }
    auto* layers = yyjson_mut_obj_get(yyjson_mut_doc_get_root(edited.get()), "layers");
    for (std::size_t i = 0; i < layers_.size(); ++i) {
        auto* rows = yyjson_mut_obj_get(yyjson_mut_arr_get(layers, i), "rows");
        for (std::size_t r = 0; r < layers_[i].rows.size(); ++r) {
            const auto& row = layers_[i].rows[r];
            if (!yyjson_mut_set_strn(yyjson_mut_arr_get(rows, r), row.data(), row.size())) {
                error = "Cannot update the tile row.";
                return false;
            }
        }
    }
    std::size_t size{};
    std::unique_ptr<char, decltype(&std::free)> encoded(
        yyjson_mut_write(edited.get(), YYJSON_WRITE_PRETTY, &size), std::free);
    if (!encoded || size > kMaxWorldJsonBytes) {
        error = "Serialized room exceeds the runtime limit or could not be written.";
        return false;
    }
    output.assign(encoded.get(), size);
    return true;
}

bool MapModel::validate(const std::string& candidate, std::string& error) const {
    std::unique_ptr<WorldModule> world;
    return loadWorld(candidate, world, error);
}
} // namespace echo::editor
