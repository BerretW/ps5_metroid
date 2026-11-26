#include "capability_manifest_input.hpp"
#include "echo/framework/abilities.hpp"
#include "echo/framework/cutscenes.hpp"
#include "echo/framework/attributes.hpp"
#include "echo/framework/equipment.hpp"
#include "echo/framework/narrative.hpp"
#include "echo/framework/power.hpp"
#include "echo/framework/world.hpp"
#include "game/game.hpp"
#include "game/project_components.hpp"
#include "platform/presentation/bitmap_font.hpp"
#include "json_input.hpp"

#include <array>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <string_view>

namespace {

using namespace echo::framework;

constexpr std::size_t kProjectContentBytes = 16 * 1024;
constexpr std::array<std::string_view, 13> kGameConfigFields{
    "game", "modules", "technician", "resources", "quests", "cutscenes", "power",
    "worldLighting", "equipment", "attributes", "abilities", "loot", "scripts"};
// The engineer's position belongs to echo:object/engineer spawn data.
constexpr std::array<std::string_view, 1> kTechnicianFields{"interaction"};
constexpr std::array<std::string_view, 6> kResourceFields{
    "id", "type", "source", "optional", "dependsOn", "optionalDependencies"};
constexpr std::array<std::string_view, 2> kComponentFields{
    "echo:component/transform2d", echo::kDialogueInteractionComponentId};

[[nodiscard]] Status validateObjectFields(
    JsonValue object, std::span<const std::string_view> allowed,
    std::string_view subject) noexcept {
    if (!object.isObject() || object.size() > allowed.size()) {
        return {ErrorCode::InvalidValue, subject,
                "Object contains an invalid shape or too many fields."};
    }
    for (std::size_t index = 0; index < object.size(); ++index) {
        std::string_view name;
        JsonValue value{};
        if (!object.memberAt(index, name, value) ||
            std::find(allowed.begin(), allowed.end(), name) == allowed.end()) {
            return {ErrorCode::InvalidValue, subject,
                    "Object contains an unknown field."};
        }
        for (std::size_t previous = 0; previous < index; ++previous) {
            std::string_view previousName;
            JsonValue previousValue{};
            if (!object.memberAt(previous, previousName, previousValue) ||
                previousName == name) {
                return {ErrorCode::DuplicateId, subject,
                        "Object contains a duplicate field."};
            }
        }
    }
    return Status::success();
}

[[nodiscard]] Status validateScriptBindings(JsonValue scripts) noexcept {
    if (!scripts.valid()) {
        return Status::success();
    }
    if (!scripts.isObject()) {
        return {ErrorCode::InvalidValue, "scripts",
                "Script bindings must be a JSON object."};
    }
    std::int64_t schemaVersion{};
    if (!scripts.find("schemaVersion").integer(schemaVersion) ||
        schemaVersion != 1) {
        return {ErrorCode::InvalidValue, "scripts.schemaVersion",
                "Script binding schema version must be 1."};
    }
    constexpr std::array<std::string_view, 2> kScriptFields{
        "schemaVersion", "bindings"};
    Status status = validateObjectFields(scripts, kScriptFields, "scripts");
    if (!status) return status;
    const JsonValue bindings = scripts.find("bindings");
    if (!bindings.isArray() || bindings.size() == 0 || bindings.size() > 16) {
        return {ErrorCode::InvalidValue, "scripts.bindings",
                "Script bindings must be a non-empty bounded array."};
    }
    std::array<std::string_view, 16> ids{};
    std::array<std::string_view, 16> targets{};
    for (std::size_t index = 0; index < bindings.size(); ++index) {
        const JsonValue binding = bindings.at(index);
        constexpr std::array<std::string_view, 4> kBindingFields{
            "id", "kind", "target", "source"};
        if (!binding.isObject() || binding.size() != kBindingFields.size()) {
            return {ErrorCode::InvalidValue, "scripts.bindings",
                    "Script binding must contain exactly id, kind, target and source."};
        }
        status = validateObjectFields(binding, kBindingFields, "scripts.bindings");
        if (!status) return status;
        std::string_view id;
        std::string_view kind;
        std::string_view target;
        std::string_view source;
        if (!binding.find("id").string(id) || id.empty() || id.size() > 64 ||
            !binding.find("kind").string(kind) ||
            (kind != "enemy_ai" && kind != "ui_visibility") ||
            !binding.find("target").string(target) ||
            target.empty() || target.size() > 64 ||
            !binding.find("target").string(target) ||
            !binding.find("source").string(source) ||
            source.empty() || source.size() > 4096 ||
            std::memchr(source.data(), '\0', source.size()) != nullptr) {
            return {ErrorCode::InvalidValue, "scripts.bindings",
                    "Script binding fields or source exceed the authored contract."};
        }
        if ((kind == "enemy_ai" && target.find("echo:object/") != 0) ||
            (kind == "ui_visibility" && target.find("echo:ui/") != 0)) {
            return {ErrorCode::InvalidValue, "scripts.bindings.target",
                    "Script target does not match its binding kind."};
        }
        for (std::size_t previous = 0; previous < index; ++previous) {
            if (ids[previous] == id || targets[previous] == target) {
                return {ErrorCode::DuplicateId, "scripts.bindings",
                        "Script binding IDs and targets must be unique."};
            }
        }
        ids[index] = id;
        targets[index] = target;
    }
    return Status::success();
}

[[nodiscard]] const ManifestModuleDescriptor* findCompiledModule(
    std::span<const CapabilityManifestFragment> fragments,
    std::string_view id) noexcept {
    for (const CapabilityManifestFragment& fragment : fragments) {
        for (const ManifestModuleDescriptor& module : fragment.modules) {
            if (module.id == id) return &module;
        }
    }
    return nullptr;
}

[[nodiscard]] bool matchesStringList(
    JsonValue json, std::span<const std::string_view> expected) noexcept {
    if (!json.isArray() || json.size() != expected.size()) return false;
    std::string_view previous;
    for (std::size_t index = 0; index < json.size(); ++index) {
        std::string_view value;
        if (!json.at(index).string(value) ||
            (index != 0 && previous >= value) ||
            std::find(expected.begin(), expected.end(), value) == expected.end()) {
            return false;
        }
        previous = value;
    }
    return true;
}

[[nodiscard]] Status validateManifestModules(
    JsonValue root, std::span<const CapabilityManifestFragment> fragments) noexcept {
    const JsonValue jsonModules = root.find("modules");
    if (!jsonModules.isArray() || jsonModules.size() > 64) {
        return {ErrorCode::InvalidValue, "modules",
                "Capability manifest modules must be a bounded array."};
    }
    std::size_t expectedCount{};
    for (const CapabilityManifestFragment& fragment : fragments) {
        expectedCount += fragment.modules.size();
    }
    if (jsonModules.size() != expectedCount) {
        return {ErrorCode::InvalidValue, "modules",
                "Capability manifest does not describe the complete compiled module set."};
    }

    std::string_view previousId;
    for (std::size_t index = 0; index < jsonModules.size(); ++index) {
        const JsonValue jsonModule = jsonModules.at(index);
        std::string_view id;
        std::string_view name;
        std::int64_t version{};
        bool selectable{};
        const ManifestModuleDescriptor* compiled =
            jsonModule.find("id").string(id)
                ? findCompiledModule(fragments, id)
                : nullptr;
        if (!jsonModule.isObject() || compiled == nullptr ||
            !jsonModule.find("name").string(name) || name != compiled->name ||
            !jsonModule.find("version").integer(version) ||
            version != compiled->version ||
            !jsonModule.find("selectable").boolean(selectable) || !selectable ||
            !matchesStringList(jsonModule.find("required"), compiled->required) ||
            !matchesStringList(jsonModule.find("optional"), compiled->optional) ||
            !matchesStringList(jsonModule.find("provides"), compiled->provides) ||
            !matchesStringList(jsonModule.find("initializeAfter"),
                               compiled->initializeAfter)) {
            return {ErrorCode::InvalidValue, id,
                    "Capability manifest module metadata differs from compiled descriptors."};
        }
        if (index != 0 && previousId >= id) {
            return {previousId == id ? ErrorCode::DuplicateId : ErrorCode::InvalidValue,
                    id, "Capability manifest modules must be unique and sorted."};
        }
        previousId = id;
    }
    return Status::success();
}

[[nodiscard]] bool matchesActionParameters(
    JsonValue json, std::span<const ManifestActionParameter> expected) noexcept {
    if (!json.isArray() || json.size() != expected.size()) return false;
    for (std::size_t index = 0; index < json.size(); ++index) {
        const JsonValue parameter = json.at(index);
        std::string_view name;
        std::string_view type;
        bool required{};
        if (!parameter.isObject() ||
            !parameter.find("name").string(name) || name != expected[index].name ||
            !parameter.find("type").string(type) || type != expected[index].type ||
            !parameter.find("required").boolean(required) ||
            required != expected[index].required) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] Status validateManifestActions(
    JsonValue root, std::span<const CapabilityManifestFragment> fragments) noexcept {
    std::size_t expectedCapabilityCount{};
    std::size_t expectedActionCount{};
    for (const CapabilityManifestFragment& fragment : fragments) {
        expectedCapabilityCount += fragment.capabilities.size();
        expectedActionCount += fragment.actions.size();
    }
    const JsonValue jsonCapabilities = root.find("capabilities");
    if (!jsonCapabilities.isArray() ||
        jsonCapabilities.size() != expectedCapabilityCount) {
        return {ErrorCode::InvalidValue, "capabilities",
                "Capability manifest does not match the compiled project capabilities."};
    }
    std::string_view previous;
    for (std::size_t index = 0; index < jsonCapabilities.size(); ++index) {
        std::string_view id;
        if (!jsonCapabilities.at(index).string(id) ||
            (index != 0 && previous >= id) ||
            std::none_of(fragments.begin(), fragments.end(),
                         [id](const CapabilityManifestFragment& fragment) {
                             return std::any_of(
                                 fragment.capabilities.begin(), fragment.capabilities.end(),
                                 [id](const ManifestCapabilityDescriptor& expected) {
                                     return expected.id == id;
                                 });
                         })) {
            return {ErrorCode::InvalidValue, "capabilities",
                    "Capability IDs are unknown, duplicated, or unsorted."};
        }
        previous = id;
    }

    const JsonValue jsonActions = root.find("actions");
    if (!jsonActions.isArray() || jsonActions.size() != expectedActionCount) {
        return {ErrorCode::InvalidValue, "actions",
                "Capability manifest does not match the compiled project actions."};
    }
    previous = {};
    for (std::size_t index = 0; index < jsonActions.size(); ++index) {
        const JsonValue jsonAction = jsonActions.at(index);
        std::string_view id;
        std::string_view capability;
        bool requiresActiveGame{};
        const ManifestActionDescriptor* expected{};
        if (!jsonAction.isObject() || !jsonAction.find("id").string(id) ||
            (index != 0 && previous >= id)) {
            return {ErrorCode::InvalidValue, "actions",
                    "Capability manifest actions must be sorted and unique."};
        }
        for (const CapabilityManifestFragment& fragment : fragments) {
            for (const ManifestActionDescriptor& action : fragment.actions) {
                if (action.id == id) expected = &action;
            }
        }
        if (expected == nullptr ||
            !jsonAction.find("capability").string(capability) ||
            capability != expected->capability ||
            !jsonAction.find("requiresActiveGame").boolean(requiresActiveGame) ||
            requiresActiveGame != expected->requiresActiveGame ||
            !matchesActionParameters(jsonAction.find("parameters"),
                                     expected->parameters)) {
            return {ErrorCode::InvalidValue, id,
                    "Capability manifest action metadata differs from compiled descriptors."};
        }
        previous = id;
    }
    return Status::success();
}

[[nodiscard]] const ManifestActionDescriptor* findAction(
    std::span<const CapabilityManifestFragment> fragments,
    std::string_view id) noexcept {
    for (const CapabilityManifestFragment& fragment : fragments) {
        for (const ManifestActionDescriptor& action : fragment.actions) {
            if (action.id == id) return &action;
        }
    }
    return nullptr;
}

[[nodiscard]] bool valueMatches(std::string_view type,
                                ValueType valueType) noexcept {
    return (type == "boolean" && valueType == ValueType::Boolean) ||
           (type == "integer" && valueType == ValueType::Integer) ||
           (type == "number" && valueType == ValueType::Number) ||
           (type == "string" && valueType == ValueType::String);
}

[[nodiscard]] Status validateDialogueActions(
    const DialogueDefinition& definition,
    std::span<const CapabilityManifestFragment> fragments,
    const ItemCatalog& itemCatalog,
    JsonValue manifest) noexcept {
    const JsonValue actions = manifest.find("actions");
    for (const DialogueNode& node : definition.nodes) {
        for (const DialogueChoice& choice : node.choices) {
            const ActionRequest& request = choice.onSelect;
            if (request.id.empty()) continue;
            const ManifestActionDescriptor* expected =
                findAction(fragments, request.id);
            if (expected == nullptr) {
                return {ErrorCode::NotFound, request.id,
                        "Dialogue references an action that is not compiled into the game."};
            }
            bool foundInManifest{};
            for (std::size_t index = 0; index < actions.size(); ++index) {
                std::string_view id;
                if (actions.at(index).find("id").string(id) && id == request.id) {
                    foundInManifest = true;
                    break;
                }
            }
            if (!foundInManifest) {
                return {ErrorCode::MissingDependency, request.id,
                        "Dialogue action is absent from the capability manifest."};
            }
            if (request.parameters.size() > expected->parameters.size()) {
                return {ErrorCode::InvalidValue, request.id,
                        "Dialogue action supplies too many parameters."};
            }
            for (const ManifestActionParameter& parameter : expected->parameters) {
                const ActionParameter* value{};
                for (const ActionParameter& candidate : request.parameters) {
                    if (candidate.name == parameter.name) value = &candidate;
                }
                if (value == nullptr) {
                    if (parameter.required) {
                        return {ErrorCode::InvalidValue, request.id,
                                "Dialogue action omits a required parameter."};
                    }
                } else if (!valueMatches(parameter.type, value->value.type)) {
                    return {ErrorCode::InvalidValue, request.id,
                            "Dialogue action parameter type does not match its contract."};
                }
            }
            for (const ActionParameter& value : request.parameters) {
                if (std::none_of(expected->parameters.begin(), expected->parameters.end(),
                                 [&value](const ManifestActionParameter& parameter) {
                                     return parameter.name == value.name;
                                 })) {
                    return {ErrorCode::InvalidValue, request.id,
                            "Dialogue action supplies an unknown parameter."};
                }
            }
            if (request.id == "echo.inventory.grant") {
                const ActionParameter* item{};
                for (const ActionParameter& parameter : request.parameters) {
                    if (parameter.name == "item") item = &parameter;
                }
                const ItemDefinition* definition =
                    item != nullptr && item->value.type == ValueType::String
                        ? itemCatalog.find(item->value.stringValue()) : nullptr;
                if (definition == nullptr || definition->key == 0 ||
                    definition->key >= static_cast<std::uint32_t>(echo::ItemId::Count)) {
                    return {ErrorCode::InvalidValue, request.id,
                            "Dialogue grants must reference an item in the selected catalog."};
                }
            }
        }
    }
    return Status::success();
}

[[nodiscard]] Status requireResource(const ContentRegistry& resources,
                                     std::string_view id, ResourceType type) noexcept {
    ResourceInfo resource{};
    const Status found = resources.find(id, resource);
    if (!found || resource.type != type || resource.optional) {
        return {ErrorCode::MissingDependency, id,
                "Required project resource is missing, optional, or has the wrong type."};
    }
    return Status::success();
}

[[nodiscard]] Status readBoundedFile(const char* path, char* output,
                                     std::size_t capacity,
                                     std::size_t& size) noexcept {
    size = 0;
    std::FILE* file = std::fopen(path, "rb");
    if (file == nullptr) {
        return {ErrorCode::StorageFailure, path,
                "Could not open required project content."};
    }
    size = std::fread(output, 1, capacity + 1, file);
    const bool readFailed = std::ferror(file) != 0;
    const int closeResult = std::fclose(file);
    if (readFailed || closeResult != 0) {
        return {ErrorCode::StorageFailure, path,
                "Could not read complete project content."};
    }
    if (size == 0) {
        return {ErrorCode::InvalidValue, path,
                "Project content is empty."};
    }
    if (size > capacity) {
        size = 0;
        return {ErrorCode::CapacityExceeded, path,
                "Project content exceeds the 16 KiB limit."};
    }
    return Status::success();
}

[[nodiscard]] Status validateGameConfig(
    JsonValue root, const ComponentTypeRegistry& components,
    const std::array<CapabilityManifestFragment, 11>& fragments,
    const CapabilityManifestFragment& project,
    const ItemCatalog& itemCatalog,
    ContentRegistry& resources) noexcept {
    Status status = validateObjectFields(root, kGameConfigFields, "game");
    if (!status) return status;
    std::string_view gameId;
    if (!root.find("game").string(gameId) || gameId != "echo.project_echo") {
        return {ErrorCode::InvalidValue, "game",
                "Game configuration must target echo.project_echo."};
    }
    status = validateScriptBindings(root.find("scripts"));
    if (!status) return status;

    const JsonValue modules = root.find("modules");
    if (!modules.isArray() || modules.size() == 0 || modules.size() > 11) {
        return {ErrorCode::InvalidValue, "modules",
                "Game configuration requires a bounded non-empty module list."};
    }
    std::array<std::string_view, 11> selected{};
    for (std::size_t index = 0; index < modules.size(); ++index) {
        if (!modules.at(index).string(selected[index]) ||
            findCompiledModule(fragments, selected[index]) == nullptr) {
            return {ErrorCode::MissingDependency, "modules",
                    "Configured module is not compiled into this application."};
        }
        for (std::size_t previous = 0; previous < index; ++previous) {
            if (selected[previous] == selected[index]) {
                return {ErrorCode::DuplicateId, "modules",
                        "A module is selected more than once."};
            }
        }
    }
    for (const std::string_view requiredModule :
         {std::string_view{"echo.state"},
          std::string_view{"echo.narrative.dialogue"},
          std::string_view{"echo.rpg.abilities"},
          std::string_view{"echo.rpg.quests"},
          std::string_view{"echo.narrative.cutscenes"},
          std::string_view{"echo.project_echo"}}) {
        if (std::find(selected.begin(), selected.begin() +
                          static_cast<std::ptrdiff_t>(modules.size()),
                      requiredModule) == selected.begin() +
                          static_cast<std::ptrdiff_t>(modules.size())) {
            return {ErrorCode::MissingDependency, requiredModule,
                    "Game configuration omits a required compiled module."};
        }
    }
    if (std::find(selected.begin(), selected.begin() +
                      static_cast<std::ptrdiff_t>(modules.size()),
                  "echo.rpg.inventory") ==
        selected.begin() + static_cast<std::ptrdiff_t>(modules.size())) {
        return {ErrorCode::MissingDependency, "echo.rpg.inventory",
                "The reference game must select its compiled inventory module."};
    }
    const bool equipmentSelected =
        std::find(selected.begin(), selected.begin() +
                     static_cast<std::ptrdiff_t>(modules.size()),
                 "echo.rpg.equipment") !=
        selected.begin() + static_cast<std::ptrdiff_t>(modules.size());
    if (equipmentSelected) {
        EquipmentModule equipment{};
        status = equipment.load(root.find("equipment"));
        if (!status) return status;
    } else if (root.find("equipment").valid()) {
        return {ErrorCode::MissingDependency, "echo.rpg.equipment",
                "Equipment content requires the compiled equipment module."};
    }
    const bool attributesSelected =
        std::find(selected.begin(), selected.begin() +
                     static_cast<std::ptrdiff_t>(modules.size()),
                 "echo.rpg.attributes") !=
        selected.begin() + static_cast<std::ptrdiff_t>(modules.size());
    if (attributesSelected) {
        AttributesModule attributes{};
        status = attributes.load(root.find("attributes"));
        if (!status) return status;
    } else if (root.find("attributes").valid()) {
        return {ErrorCode::MissingDependency, "echo.rpg.attributes",
                "Attribute content requires the compiled attributes module."};
    }
    const bool abilitiesSelected =
        std::find(selected.begin(), selected.begin() +
                     static_cast<std::ptrdiff_t>(modules.size()),
                 "echo.rpg.abilities") !=
        selected.begin() + static_cast<std::ptrdiff_t>(modules.size());
    if (abilitiesSelected) {
        AbilityModule abilities{};
        status = abilities.load(root.find("abilities"));
        if (!status) return status;
    } else if (root.find("abilities").valid()) {
        return {ErrorCode::MissingDependency, "echo.rpg.abilities",
                "Ability content requires the compiled abilities module."};
    }
    const bool powerSelected =
        std::find(selected.begin(), selected.begin() +
                      static_cast<std::ptrdiff_t>(modules.size()),
                  "echo.power") != selected.begin() +
                      static_cast<std::ptrdiff_t>(modules.size());
    const bool worldSelected =
        std::find(selected.begin(), selected.begin() +
                      static_cast<std::ptrdiff_t>(modules.size()),
                  "echo.world") != selected.begin() +
                      static_cast<std::ptrdiff_t>(modules.size());
    WorldLighting worldLighting{};
    status = WorldModule::parseWorldLighting(root.find("worldLighting"), worldLighting);
    if (!status) return status;
    if (!worldSelected && root.find("worldLighting").valid()) {
        return {ErrorCode::MissingDependency, "echo.world",
                "World lighting content requires the echo.world module."};
    }
    if (powerSelected) {
        PowerModule powerModule{};
        status = powerModule.load(root.find("power"));
        if (!status) return status;
    } else if (root.find("power").valid()) {
        return {ErrorCode::MissingDependency, "echo.power",
                "Power content requires the echo.power module to be selected."};
    }
    QuestModule questModule{};
    status = questModule.load(root.find("quests"));
    if (!status) return status;
    status = echo::validateProjectQuest(questModule);
    if (!status) return status;
    status = echo::validateProjectQuestRewards(
        questModule, itemCatalog, static_cast<std::uint32_t>(echo::ItemId::Count));
    if (!status) return status;
    for (const QuestDefinition& quest : questModule.definitions()) {
        for (const QuestObjective& objective : quest.objectives()) {
            if (!echo::platform::supportsBitmapText(objective.text.data(), 48)) {
                return {ErrorCode::InvalidValue, "quests.objective.text",
                        "Quest objective does not fit the game's bitmap font."};
            }
        }
        if (!quest.branchPrompt().empty() &&
            !echo::platform::supportsBitmapText(quest.branchPrompt(), 48)) {
            return {ErrorCode::InvalidValue, "quests.branch.prompt",
                    "Quest branch prompt does not fit the game's bitmap font."};
        }
        for (const QuestBranch& branch : quest.branches()) {
            if (!echo::platform::supportsBitmapText(branch.label.data(), 48) ||
                !echo::platform::supportsBitmapText(branch.objective.data(), 48)) {
                return {ErrorCode::InvalidValue, "quests.branch",
                        "Quest branch text does not fit the game's bitmap font."};
            }
        }
    }
    CutsceneModule cutsceneModule{};
    status = cutsceneModule.load(root.find("cutscenes"));
    if (!status) return status;
    status = echo::validateProjectCutscenes(cutsceneModule);
    if (!status) return status;
    for (const CutsceneDefinition& cutscene : cutsceneModule.definitions()) {
        for (const CutsceneCard& card : cutscene.cards()) {
            if (!echo::platform::supportsBitmapText(card.speaker.data(), 32) ||
                !echo::platform::supportsBitmapText(card.text.data(), 72) ||
                !echo::platform::supportsBitmapText(card.speakerCs.data(), 32) ||
                !echo::platform::supportsBitmapText(card.textCs.data(), 72)) {
                return {ErrorCode::InvalidValue, "cutscenes.card",
                        "Cutscene text does not fit the localized bitmap font layout."};
            }
        }
    }
    for (const ManifestModuleDescriptor& module : project.modules) {
        if (std::find(selected.begin(), selected.begin() +
                          static_cast<std::ptrdiff_t>(modules.size()),
                      module.id) == selected.begin() +
                          static_cast<std::ptrdiff_t>(modules.size())) {
            continue;
        }
        for (const std::string_view dependency : module.required) {
            if (std::find(selected.begin(), selected.begin() +
                              static_cast<std::ptrdiff_t>(modules.size()),
                          dependency) == selected.begin() +
                              static_cast<std::ptrdiff_t>(modules.size())) {
                return {ErrorCode::MissingDependency, dependency,
                        "Selected module is missing a required module dependency."};
            }
        }
    }

    status = loadResourceManifest(root, resources);
    if (!status) return status;
    const JsonValue resourceEntries = root.find("resources");
    for (std::size_t index = 0; index < resourceEntries.size(); ++index) {
        status = validateObjectFields(resourceEntries.at(index), kResourceFields,
                                      "resources");
        if (!status) return status;
    }
    status = requireResource(resources, "echo:ability/echo_step",
                             ResourceType::Ability);
    if (!status) return status;
    status = requireResource(resources, "echo:dialogue/engineer",
                             ResourceType::Dialogue);
    if (!status) return status;
    status = requireResource(resources, "echo:cutscene/central_signal",
                             ResourceType::Cutscene);
    if (!status) return status;
    for (std::size_t index = 0; index < itemCatalog.size(); ++index) {
        const ItemDefinition* item = itemCatalog.at(index);
        if (item->key == 0 ||
            item->key >= static_cast<std::uint32_t>(echo::ItemId::Count)) {
            return {ErrorCode::InvalidValue, item->id.data(),
                    "Item keys must remain in the stable gameplay save range."};
        }
        status = requireResource(resources, item->id.data(), ResourceType::Item);
        if (!status) return status;
    }
    for (unsigned key = 1; key < static_cast<unsigned>(echo::ItemId::Count); ++key) {
        if (itemCatalog.find(key) == nullptr) {
            return {ErrorCode::MissingDependency, "echo.rpg.inventory",
                    "Item catalog must define every stable gameplay save key."};
        }
    }
    echo::EnemyLootTable lootTable{};
    status = lootTable.load(root.find("loot"), itemCatalog);
    if (!status) return status;

    const JsonValue technician = root.find("technician");
    status = validateObjectFields(technician, kTechnicianFields, "technician");
    if (!status) return status;
    ComponentInstance instance{};
    const JsonValue interaction = technician.find("interaction");
    if (interaction.valid()) {
        status = components.instantiate(echo::kDialogueInteractionComponentId,
                                        interaction, instance, &resources);
        if (!status) return {status.code, "technician.interaction", status.detail};
        echo::DialogueInteractionComponent configuredInteraction{};
        if (!instance.read(configuredInteraction)) {
            return {ErrorCode::InvalidState, "technician.interaction",
                    "Interaction component factory returned an invalid representation."};
        }
    }
    return Status::success();
}

void reportInputFailure(const char* label, const Status& status,
                        const JsonError& error) {
    if (status.code == ErrorCode::ParseError) {
        std::fprintf(stderr, "%s JSON parse failed at byte %zu: %.*s\n",
                     label, error.offset, static_cast<int>(error.message.size()),
                     error.message.data());
    } else {
        echo::framework::tools::reportStatus(status);
    }
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 5) {
        std::fprintf(stderr,
                     "Usage: echo_project_content_validate <game.json> "
                     "<dialogue.json> <items.json> <capability-manifest.json>\n");
        return 2;
    }

    ComponentTypeRegistry components{};
    Status status = registerCoreComponentTypes(components);
    if (!status) {
        tools::reportStatus(status);
        return 1;
    }
    status = echo::registerProjectComponentTypes(components);
    if (!status) {
        tools::reportStatus(status);
        return 1;
    }
    const std::array<CapabilityManifestFragment, 11> fragments{{
        frameworkCapabilityManifestFragment(),
        narrativeCapabilityManifestFragment(),
        inventoryCapabilityManifestFragment(),
        attributesCapabilityManifestFragment(),
        equipmentCapabilityManifestFragment(),
        abilitiesCapabilityManifestFragment(),
        questsCapabilityManifestFragment(),
        cutscenesCapabilityManifestFragment(),
        worldCapabilityManifestFragment(),
        powerCapabilityManifestFragment(),
        echo::projectCapabilityManifestFragment(),
    }};
    const CapabilityManifestFragment project =
        echo::projectCapabilityManifestFragment();

    JsonDocument manifestDocument{};
    JsonError error{};
    JsonDocument itemDocument{};
    status = tools::readJsonFile(argv[3], itemDocument, error, kProjectContentBytes);
    if (!status) {
        reportInputFailure("Item catalog", status, error);
        return 1;
    }
    InventoryModule itemModule{};
    status = itemModule.load(itemDocument.root());
    if (!status) {
        tools::reportStatus(status);
        return 1;
    }
    const ItemDefinition* maintenanceKey =
        itemModule.catalog().find("echo:item/maintenance_key");
    if (maintenanceKey == nullptr ||
        maintenanceKey->key != static_cast<std::uint32_t>(echo::ItemId::MaintenanceKey)) {
        tools::reportStatus({ErrorCode::MissingDependency, "echo:item/maintenance_key",
                             "Item catalog must define the compiled maintenance-key item."});
        return 1;
    }
    const ItemDefinition* healthCell =
        itemModule.catalog().find("echo:item/health_cell");
    if (healthCell == nullptr ||
        healthCell->key != static_cast<std::uint32_t>(echo::ItemId::HealthCell)) {
        tools::reportStatus({ErrorCode::MissingDependency, "echo:item/health_cell",
                             "Item catalog must preserve the stable Health Cell save key."});
        return 1;
    }
    for (unsigned key = 1; key < static_cast<unsigned>(echo::ItemId::Count); ++key) {
        if (itemModule.catalog().find(key) == nullptr) {
            tools::reportStatus({ErrorCode::MissingDependency, "echo.rpg.inventory",
                                 "Item catalog must define every stable gameplay save key."});
            return 1;
        }
    }
    status = tools::readJsonFile(argv[4], manifestDocument, error);
    if (!status) {
        reportInputFailure("Capability manifest", status, error);
        return 1;
    }
    JsonValue manifestComponents{};
    status = tools::validateCapabilityManifest(
        manifestDocument.root(), components, manifestComponents);
    if (!status) {
        tools::reportStatus(status);
        return 1;
    }
    status = validateManifestModules(manifestDocument.root(), fragments);
    if (!status) {
        tools::reportStatus(status);
        return 1;
    }
    status = validateManifestActions(manifestDocument.root(), fragments);
    if (!status) {
        tools::reportStatus(status);
        return 1;
    }
    for (const std::string_view id : kComponentFields) {
        JsonValue component{};
        status = tools::findManifestComponent(manifestComponents, id, component);
        if (!status) {
            tools::reportStatus(status);
            return 1;
        }
    }

    JsonDocument gameDocument{};
    status = tools::readJsonFile(argv[1], gameDocument, error, kProjectContentBytes);
    if (!status) {
        reportInputFailure("Game configuration", status, error);
        return 1;
    }
    ContentRegistry resources{};
    status = validateGameConfig(gameDocument.root(), components, fragments,
                                project, itemModule.catalog(), resources);
    if (!status) {
        tools::reportStatus(status);
        return 1;
    }

    std::array<char, kProjectContentBytes + 1> dialogueBytes{};
    std::size_t dialogueSize{};
    status = readBoundedFile(argv[2], dialogueBytes.data(),
                             kProjectContentBytes, dialogueSize);
    if (!status) {
        tools::reportStatus(status);
        return 1;
    }
    DialogueDocument dialogue{};
    status = dialogue.parse(dialogueBytes.data(), dialogueSize, error);
    if (!status) {
        reportInputFailure("Dialogue", status, error);
        return 1;
    }
    const DialogueDefinition* definition = dialogue.definition();
    if (definition == nullptr || definition->id != "echo:dialogue/engineer") {
        tools::reportStatus({ErrorCode::InvalidValue, "dialogue.id",
                             "Dialogue document ID does not match the required resource."});
        return 1;
    }
    DialogueModule dialogueModule{};
    status = dialogueModule.add(*definition);
    if (!status) {
        tools::reportStatus(status);
        return 1;
    }
    status = dialogueModule.validate();
    if (!status) {
        tools::reportStatus(status);
        return 1;
    }

    constexpr std::array<std::size_t, 4> textLimits{39, 80, 39, 80};
    for (const DialogueNode& node : definition->nodes) {
        if (!echo::platform::supportsBitmapText(node.speaker, textLimits[0]) ||
            !echo::platform::supportsBitmapText(node.text, textLimits[1]) ||
            (!node.speakerCs.empty() &&
             !echo::platform::supportsBitmapText(node.speakerCs, textLimits[2])) ||
            (!node.textCs.empty() &&
             !echo::platform::supportsBitmapText(node.textCs, textLimits[3]))) {
            tools::reportStatus({ErrorCode::InvalidValue, "dialogue.text",
                                 "Dialogue text does not fit the game's bitmap font."});
            return 1;
        }
        for (const DialogueChoice& choice : node.choices) {
            if (!echo::platform::supportsBitmapText(choice.label, 72) ||
                (!choice.labelCs.empty() &&
                 !echo::platform::supportsBitmapText(choice.labelCs, 72))) {
                tools::reportStatus({ErrorCode::InvalidValue, "dialogue.label",
                                     "Dialogue choice does not fit the game's bitmap font."});
                return 1;
            }
        }
    }

    status = validateDialogueActions(*definition, fragments,
                                     itemModule.catalog(),
                                     manifestDocument.root());
    if (!status) {
        tools::reportStatus(status);
        return 1;
    }

    std::size_t moduleCount{};
    for (const CapabilityManifestFragment& fragment : fragments) {
        moduleCount += fragment.modules.size();
    }
    std::printf("Validated PROJECT ECHO content: %zu modules, %zu resources, "
                "%zu dialogue nodes.\n",
                moduleCount,
                resources.size(), definition->nodes.size());
    return 0;
}
