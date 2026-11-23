#include "platform/sdl/framework_integration.hpp"

#include "echo/framework/data.hpp"
#include "echo/framework/schema.hpp"
#include "game/game.hpp"
#include "platform/presentation/bitmap_font.hpp"

#include <SDL.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <memory>
#include <new>
#include <string_view>
#include <utility>

namespace echo::platform {
namespace {

constexpr std::array<std::string_view, 1> kStateServices{"echo.state"};
constexpr std::array<std::string_view, 2> kProjectDependencies{
    "echo.state", "echo.narrative.dialogue"};
constexpr std::array<std::string_view, 8> kProjectOptionalModules{
    "echo.narrative.cutscenes", "echo.rpg.abilities",
    "echo.rpg.attributes", "echo.rpg.equipment",
    "echo.rpg.inventory", "echo.rpg.quests", "echo.world", "echo.power"};
constexpr std::array<std::string_view, 1> kProjectServices{"echo.actions"};
constexpr std::size_t kMaxGameConfigBytes = 16 * 1024;
constexpr std::size_t kMaxDialogueBytes = 16 * 1024;

const framework::ModuleInfo kStateModuleInfo{
    "echo.state", "Echo state service", 1, {}, {}, kStateServices, {}};
const framework::ModuleInfo kProjectModuleInfo{
    "echo.project_echo", "PROJECT ECHO adapter", 1,
    kProjectDependencies, kProjectOptionalModules, kProjectServices, {}};

struct SdlFree {
    void operator()(char* data) const noexcept { SDL_free(data); }
};

using FileBuffer = std::unique_ptr<char, SdlFree>;

[[nodiscard]] bool supportsDialogueText(std::string_view text,
                                        std::size_t maximum) noexcept {
    return supportsBitmapText(text, maximum);
}

[[nodiscard]] framework::Status validateDialogueDisplay(
    const framework::DialogueDefinition& definition) noexcept {
    // The dialogue panel uses single-line, fixed-width bitmap text.
    for (const framework::DialogueNode& node : definition.nodes) {
        if (!supportsDialogueText(node.speaker, 39) ||
            !supportsDialogueText(node.text, 80) ||
            (!node.speakerCs.empty() && !supportsDialogueText(node.speakerCs, 39)) ||
            (!node.textCs.empty() && !supportsDialogueText(node.textCs, 80))) {
            return {framework::ErrorCode::InvalidValue, "dialogue.text",
                    "Dialogue speaker or line does not fit the game's bitmap font."};
        }
        for (const framework::DialogueChoice& choice : node.choices) {
            if (!supportsDialogueText(choice.label, 72) ||
                (!choice.labelCs.empty() && !supportsDialogueText(choice.labelCs, 72))) {
                return {framework::ErrorCode::InvalidValue, "dialogue.label",
                        "Dialogue choice does not fit the game's bitmap font."};
            }
        }
    }
    return framework::Status::success();
}

[[nodiscard]] framework::Status readContentFile(
    const char* path, std::size_t limit, FileBuffer& data,
    std::size_t& size) noexcept {
    using namespace framework;
    SDL_RWops* file = SDL_RWFromFile(path, "rb");
    if (file == nullptr) {
        return {ErrorCode::NotFound, path, "Required framework content could not be opened."};
    }
    const Sint64 fileSize = SDL_RWsize(file);
    if (fileSize <= 0 || static_cast<std::uint64_t>(fileSize) > limit) {
        SDL_RWclose(file);
        return {ErrorCode::CapacityExceeded, path,
                "Framework content is empty or exceeds its size limit."};
    }
    size = static_cast<std::size_t>(fileSize);
    data.reset(static_cast<char*>(SDL_malloc(size)));
    if (!data) {
        SDL_RWclose(file);
        return {ErrorCode::CapacityExceeded, path,
                "Framework content buffer allocation failed."};
    }
    std::size_t bytesRead = 0;
    while (bytesRead < size) {
        const std::size_t count = SDL_RWread(file, data.get() + bytesRead, 1, size - bytesRead);
        if (count == 0) {
            break;
        }
        bytesRead += count;
    }
    const int closeResult = SDL_RWclose(file);
    if (bytesRead != size || closeResult != 0) {
        return {ErrorCode::StorageFailure, path,
                "Framework content read or close failed."};
    }
    return Status::success();
}

} // namespace

const framework::ModuleInfo& framework_detail::StateModule::info() const noexcept {
    return kStateModuleInfo;
}

const framework::ModuleInfo& framework_detail::ProjectEchoModule::info() const noexcept {
    return kProjectModuleInfo;
}

void framework_detail::UnlockListener::onEvent(const AbilityUnlocked& event) noexcept {
    if (std::string_view(event.ability.data()) == "echo_step") {
        ++count_;
    }
}

FrameworkIntegration::~FrameworkIntegration() {
    shutdown();
}

framework::Status FrameworkIntegration::loadWorld(const char* contentRoot) noexcept {
    using framework::ErrorCode;
    if (contentRoot == nullptr) {
        return {ErrorCode::InvalidArgument, "echo.world",
                "The selected world module requires the asset directory."};
    }
    worldModule_.reset(new (std::nothrow) framework::WorldModule{});
    worldLayout_.reset(new (std::nothrow) WorldLayout{});
    if (!worldModule_ || !worldLayout_) {
        return {ErrorCode::CapacityExceeded, "echo.world", "World data allocation failed."};
    }
    // Reads one world JSON file relative to the asset root and hands its root to `apply`.
    // Failures report the relative file path, kept alive in a member buffer.
    const auto loadFile = [&](std::string_view relative, auto&& apply) -> framework::Status {
        const auto fail = [&](framework::Status failure) -> framework::Status {
            worldErrorSubject_ = {};
            const std::size_t length = std::min(relative.size(), worldErrorSubject_.size() - 1);
            std::copy_n(relative.data(), length, worldErrorSubject_.data());
            failure.subject = worldErrorSubject_.data();
            return failure;
        };
        std::array<char, 512> path{};
        const int written = std::snprintf(path.data(), path.size(), "%s%.*s", contentRoot,
                                          static_cast<int>(relative.size()), relative.data());
        if (written <= 0 || static_cast<std::size_t>(written) >= path.size()) {
            return fail({ErrorCode::CapacityExceeded, {}, "World content path is too long."});
        }
        FileBuffer data{};
        std::size_t size{};
        framework::Status status =
            readContentFile(path.data(), framework::kMaxWorldJsonBytes, data, size);
        if (!status) return fail(status);
        framework::JsonDocument document{};
        framework::JsonError error{};
        status = framework::JsonDocument::parse(data.get(), size, document, error);
        if (!status) return fail(status);
        status = apply(document.root());
        return status ? status : fail(status);
    };
    framework::Status status = loadFile("world/world.json", [&](framework::JsonValue root) {
        return worldModule_->loadManifest(root);
    });
    if (!status) return status;
    std::array<char, 160> relative{};
    for (std::size_t index = 0; index < worldModule_->manifestObjectCount(); ++index) {
        const std::string_view id = worldModule_->manifestObject(index);
        if (!framework::WorldModule::contentPath(id, relative.data(), relative.size())) {
            return {ErrorCode::InvalidValue, id, "World object ID has no content path."};
        }
        status = loadFile(relative.data(), [&](framework::JsonValue root) {
            return worldModule_->addObject(id, root);
        });
        if (!status) return status;
    }
    for (std::size_t index = 0; index < worldModule_->manifestRoomCount(); ++index) {
        const std::string_view id = worldModule_->manifestRoom(index);
        if (!framework::WorldModule::contentPath(id, relative.data(), relative.size())) {
            return {ErrorCode::InvalidValue, id, "World room ID has no content path."};
        }
        status = loadFile(relative.data(), [&](framework::JsonValue root) {
            return worldModule_->addRoom(id, root);
        });
        if (!status) return status;
    }
    status = worldModule_->finalize();
    if (!status) return status;
    const auto validateMaterialCircuit = [&](std::string_view circuit) noexcept -> framework::Status {
        if (circuit.empty()) return framework::Status::success();
        if (!powerModule_) {
            return {ErrorCode::MissingDependency, circuit,
                    "An emissive material circuit requires the echo.power module."};
        }
        if (!powerModule_->hasCircuit(circuit)) {
            return {ErrorCode::MissingDependency, circuit,
                    "An emissive material references an unknown power circuit."};
        }
        return framework::Status::success();
    };
    for (const framework::ObjectDefinition& object : worldModule_->objects()) {
        status = validateMaterialCircuit(object.sprite().material.emissiveCircuit.data());
        if (!status) return status;
    }
    for (const framework::RoomDefinition& room : worldModule_->rooms()) {
        for (const framework::TilesetSpec& tileset : room.tilesets()) {
            status = validateMaterialCircuit(tileset.material.emissiveCircuit.data());
            if (!status) return status;
        }
        for (const framework::WorldLightDefinition& light : room.lighting().lights) {
            if (light.id[0] == '\0' || light.circuit[0] == '\0') continue;
            if (!powerModule_) {
                return {ErrorCode::MissingDependency, light.circuit.data(),
                        "A world light circuit requires the echo.power module."};
            }
            if (!powerModule_->hasCircuit(light.circuit.data())) {
                return {ErrorCode::MissingDependency, light.circuit.data(),
                        "A world light references an unknown power circuit."};
            }
        }
    }
    status = buildWorldLayout(*worldModule_, *worldLayout_);
    if (!status) return status;
    worldLayout_->worldLighting = worldLighting_;
    for (const PropLayout& prop : worldLayout_->propList()) {
        const auto validateCircuit = [&](std::string_view circuit,
                                         std::string_view context) -> framework::Status {
            if (circuit.empty()) return framework::Status::success();
            if (!powerModule_) {
                return {ErrorCode::MissingDependency, circuit,
                        context};
            }
            if (!powerModule_->hasCircuit(circuit)) {
                return {ErrorCode::MissingDependency, circuit,
                        "A world interaction references an unknown circuit."};
            }
            return framework::Status::success();
        };
        status = validateCircuit(prop.circuit,
                                 "A power-panel interaction requires the echo.power module.");
        if (!status) return status;
        status = validateCircuit(prop.dialoguePowerCircuit,
                                 "A dialogue power-state binding requires echo.power.");
        if (!status) return status;
    }
    return framework::Status::success();
}

framework::Status FrameworkIntegration::initialize(const char* configPath,
                                                    const char* dialoguePath,
                                                    const char* itemCatalogPath,
                                                    const char* contentRoot) noexcept {
    if (initialized_) {
        return {framework::ErrorCode::InvalidState, "echo.project_echo",
                "Framework integration is already initialized."};
    }
    if (configPath == nullptr) {
        return {framework::ErrorCode::InvalidArgument, {},
                "Framework game configuration path is required."};
    }
    FileBuffer data{};
    std::size_t size{};
    framework::Status status = readContentFile(configPath, kMaxGameConfigBytes, data, size);
    if (!status) {
        return status;
    }
    framework::JsonDocument document{};
    framework::JsonError jsonError{};
    status = framework::JsonDocument::parse(data.get(), size, document, jsonError);
    data.reset();
    if (!status) {
        return status;
    }
    const framework::JsonValue root = document.root();
    std::string_view gameId;
    const framework::JsonValue moduleList = root.find("modules");
    if (!root.isObject() || !root.find("game").string(gameId) ||
        gameId != "echo.project_echo" || !moduleList.isArray() || moduleList.size() > 11) {
        return {framework::ErrorCode::InvalidValue, configPath,
                "Configuration requires game echo.project_echo and a bounded modules array."};
    }
    bool stateSelected = false;
    bool dialogueSelected = false;
    bool projectSelected = false;
    bool inventorySelected = false;
    bool attributesSelected = false;
    bool equipmentSelected = false;
    bool abilitiesSelected = false;
    bool questsSelected = false;
    bool cutscenesSelected = false;
    bool worldSelected = false;
    bool powerSelected = false;
    for (std::size_t index = 0; index < moduleList.size(); ++index) {
        std::string_view moduleId;
        if (!moduleList.at(index).string(moduleId)) {
            return {framework::ErrorCode::InvalidValue, "modules",
                    "Configured module IDs must be strings."};
        }
        if (moduleId != "echo.state" && moduleId != "echo.narrative.dialogue" &&
            moduleId != "echo.project_echo" && moduleId != "echo.rpg.inventory" &&
            moduleId != "echo.rpg.attributes" && moduleId != "echo.rpg.equipment" &&
            moduleId != "echo.rpg.abilities" && moduleId != "echo.rpg.quests" &&
            moduleId != "echo.narrative.cutscenes" && moduleId != "echo.world" &&
            moduleId != "echo.power") {
            return {framework::ErrorCode::MissingDependency, "modules",
                    "Configured module is not compiled into this application."};
        }
        stateSelected = stateSelected || moduleId == "echo.state";
        dialogueSelected = dialogueSelected || moduleId == "echo.narrative.dialogue";
        projectSelected = projectSelected || moduleId == "echo.project_echo";
        inventorySelected = inventorySelected || moduleId == "echo.rpg.inventory";
        attributesSelected = attributesSelected || moduleId == "echo.rpg.attributes";
        equipmentSelected = equipmentSelected || moduleId == "echo.rpg.equipment";
        abilitiesSelected = abilitiesSelected || moduleId == "echo.rpg.abilities";
        questsSelected = questsSelected || moduleId == "echo.rpg.quests";
        cutscenesSelected = cutscenesSelected ||
                            moduleId == "echo.narrative.cutscenes";
        worldSelected = worldSelected || moduleId == "echo.world";
        powerSelected = powerSelected || moduleId == "echo.power";
        for (std::size_t previous = 0; previous < index; ++previous) {
            std::string_view previousId;
            (void)moduleList.at(previous).string(previousId);
            if (previousId == moduleId) {
                return {framework::ErrorCode::DuplicateId, "modules",
                        "A module is selected more than once in game configuration."};
            }
        }
    }
    if (!stateSelected || !dialogueSelected || !projectSelected) {
        return {framework::ErrorCode::MissingDependency, "echo.project_echo",
                "The game configuration must select state, dialogue, and PROJECT ECHO modules."};
    }
    status = framework::WorldModule::parseWorldLighting(
        root.find("worldLighting"), worldLighting_);
    if (!status) return status;
    if (!worldSelected && root.find("worldLighting").valid()) {
        return {framework::ErrorCode::MissingDependency, "echo.world",
                "World lighting content requires the echo.world module."};
    }
    if (powerSelected) {
        powerModule_.reset(new (std::nothrow) framework::PowerModule{});
        if (!powerModule_) {
            return {framework::ErrorCode::CapacityExceeded, "echo.power",
                    "Power module allocation failed."};
        }
        status = powerModule_->load(root.find("power"));
        if (!status) return status;
    } else {
        if (root.find("power").valid()) {
            return {framework::ErrorCode::MissingDependency, "echo.power",
                    "Power content requires the echo.power module to be selected."};
        }
        powerModule_.reset();
    }
    if (worldSelected) {
        status = loadWorld(contentRoot);
        if (!status) {
            return status;
        }
    } else {
        worldLayout_.reset();
        worldModule_.reset();
    }
    if (questsSelected) {
        status = questModule_.load(root.find("quests"));
        if (!status) return status;
        status = validateProjectQuest(questModule_);
        if (!status) return status;
        for (const framework::QuestDefinition& quest : questModule_.definitions()) {
            for (const framework::QuestObjective& objective : quest.objectives()) {
                if (!supportsBitmapText(objective.text.data(), 48)) {
                    return {framework::ErrorCode::InvalidValue, "quests.objective.text",
                            "Quest objective does not fit the game's bitmap font."};
                }
            }
            if (!quest.branchPrompt().empty() &&
                !supportsBitmapText(quest.branchPrompt(), 48)) {
                return {framework::ErrorCode::InvalidValue, "quests.branch.prompt",
                        "Quest branch prompt does not fit the game's bitmap font."};
            }
            for (const framework::QuestBranch& branch : quest.branches()) {
                if (!supportsBitmapText(branch.label.data(), 48) ||
                    !supportsBitmapText(branch.objective.data(), 48)) {
                    return {framework::ErrorCode::InvalidValue, "quests.branch.text",
                            "Quest branch text does not fit the game's bitmap font."};
                }
            }
        }
    }
    if (cutscenesSelected) {
        status = cutsceneModule_.load(root.find("cutscenes"));
        if (!status) return status;
        const framework::Status projectStatus =
            validateProjectCutscenes(cutsceneModule_);
        if (!projectStatus) return projectStatus;
        for (const framework::CutsceneDefinition& scene : cutsceneModule_.definitions()) {
            for (const framework::CutsceneCard& card : scene.cards()) {
                if (!supportsBitmapText(card.speaker.data(), 32) ||
                    !supportsBitmapText(card.text.data(), 72) ||
                    !supportsBitmapText(card.speakerCs.data(), 32) ||
                    !supportsBitmapText(card.textCs.data(), 72)) {
                    return {framework::ErrorCode::InvalidValue, "cutscenes.card",
                            "Cutscene text does not fit the localized bitmap font layout."};
                }
            }
        }
    }
    if (inventorySelected) {
        if (itemCatalogPath == nullptr) {
            return {framework::ErrorCode::InvalidArgument, "echo.rpg.inventory",
                    "The selected inventory module requires an item catalog path."};
        }
        if (!inventoryModule_) {
            inventoryModule_.reset(new (std::nothrow) framework::InventoryModule{});
            if (!inventoryModule_) {
                return {framework::ErrorCode::CapacityExceeded, "echo.rpg.inventory",
                        "Inventory module allocation failed."};
            }
        }
        status = readContentFile(itemCatalogPath, kMaxGameConfigBytes, data, size);
        if (!status) return status;
        framework::JsonDocument itemDocument{};
        status = framework::JsonDocument::parse(data.get(), size, itemDocument, jsonError);
        data.reset();
        if (!status) return status;
        status = inventoryModule_->load(itemDocument.root());
        if (!status) return status;
        lootTable_.reset(new (std::nothrow) EnemyLootTable{});
        if (!lootTable_) {
            return {framework::ErrorCode::CapacityExceeded, "enemy_loot",
                    "Enemy loot table allocation failed."};
        }
        status = lootTable_->load(root.find("loot"), inventoryModule_->catalog());
        if (!status) return status;
    } else if (root.find("loot").valid()) {
        return {framework::ErrorCode::MissingDependency, "enemy_loot",
                "Enemy loot content requires the echo.rpg.inventory module."};
    }
    if (attributesSelected) {
        status = attributesModule_.load(root.find("attributes"));
        if (!status) return status;
    } else if (root.find("attributes").valid()) {
        return {framework::ErrorCode::MissingDependency, "echo.rpg.attributes",
                "Attribute content requires the echo.rpg.attributes module."};
    }
    if (abilitiesSelected) {
        if (!abilitiesModule_) {
            abilitiesModule_.reset(new (std::nothrow) framework::AbilityModule{});
            if (!abilitiesModule_) {
                return {framework::ErrorCode::CapacityExceeded, "echo.rpg.abilities",
                        "Abilities module allocation failed."};
            }
        }
        status = abilitiesModule_->load(root.find("abilities"));
        if (!status) return status;
    } else if (root.find("abilities").valid()) {
        return {framework::ErrorCode::MissingDependency, "echo.rpg.abilities",
                "Ability content requires the echo.rpg.abilities module."};
    }
    if (equipmentSelected) {
        if (!equipmentModule_) {
            equipmentModule_.reset(new (std::nothrow) framework::EquipmentModule{});
            if (!equipmentModule_) {
                return {framework::ErrorCode::CapacityExceeded, "echo.rpg.equipment",
                        "Equipment module allocation failed."};
            }
        }
        status = equipmentModule_->load(root.find("equipment"));
        if (!status) return status;
    }
    framework::ContentRegistry content{};
    status = framework::loadResourceManifest(root, content);
    if (!status) {
        return {status.code, "resources", status.detail};
    }
    if (worldLayout_) {
        for (const PropLayout& prop : worldLayout_->propList()) {
            if (prop.dialogue.empty()) continue;
            framework::ResourceInfo propDialogue{};
            status = content.find(prop.dialogue, propDialogue);
            if (!status || propDialogue.type != framework::ResourceType::Dialogue ||
                propDialogue.optional) {
                return {framework::ErrorCode::MissingDependency, prop.dialogue,
                        "A dialogue prop requires a registered non-optional dialogue resource."};
            }
        }
    }
    if (cutscenesSelected) {
        framework::ResourceInfo cutsceneResource{};
        status = content.find("echo:cutscene/central_signal", cutsceneResource);
        if (!status || cutsceneResource.type != framework::ResourceType::Cutscene ||
            cutsceneResource.optional) {
            return {framework::ErrorCode::MissingDependency,
                    "echo:cutscene/central_signal",
                    "The Central Chamber cutscene must be a required cutscene resource."};
        }
    }
    framework::ResourceInfo dialogueResource{};
    status = content.find("echo:dialogue/engineer", dialogueResource);
    if (!status || dialogueResource.type != framework::ResourceType::Dialogue ||
        dialogueResource.optional) {
        return {framework::ErrorCode::MissingDependency, "echo:dialogue/engineer",
                "The engineer dialogue must be a required dialogue resource."};
    }
    framework::ResourceInfo keyResource{};
    status = content.find("echo:item/maintenance_key", keyResource);
    if (!status || keyResource.type != framework::ResourceType::Item || keyResource.optional) {
        return {framework::ErrorCode::MissingDependency, "echo:item/maintenance_key",
                "The maintenance key must be a required item resource."};
    }
    if (inventorySelected) {
        for (std::size_t index = 0; index < inventoryModule_->catalog().size(); ++index) {
            const framework::ItemDefinition* item = inventoryModule_->catalog().at(index);
            if (item->key == 0 ||
                item->key >= static_cast<std::uint32_t>(echo::ItemId::Count)) {
                return {framework::ErrorCode::InvalidValue, item->id.data(),
                        "Item catalog keys must retain the stable gameplay save range."};
            }
            framework::ResourceInfo resource{};
            status = content.find(item->id.data(), resource);
            if (!status || resource.type != framework::ResourceType::Item ||
                resource.optional) {
                return {framework::ErrorCode::MissingDependency, item->id.data(),
                        "Each item catalog entry requires a matching non-optional item resource."};
            }
        }
        for (unsigned key = 1; key < static_cast<unsigned>(echo::ItemId::Count); ++key) {
            if (inventoryModule_->catalog().find(key) == nullptr) {
                return {framework::ErrorCode::MissingDependency, "echo.rpg.inventory",
                        "Item catalog must define every stable gameplay save key."};
            }
        }
    }

    const framework::JsonValue technician = root.find("technician");
    if (!technician.isObject()) {
        return {framework::ErrorCode::InvalidValue, "technician",
                "Game configuration requires the engineer's component settings."};
    }
    if (technician.find("transform").valid()) {
        return {framework::ErrorCode::InvalidValue, "technician.transform",
                "The engineer's position belongs to echo:object/engineer spawn data."};
    }
    framework::ComponentTypeRegistry componentTypes{};
    status = framework::registerCoreComponentTypes(componentTypes);
    if (!status) {
        return status;
    }
    status = registerProjectComponentTypes(componentTypes);
    if (!status) {
        return status;
    }
    framework::ComponentInstance component{};
    DialogueInteractionComponent interaction{};
    const framework::JsonValue interactionConfig = technician.find("interaction");
    if (interactionConfig.valid()) {
        status = componentTypes.instantiate(
            kDialogueInteractionComponentId, interactionConfig, component, &content);
        if (!status) {
            return {status.code, "technician.interaction", status.detail};
        }
        if (!component.read(interaction)) {
            return {framework::ErrorCode::InvalidState, "technician.interaction",
                    "The engineer interaction component has an invalid representation."};
        }
    }

    std::size_t requiredDialogueCount = 0;
    for (std::size_t index = 0; index < content.size(); ++index) {
        framework::ResourceInfo resource{};
        if (content.at(index, resource) &&
            resource.type == framework::ResourceType::Dialogue && !resource.optional) {
            ++requiredDialogueCount;
        }
    }
    if (requiredDialogueCount > dialogueDocuments_.size()) {
        return {framework::ErrorCode::CapacityExceeded, "resources",
                "Required dialogue resources exceed the runtime capacity."};
    }
    for (std::size_t index = 0; index < content.size(); ++index) {
        framework::ResourceInfo resource{};
        if (!content.at(index, resource) ||
            resource.type != framework::ResourceType::Dialogue || resource.optional) {
            continue;
        }
        std::array<char, 512> resolvedPath{};
        const char* dialogueSource = nullptr;
        if (!resource.source.empty()) {
            if (contentRoot == nullptr) {
                return {framework::ErrorCode::InvalidArgument, resource.id,
                        "A dialogue resource source requires the asset root."};
            }
            const int pathLength = std::snprintf(
                resolvedPath.data(), resolvedPath.size(), "%s%.*s", contentRoot,
                static_cast<int>(resource.source.size()), resource.source.data());
            if (pathLength <= 0 ||
                static_cast<std::size_t>(pathLength) >= resolvedPath.size()) {
                return {framework::ErrorCode::CapacityExceeded, resource.id,
                        "Dialogue resource source path is too long."};
            }
            dialogueSource = resolvedPath.data();
        } else if (requiredDialogueCount == 1 && dialoguePath != nullptr) {
            dialogueSource = dialoguePath;
        } else {
            return {framework::ErrorCode::MissingDependency, resource.id,
                    "Required dialogue resource has no source path."};
        }
        status = readContentFile(dialogueSource, kMaxDialogueBytes, data, size);
        if (!status) return status;
        std::unique_ptr<framework::DialogueDocument> dialogue(
            new (std::nothrow) framework::DialogueDocument{});
        if (!dialogue) {
            return {framework::ErrorCode::CapacityExceeded, resource.id,
                    "Dialogue document allocation failed."};
        }
        status = dialogue->parse(data.get(), size, jsonError);
        data.reset();
        if (!status) return status;
        if (dialogue->definition()->id != resource.id) {
            return {framework::ErrorCode::InvalidValue, resource.id,
                    "Dialogue source does not match its declared resource ID."};
        }
        status = validateDialogueDisplay(*dialogue->definition());
        if (!status) return status;
        status = dialogueModule_.add(*dialogue->definition());
        if (!status) return status;
        dialogueDocuments_[dialogueDocumentCount_++] = std::move(dialogue);
    }
    technicianInteractionReach_ = interaction.horizontalReach;

    if (!equipmentModule_) {
        equipmentModule_.reset(new (std::nothrow) framework::EquipmentModule{});
        if (!equipmentModule_) {
            return {framework::ErrorCode::CapacityExceeded, "echo.rpg.equipment",
                    "Equipment module allocation failed."};
        }
    }
    status = modules_.add(stateModule_, stateSelected);
    if (!status) {
        return status;
    }
    status = modules_.add(dialogueModule_, dialogueSelected);
    if (!status) {
        return status;
    }
    status = modules_.add(projectModule_, projectSelected);
    if (!status) {
        return status;
    }
    if (inventoryModule_) {
        status = modules_.add(*inventoryModule_, inventorySelected);
        if (!status) {
            return status;
        }
    }
    status = modules_.add(attributesModule_, attributesSelected);
    if (!status) {
        return status;
    }
    status = modules_.add(*equipmentModule_, equipmentSelected);
    if (!status) {
        return status;
    }
    if (abilitiesModule_) {
        status = modules_.add(*abilitiesModule_, abilitiesSelected);
        if (!status) {
            return status;
        }
    }
    status = modules_.add(questModule_, questsSelected);
    if (!status) {
        return status;
    }
    status = modules_.add(cutsceneModule_, cutscenesSelected);
    if (!status) {
        return status;
    }
    if (worldModule_) {
        status = modules_.add(*worldModule_, true);
        if (!status) {
            return status;
        }
    }
    if (powerModule_) {
        status = modules_.add(*powerModule_, true);
        if (!status) return status;
    }
    framework::ModuleContext context{this};
    status = modules_.initializeAll(context);
    if (!status) {
        modules_.shutdownAll();
        return status;
    }
    status = actions_.addCapability("echo.state.write");
    if (!status) {
        modules_.shutdownAll();
        return status;
    }
    status = actions_.registerAction(
        "echo.state.set_flag", "echo.state.write", &framework::ActionRegistry::setStateFlag,
        &stateModule_.state, true);
    if (!status) {
        modules_.shutdownAll();
        return status;
    }
    if (powerModule_) {
        status = powerModule_->registerActions(actions_);
        if (!status) {
            modules_.shutdownAll();
            return status;
        }
    }
    status = events_.subscribe(unlockListener_, unlockSubscription_);
    if (!status) {
        modules_.shutdownAll();
        return status;
    }
    initialized_ = true;
    return framework::Status::success();
}

framework::Status FrameworkIntegration::configureGame(Game& game) noexcept {
    if (!initialized_) {
        return {framework::ErrorCode::InvalidState, "echo.project_echo",
                "Framework integration must initialize before configuring the game."};
    }
    framework::Status status = framework::Status::success();
    if (worldLayout_) {
        status = game.configureWorld(*worldLayout_);
        if (!status) return status;
    }
    if (powerModule_ != nullptr && questModule_.find(kCargoQuestId) != nullptr &&
        worldLayout_) {
        status = game.configurePower(*powerModule_);
        if (!status) return status;
    }
    status = game.configureDialogue(
        dialogueModule_, stateModule_.state, technicianInteractionReach_,
        inventoryModule_ && inventoryModule_->initialized() ? inventoryModule_.get() : nullptr);
    if (!status) return status;
    if (lootTable_) {
        status = game.configureLoot(*lootTable_);
        if (!status) return status;
    }
    if (abilitiesModule_ && abilitiesModule_->initialized()) {
        status = game.configureAbilities(*abilitiesModule_);
        if (!status) return status;
    }
    if (questModule_.initialized()) {
        status = game.configureQuests(questModule_);
        if (!status) return status;
    }
    if (cutsceneModule_.initialized()) {
        status = game.configureCutscenes(cutsceneModule_);
        if (!status) return status;
    }
    if (attributesModule_.initialized()) {
        status = game.configureAttributes(attributesModule_);
        if (!status) return status;
        if (equipmentModule_ && equipmentModule_->initialized()) {
            return game.configureEquipment(*equipmentModule_);
        }
        return framework::Status::success();
    }
    return framework::Status::success();
}

framework::Status FrameworkIntegration::beginGame() noexcept {
    if (!initialized_) {
        return {framework::ErrorCode::InvalidState, "echo.project_echo",
                "Framework integration must initialize before starting a game."};
    }
    framework::GameState empty{};
    const framework::Status cleared = stateModule_.state.replaceValues(empty);
    if (!cleared) {
        return cleared;
    }
    if (powerModule_) {
        const framework::Status restored = powerModule_->restore();
        if (!restored) return restored;
    }
    actions_.setGameActive(true);
    return stateModule_.state.set(
        {framework::StateScope::Global, {}, "player.echo_step_unlocked"},
        framework::StateValue::fromBoolean(false));
}

framework::Status FrameworkIntegration::resumeGame(const framework::GameState& state) noexcept {
    if (!initialized_) {
        return {framework::ErrorCode::InvalidState, "echo.project_echo",
                "Framework integration must initialize before resuming a game."};
    }
    const framework::Status replaced = stateModule_.state.replaceValues(state);
    if (!replaced) {
        return replaced;
    }
    if (powerModule_) {
        const framework::Status restored = powerModule_->restoreState(state);
        if (!restored) return restored;
    }
    actions_.setGameActive(true);
    return framework::Status::success();
}

framework::Status FrameworkIntegration::togglePowerCircuit(
    std::string_view circuitId) noexcept {
    if (!initialized_ || !powerModule_) {
        return {framework::ErrorCode::MissingDependency, "echo.power",
                "Power controls are not initialized."};
    }
    if (!powerModule_->hasCircuit(circuitId)) {
        return {framework::ErrorCode::NotFound, circuitId,
                "The requested power circuit is not configured."};
    }
    framework::StateValue circuit{};
    if (!framework::StateValue::fromString(circuitId, circuit)) {
        return {framework::ErrorCode::InvalidValue, circuitId,
                "The power circuit ID exceeds the action value limit."};
    }
    const std::array<framework::ActionParameter, 1> parameters{{
        {"circuit", circuit},
    }};
    const framework::ActionResult result =
        actions_.execute({"echo.power.toggle", parameters});
    return result.status;
}

framework::Status FrameworkIntegration::notifyEchoStepUnlocked() noexcept {
    if (!initialized_) {
        return {framework::ErrorCode::InvalidState, "echo.project_echo",
                "Framework integration is not initialized."};
    }
    constexpr framework::StateAddress address{
        framework::StateScope::Global, {}, "player.echo_step_unlocked"};
    framework::StateValue current{};
    if (stateModule_.state.get(address, current) &&
        current.type == framework::ValueType::Boolean && current.boolean) {
        return framework::Status::success();
    }

    framework::StateValue key{};
    if (!framework::StateValue::fromString(address.key, key)) {
        return {framework::ErrorCode::InvalidValue, address.key,
                "Ability state key does not fit the core value limit."};
    }
    const std::array<framework::ActionParameter, 2> parameters{{
        {"key", key},
        {"value", framework::StateValue::fromBoolean(true)},
    }};
    const framework::ActionResult action =
        actions_.execute({"echo.state.set_flag", parameters});
    if (!action.status) {
        return action.status;
    }

    const framework::Condition condition = framework::Condition::compare(
        address, framework::CompareOperator::Equal,
        framework::StateValue::fromBoolean(true));
    const framework::ConditionResult evaluated =
        framework::evaluate(condition, stateModule_.state);
    if (!evaluated.status) {
        return evaluated.status;
    }
    if (!evaluated.value) {
        return {framework::ErrorCode::InvalidState, address.key,
                "Ability state condition did not match after the registered action."};
    }

    framework_detail::AbilityUnlocked event{};
    constexpr std::string_view ability = "echo_step";
    for (std::size_t index = 0; index < ability.size(); ++index) {
        event.ability[index] = ability[index];
    }
    return events_.publish(event);
}

void FrameworkIntegration::shutdown() noexcept {
    if (unlockSubscription_.valid()) {
        (void)events_.unsubscribe(unlockSubscription_);
        unlockSubscription_ = {};
    }
    actions_.setGameActive(false);
    modules_.shutdownAll();
    for (std::size_t index = dialogueDocumentCount_; index > 0; --index) {
        dialogueDocuments_[index - 1].reset();
    }
    dialogueDocumentCount_ = 0;
    lootTable_.reset();
    equipmentModule_.reset();
    initialized_ = false;
}

framework::Status FrameworkIntegration::setWorldLightEnabled(
    std::string_view roomId, std::string_view lightId, bool enabled) noexcept {
    if (!initialized_ || worldModule_ == nullptr) {
        return {framework::ErrorCode::InvalidState, "echo.world",
                "World lighting controls are not initialized."};
    }
    const framework::RoomDefinition* room = worldModule_->findRoom(roomId);
    if (room == nullptr) {
        return {framework::ErrorCode::NotFound, roomId,
                "The requested lighting room is not configured."};
    }
    const framework::WorldLightDefinition* selected = nullptr;
    for (const framework::WorldLightDefinition& light : room->lighting().lights) {
        if (light.id[0] != '\0' && std::string_view(light.id.data()) == lightId) {
            selected = &light;
            break;
        }
    }
    if (selected == nullptr) {
        return {framework::ErrorCode::NotFound, lightId,
                "The requested world light is not configured in that room."};
    }
    if (selected->persistence == framework::LightPersistence::Transient) {
        return {framework::ErrorCode::InvalidState, lightId,
                "Transient lights cannot be stored as persistent light overrides."};
    }
    std::array<char, 64> key{};
    if (!framework::worldLightStateKey(roomId, lightId, key.data(), key.size())) {
        return {framework::ErrorCode::InvalidValue, lightId,
                "The requested world light cannot be represented as a state key."};
    }
    const framework::StateScope scope =
        selected->persistence == framework::LightPersistence::Persistent
            ? framework::StateScope::Global : framework::StateScope::World;
    return stateModule_.state.set({scope, {}, key.data()},
                                  framework::StateValue::fromBoolean(enabled));
}

framework::Status FrameworkIntegration::saveWorldLightOverrides(
    framework::GameState& state) const noexcept {
    constexpr std::string_view kLightStatePrefix{"echo.world_light."};
    for (std::size_t index = 0; index < stateModule_.state.size(); ++index) {
        framework::StateEntryView entry{};
        if (!stateModule_.state.entryAt(index, entry)) {
            return {framework::ErrorCode::InvalidState, "echo.world_light",
                    "Could not enumerate persistent world-light state."};
        }
        if (!entry.address.key.starts_with(kLightStatePrefix)) continue;
        if (entry.value.type != framework::ValueType::Boolean) {
            return {framework::ErrorCode::InvalidValue, entry.address.key,
                    "Persistent world-light overrides must be boolean."};
        }
        const framework::Status status = state.set(entry.address, entry.value);
        if (!status) return status;
    }
    return framework::Status::success();
}

std::size_t FrameworkIntegration::unlockEventCount() const noexcept {
    return unlockListener_.count();
}

} // namespace echo::platform
