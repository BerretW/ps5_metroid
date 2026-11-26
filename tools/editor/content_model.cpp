#include "content_model.hpp"

#include "echo/framework/attributes.hpp"
#include "echo/framework/quests.hpp"
#include "echo/framework/world.hpp"
#include "game/project_quests.hpp"
#include "platform/presentation/bitmap_font.hpp"
#include "yyjson.h"

#include <charconv>
#include <cstring>
#include <cstdlib>
#include <memory>

namespace echo::editor {
namespace {

using namespace echo::framework;

bool uniqueMembers(JsonValue value, std::size_t depth) {
    if (depth > 64) return false;
    if (value.isArray()) {
        for (std::size_t i = 0; i < value.size(); ++i)
            if (!uniqueMembers(value.at(i), depth + 1)) return false;
    } else if (value.isObject()) {
        if (value.size() > 256) return false;
        for (std::size_t i = 0; i < value.size(); ++i) {
            std::string_view key;
            JsonValue child;
            if (!value.memberAt(i, key, child) || !uniqueMembers(child, depth + 1)) return false;
            for (std::size_t j = 0; j < i; ++j) {
                std::string_view previous;
                JsonValue ignored;
                if (!value.memberAt(j, previous, ignored) || key == previous) return false;
            }
        }
    }
    return true;
}

bool statusError(Status status, std::string& error) {
    if (status) return false;
    error = std::string(status.subject) + ": " + std::string(status.detail);
    return true;
}

bool parse(std::string_view source, JsonDocument& doc, std::string& error) {
    JsonError jsonError{};
    Status status = JsonDocument::parse(source.data(), source.size(), doc, jsonError);
    if (statusError(status, error)) return false;
    if (!uniqueMembers(doc.root(), 0)) {
        error = "Duplicate fields or excessive object size in JSON.";
        return false;
    }
    return true;
}

bool inspect(std::string_view source, std::vector<Field>& fields, std::string& error) {
    JsonDocument document;
    if (!parse(source, document, error)) return false;
    const JsonValue root = document.root();
    std::string_view id;
    fields.clear();
    if (root.find("game").string(id) && id == "echo.project_echo") {
        if (source.size() > 16 * 1024) {
            error = "Game configuration exceeds the 16 KiB runtime limit.";
            return false;
        }
        AttributesModule attributes;
        if (statusError(attributes.load(root.find("attributes")), error)) return false;
        const auto definitions = attributes.definitions();
        for (std::size_t i = 0; i < definitions.size(); ++i) {
            const auto& definition = definitions[i];
            fields.push_back({FieldKind::Attribute, i, 0, std::string(definition.id),
                              std::to_string(definition.baseValue),
                              definition.minimum, definition.maximum});
        }
        QuestModule quests;
        if (statusError(quests.load(root.find("quests")), error) ||
            statusError(validateProjectQuest(quests), error)) return false;
        const JsonValue questArray = root.find("quests").find("quests");
        for (std::size_t q = 0; q < questArray.size(); ++q) {
            const JsonValue quest = questArray.at(q);
            std::string_view questId;
            if (!quest.find("id").string(questId)) {
                error = "Quest ID is missing.";
                return false;
            }
            const JsonValue objectives = quest.find("objectives");
            for (std::size_t o = 0; o < objectives.size(); ++o) {
                std::string_view objectiveId, text;
                if (!objectives.at(o).find("id").string(objectiveId) ||
                    !objectives.at(o).find("text").string(text) ||
                    !platform::supportsBitmapText(text, 48)) {
                    error = "Quest objective text does not fit the game font.";
                    return false;
                }
                fields.push_back({FieldKind::Objective, q, o,
                                  "Quest " + std::string(questId) + " / " + std::string(objectiveId),
                                  std::string(text), 0, 0});
            }
        }
        const JsonValue scripts = root.find("scripts");
        if (scripts.valid()) {
            std::int64_t schemaVersion{};
            if (!scripts.isObject() ||
                !scripts.find("schemaVersion").integer(schemaVersion) ||
                schemaVersion != 1) {
                error = "Script schema version must be 1.";
                return false;
            }
            const JsonValue bindings = scripts.find("bindings");
            if (!bindings.isArray() || bindings.size() == 0 || bindings.size() > 16) {
                error = "Script bindings must be a non-empty bounded array.";
                return false;
            }
            for (std::size_t i = 0; i < bindings.size(); ++i) {
                std::string_view id, kind, target, source;
                if (!bindings.at(i).isObject() ||
                    !bindings.at(i).find("id").string(id) ||
                    !bindings.at(i).find("kind").string(kind) ||
                    !bindings.at(i).find("target").string(target) ||
                    !bindings.at(i).find("source").string(source) ||
                    id.empty() || id.size() > 64 ||
                    (kind != "enemy_ai" && kind != "ui_visibility") ||
                    target.empty() || target.size() > 64 ||
                    source.empty() || source.size() > 4096 ||
                    std::memchr(source.data(), '\0', source.size()) != nullptr) {
                    error = "Invalid script binding.";
                    return false;
                }
                fields.push_back({FieldKind::Script, i, 0,
                                  std::string(id) + " / " + std::string(kind),
                                  std::string(source), 1, 4096});
            }
        }
        return true;
    }
    if (root.find("id").string(id) && id == "echo:object/enemy_stalker") {
        if (source.size() > kMaxWorldJsonBytes) {
            error = "World object exceeds the 32 KiB runtime limit.";
            return false;
        }
        constexpr std::string_view manifest =
            R"({"schemaVersion":1,"startRoom":"echo:room/test","objects":["echo:object/enemy_stalker"],"rooms":["echo:room/test"]})";
        JsonDocument manifestDocument;
        if (!parse(manifest, manifestDocument, error)) return false;
        auto world = std::make_unique<WorldModule>();
        if (statusError(world->loadManifest(manifestDocument.root()), error) ||
            statusError(world->addObject(id, root), error)) return false;
        JsonValue properties = root.find("properties");
        std::string_view behavior;
        if (!properties.find("behavior").string(behavior) || behavior != "stalker") {
            error = "This editor supports the stalker behavior only.";
            return false;
        }
        constexpr const char* names[] = {"health", "speed", "detectionRange"};
        constexpr std::int64_t minimum[] = {1, 0, 0};
        constexpr std::int64_t maximum[] = {99, 400, 4096};
        for (std::size_t i = 0; i < 3; ++i) {
            std::int64_t value{};
            if (!properties.find(names[i]).integer(value) ||
                value < minimum[i] || value > maximum[i]) {
                error = std::string("Invalid stalker property: ") + names[i];
                return false;
            }
            fields.push_back({FieldKind::Stalker, i, 0, std::string("Stalker / ") + names[i],
                              std::to_string(value), minimum[i], maximum[i]});
        }
        return true;
    }
    error = "Open PROJECT ECHO game.json or enemy_stalker.json.";
    return false;
}

} // namespace

bool ContentModel::open(std::string_view source, std::string& error) {
    std::vector<Field> candidate;
    if (!inspect(source, candidate, error)) return false;
    source_.assign(source);
    fields_ = std::move(candidate);
    error.clear();
    return true;
}

bool ContentModel::edit(std::size_t index, std::string_view value, std::string& error) {
    if (index >= fields_.size()) {
        error = "Select a field first.";
        return false;
    }
    const Field& field = fields_[index];
    std::int64_t number{};
    if (field.kind == FieldKind::Objective) {
        if (value.empty() || !platform::supportsBitmapText(value, 48)) {
            error = "Objective text must use the game font and contain 1-48 characters.";
            return false;
        }
    } else if (field.kind == FieldKind::Script) {
        if (value.empty() || value.size() > 4096 ||
            std::memchr(value.data(), '\0', value.size()) != nullptr) {
            error = "Script source must contain 1-4096 bytes and no embedded NUL.";
            return false;
        }
    } else {
        const auto parsed = std::from_chars(value.data(), value.data() + value.size(), number);
        if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() ||
            number < field.minimum || number > field.maximum) {
            error = "Enter a whole number between " + std::to_string(field.minimum) +
                    " and " + std::to_string(field.maximum) + ".";
            return false;
        }
    }
    using Doc = std::unique_ptr<yyjson_doc, decltype(&yyjson_doc_free)>;
    using MutableDoc = std::unique_ptr<yyjson_mut_doc, decltype(&yyjson_mut_doc_free)>;
    Doc original(yyjson_read(source_.data(), source_.size(), 0), yyjson_doc_free);
    if (!original) {
        error = "Could not read the open document.";
        return false;
    }
    MutableDoc mutableDoc(yyjson_doc_mut_copy(original.get(), nullptr), yyjson_mut_doc_free);
    if (!mutableDoc) {
        error = "Could not copy the open document.";
        return false;
    }
    yyjson_mut_val* root = yyjson_mut_doc_get_root(mutableDoc.get());
    yyjson_mut_val* target{};
    if (field.kind == FieldKind::Attribute) {
        auto* attrs = yyjson_mut_obj_get(root, "attributes");
        auto* definitions = yyjson_mut_obj_get(attrs, "definitions");
        target = yyjson_mut_obj_get(yyjson_mut_arr_get(definitions, field.first), "baseValue");
    } else if (field.kind == FieldKind::Script) {
        auto* scripts = yyjson_mut_obj_get(root, "scripts");
        auto* bindings = yyjson_mut_obj_get(scripts, "bindings");
        target = yyjson_mut_obj_get(yyjson_mut_arr_get(bindings, field.first), "source");
    } else if (field.kind == FieldKind::Stalker) {
        constexpr const char* names[] = {"health", "speed", "detectionRange"};
        target = yyjson_mut_obj_get(yyjson_mut_obj_get(root, "properties"), names[field.first]);
    } else {
        auto* quests = yyjson_mut_obj_get(yyjson_mut_obj_get(root, "quests"), "quests");
        auto* quest = yyjson_mut_arr_get(quests, field.first);
        auto* objectives = yyjson_mut_obj_get(quest, "objectives");
        target = yyjson_mut_obj_get(yyjson_mut_arr_get(objectives, field.second), "text");
    }
    if (!target || !((field.kind == FieldKind::Objective ||
                      field.kind == FieldKind::Script)
                         ? yyjson_mut_set_strn(target, value.data(), value.size())
                         : yyjson_mut_set_sint(target, number))) {
        error = "Could not update the selected field.";
        return false;
    }
    std::size_t size{};
    std::unique_ptr<char, decltype(&std::free)> encoded(
        yyjson_mut_write(mutableDoc.get(), YYJSON_WRITE_PRETTY, &size), std::free);
    if (!encoded) {
        error = "Could not serialize the modified document.";
        return false;
    }
    return open(std::string_view(encoded.get(), size), error);
}

} // namespace echo::editor
