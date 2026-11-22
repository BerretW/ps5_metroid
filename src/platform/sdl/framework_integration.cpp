#include "platform/sdl/framework_integration.hpp"

#include "echo/framework/data.hpp"
#include "echo/framework/schema.hpp"
#include "game/game.hpp"
#include "platform/sdl/bitmap_font.hpp"

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
constexpr std::array<std::string_view, 7> kProjectOptionalModules{
    "echo.narrative.cutscenes", "echo.rpg.abilities",
    "echo.rpg.attributes", "echo.rpg.equipment",
    "echo.rpg.inventory", "echo.rpg.quests", "echo.world"};
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
    return buildWorldLayout(*worldModule_, *worldLayout_);
}

framework::Status FrameworkIntegration::initialize(const char* configPath,
                                                    const char* dialoguePath,
                                                    const char* itemCatalogPath,
                                                    const char* contentRoot) noexcept {
    if (initialized_) {
        return {framework::ErrorCode::InvalidState, "echo.project_echo",
                "Framework integration is already initialized."};
    }
    if (configPath == nullptr || dialoguePath == nullptr) {
        return {framework::ErrorCode::InvalidArgument, {},
                "Framework game configuration and dialogue paths are required."};
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
        gameId != "echo.project_echo" || !moduleList.isArray() || moduleList.size() > 10) {
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
            moduleId != "echo.narrative.cutscenes" && moduleId != "echo.world") {
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
        status = readContentFile(itemCatalogPath, kMaxGameConfigBytes, data, size);
        if (!status) return status;
        framework::JsonDocument itemDocument{};
        status = framework::JsonDocument::parse(data.get(), size, itemDocument, jsonError);
        data.reset();
        if (!status) return status;
        status = inventoryModule_.load(itemDocument.root());
        if (!status) return status;
    }
    framework::ContentRegistry content{};
    status = framework::loadResourceManifest(root, content);
    if (!status) {
        return {status.code, "resources", status.detail};
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
        for (unsigned id = static_cast<unsigned>(echo::ItemId::JumpVisor);
             id < static_cast<unsigned>(echo::ItemId::Count); ++id) {
            const auto itemId = static_cast<echo::ItemId>(id);
            const std::string_view itemResource = Game::itemResource(itemId);
            framework::ResourceInfo resource{};
            status = content.find(itemResource, resource);
            const framework::ItemDefinition* definition =
                inventoryModule_.catalog().find(itemResource);
            if (!status || resource.type != framework::ResourceType::Item || resource.optional ||
                definition == nullptr || definition->key != id ||
                definition->maxStack != Game::itemStackLimit(itemId)) {
                return {framework::ErrorCode::MissingDependency, itemResource,
                        "A required gameplay item or its matching catalog entry is missing."};
            }
        }
        const framework::ItemDefinition* key =
            inventoryModule_.catalog().find(keyResource.id);
        if (key == nullptr ||
            key->key != static_cast<std::uint32_t>(echo::ItemId::MaintenanceKey)) {
            return {framework::ErrorCode::MissingDependency, keyResource.id,
                    "The required maintenance key is missing from the selected item catalog."};
        }
        for (std::size_t index = 0; index < inventoryModule_.catalog().size(); ++index) {
            const framework::ItemDefinition* item = inventoryModule_.catalog().at(index);
            framework::ResourceInfo resource{};
            status = content.find(item->id.data(), resource);
            if (!status || resource.type != framework::ResourceType::Item ||
                resource.optional) {
                return {framework::ErrorCode::MissingDependency, item->id.data(),
                        "Each item catalog entry requires a matching non-optional item resource."};
            }
        }
    }

    const framework::JsonValue technician = root.find("technician");
    if (!technician.isObject()) {
        return {framework::ErrorCode::InvalidValue, "technician",
                "Game configuration requires the engineer's component settings."};
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
    status = componentTypes.instantiate(
        "echo:component/transform2d", technician.find("transform"), component, &content);
    if (!status) {
        return {status.code, "technician.transform", status.detail};
    }
    framework::Transform2DComponent transform{};
    if (!component.read(transform) || !transform.visible || transform.scaleX != 1.0F ||
        transform.scaleY != 1.0F || transform.rotationDegrees != 0.0F) {
        return {framework::ErrorCode::InvalidValue, "technician.transform",
                "The engineer requires a visible, unscaled, unrotated transform."};
    }
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

    status = readContentFile(dialoguePath, kMaxDialogueBytes, data, size);
    if (!status) {
        return status;
    }
    std::unique_ptr<framework::DialogueDocument> dialogue(
        new (std::nothrow) framework::DialogueDocument{});
    if (!dialogue) {
        return {framework::ErrorCode::CapacityExceeded, "dialogue",
                "Dialogue document allocation failed."};
    }
    status = dialogue->parse(data.get(), size, jsonError);
    data.reset();
    if (!status) {
        return status;
    }
    if (dialogue->definition()->id != dialogueResource.id) {
        return {framework::ErrorCode::InvalidValue, "dialogue",
                "The dialogue file does not match the engineer resource ID."};
    }
    status = validateDialogueDisplay(*dialogue->definition());
    if (!status) {
        return status;
    }
    status = dialogueModule_.add(*dialogue->definition());
    if (!status) {
        return status;
    }
    dialogueDocument_ = std::move(dialogue);
    technicianPosition_ = {transform.x, transform.y};
    technicianInteractionReach_ = interaction.horizontalReach;

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
    status = modules_.add(inventoryModule_, inventorySelected);
    if (!status) {
        return status;
    }
    status = modules_.add(attributesModule_, attributesSelected);
    if (!status) {
        return status;
    }
    status = modules_.add(equipmentModule_, equipmentSelected);
    if (!status) {
        return status;
    }
    status = modules_.add(abilitiesModule_, abilitiesSelected);
    if (!status) {
        return status;
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
    status = game.configureDialogue(
        dialogueModule_, stateModule_.state,
        {technicianPosition_[0], technicianPosition_[1]}, technicianInteractionReach_,
        inventoryModule_.initialized() ? &inventoryModule_ : nullptr);
    if (!status) return status;
    if (abilitiesModule_.initialized()) {
        status = game.configureAbilities(abilitiesModule_);
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
        if (equipmentModule_.initialized()) {
            return game.configureEquipment(equipmentModule_);
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
    actions_.setGameActive(true);
    return framework::Status::success();
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
    initialized_ = false;
}

std::size_t FrameworkIntegration::unlockEventCount() const noexcept {
    return unlockListener_.count();
}

} // namespace echo::platform
