#include "echo/framework/narrative.hpp"
#include "echo/framework/capability_manifest.hpp"

#include <algorithm>
#include <limits>

namespace echo::framework {
namespace {

constexpr std::array<std::string_view, 1> kDialogueServices{
    "echo.service.dialogue",
};
constexpr std::array<ManifestModuleDescriptor, 1> kNarrativeModuleManifest{{
    {"echo.narrative.dialogue", "Dialogue", 1, {}, {},
     kDialogueServices, {}},
}};
constexpr std::array<ManifestSchemaDescriptor, 1> kNarrativeSchemaManifest{{
    {"echo.narrative.dialogue_json", 1},
}};
constexpr ModuleInfo kDialogueModuleInfo{
    "echo.narrative.dialogue",
    "Dialogue",
    1,
    {},
    {},
    kDialogueServices,
    {},
};
constexpr std::array<std::string_view, 5> kDialogueFields{
    "schemaVersion", "id", "version", "startNode", "nodes"};
constexpr std::array<std::string_view, 7> kNodeFields{
    "id", "speaker", "text", "nextNode", "choices", "speakerCs", "textCs"};
constexpr std::array<std::string_view, 6> kChoiceFields{
    "id", "label", "targetNode", "visibleWhen", "onSelect", "labelCs"};
constexpr std::array<std::string_view, 2> kActionFields{"id", "parameters"};
constexpr std::array<std::string_view, 2> kParameterFields{"name", "value"};
constexpr std::array<std::string_view, 2> kLogicalFields{"op", "conditions"};
constexpr std::array<std::string_view, 2> kNotFields{"op", "condition"};
constexpr std::array<std::string_view, 4> kExistsFields{"op", "scope", "key", "entity"};
constexpr std::array<std::string_view, 6> kCompareFields{
    "op", "scope", "key", "entity", "comparison", "value"};
constexpr std::array<std::string_view, 2> kEntityFields{"index", "generation"};

[[nodiscard]] std::string_view localizedText(std::string_view english,
                                             std::string_view czech,
                                             DialogueLanguage language) noexcept {
    return language == DialogueLanguage::Czech && !czech.empty() ? czech : english;
}

template <std::size_t Count>
[[nodiscard]] Status validateFields(JsonValue object,
                                    const std::array<std::string_view, Count>& allowed,
                                    std::string_view subject) noexcept {
    if (!object.isObject() || object.size() > allowed.size()) {
        return {ErrorCode::InvalidValue, subject,
                "Dialogue object has an invalid shape or too many fields."};
    }
    std::array<std::string_view, Count> seen{};
    for (std::size_t index = 0; index < object.size(); ++index) {
        std::string_view name;
        JsonValue value{};
        if (!object.memberAt(index, name, value) ||
            std::find(allowed.begin(), allowed.end(), name) == allowed.end()) {
            return {ErrorCode::InvalidValue, subject,
                    "Dialogue object contains an unknown field."};
        }
        for (std::size_t previous = 0; previous < index; ++previous) {
            if (seen[previous] == name) {
                return {ErrorCode::DuplicateId, subject,
                        "Dialogue object contains a duplicate field."};
            }
        }
        seen[index] = name;
    }
    return Status::success();
}

[[nodiscard]] Status validateConditionFields(JsonValue condition,
                                             std::size_t depth = 0) noexcept {
    if (depth >= 32) {
        return {ErrorCode::InvalidValue, "visibleWhen",
                "Condition nesting exceeds 32 levels."};
    }
    std::string_view operation;
    if (!condition.find("op").string(operation)) {
        return {ErrorCode::InvalidValue, "visibleWhen",
                "Condition requires a string op field."};
    }
    Status fields{};
    if (operation == "and" || operation == "or") {
        fields = validateFields(condition, kLogicalFields, "visibleWhen");
    } else if (operation == "not") {
        fields = validateFields(condition, kNotFields, "visibleWhen");
    } else if (operation == "exists") {
        fields = validateFields(condition, kExistsFields, "visibleWhen");
    } else if (operation == "compare") {
        fields = validateFields(condition, kCompareFields, "visibleWhen");
    } else {
        return {ErrorCode::InvalidValue, "visibleWhen",
                "Condition operation is not supported."};
    }
    if (!fields) {
        return fields;
    }
    if (operation == "and" || operation == "or") {
        const JsonValue children = condition.find("conditions");
        for (std::size_t index = 0; index < children.size(); ++index) {
            const Status status = validateConditionFields(children.at(index), depth + 1);
            if (!status) return status;
        }
    } else if (operation == "not") {
        return validateConditionFields(condition.find("condition"), depth + 1);
    } else {
        const JsonValue entity = condition.find("entity");
        if (entity.valid()) {
            return validateFields(entity, kEntityFields, "visibleWhen.entity");
        }
    }
    return Status::success();
}

[[nodiscard]] bool validLocalId(std::string_view id) noexcept {
    if (id.empty() || id.size() > 63) {
        return false;
    }
    for (const char character : id) {
        if (!((character >= 'a' && character <= 'z') ||
              (character >= 'A' && character <= 'Z') ||
              (character >= '0' && character <= '9') ||
              character == '_' || character == '-')) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool validActionParameterName(std::string_view name) noexcept {
    if (name.empty() || name.size() > 63) {
        return false;
    }
    for (const char character : name) {
        if (!((character >= 'a' && character <= 'z') ||
              (character >= 'A' && character <= 'Z') ||
              (character >= '0' && character <= '9') || character == '_')) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::size_t findNodeIndex(const DialogueDefinition& definition,
                                        std::string_view id) noexcept {
    for (std::size_t index = 0; index < definition.nodes.size(); ++index) {
        if (definition.nodes[index].id == id) {
            return index;
        }
    }
    return definition.nodes.size();
}

[[nodiscard]] bool parseStateValue(JsonValue json, StateValue& output) noexcept {
    if (json.isBoolean()) {
        bool value{};
        (void)json.boolean(value);
        output = StateValue::fromBoolean(value);
        return true;
    }
    if (json.isInteger()) {
        std::int64_t value{};
        (void)json.integer(value);
        output = StateValue::fromInteger(value);
        return true;
    }
    if (json.isNumber()) {
        double value{};
        return json.number(value) && (output = StateValue::fromNumber(value)).valid();
    }
    if (json.isString()) {
        std::string_view value;
        return json.string(value) && StateValue::fromString(value, output);
    }
    return false;
}

} // namespace

Status DialogueDocument::parse(const char* data, std::size_t size,
                               JsonError& error) noexcept {
    error = {};
    if (loaded_) {
        return {ErrorCode::InvalidState, "dialogue",
                "A loaded dialogue document is immutable."};
    }
    reset();

    const Status parsed = JsonDocument::parse(data, size, document_, error);
    if (!parsed) {
        reset();
        return parsed;
    }
    const JsonValue root = document_.root();
    const Status rootFields = validateFields(root, kDialogueFields, "dialogue");
    if (!rootFields) {
        reset();
        return rootFields;
    }

    std::int64_t schemaVersion{};
    if (!root.find("schemaVersion").integer(schemaVersion)) {
        reset();
        return {ErrorCode::InvalidValue, "schemaVersion",
                "Dialogue JSON requires an integer schemaVersion."};
    }
    if (schemaVersion != 1) {
        reset();
        return {ErrorCode::UnsupportedVersion, "schemaVersion",
                "Dialogue JSON schema version is not supported."};
    }

    std::string_view id;
    std::string_view startNode;
    std::int64_t version{};
    const JsonValue jsonNodes = root.find("nodes");
    if (!root.find("id").string(id) ||
        !root.find("startNode").string(startNode) ||
        !root.find("version").integer(version) || version <= 0 ||
        version > std::numeric_limits<std::uint32_t>::max() ||
        !jsonNodes.isArray() || jsonNodes.size() == 0 ||
        jsonNodes.size() > kMaxDialogueNodes) {
        reset();
        return {ErrorCode::InvalidValue, "dialogue",
                "Dialogue JSON metadata or node count is invalid."};
    }

    for (std::size_t nodeIndex = 0; nodeIndex < jsonNodes.size(); ++nodeIndex) {
        const JsonValue jsonNode = jsonNodes.at(nodeIndex);
        const Status nodeFields = validateFields(jsonNode, kNodeFields, "nodes");
        if (!nodeFields) {
            reset();
            return nodeFields;
        }
        DialogueNode& node = nodes_[nodeIndex];
        if (!jsonNode.find("id").string(node.id) ||
            !jsonNode.find("speaker").string(node.speaker) ||
            !jsonNode.find("text").string(node.text)) {
            reset();
            return {ErrorCode::InvalidValue, "nodes",
                    "Each dialogue node requires string id, speaker, and text fields."};
        }
        const JsonValue localizedSpeaker = jsonNode.find("speakerCs");
        const JsonValue localizedLine = jsonNode.find("textCs");
        if ((localizedSpeaker.valid() && !localizedSpeaker.string(node.speakerCs)) ||
            (localizedLine.valid() && !localizedLine.string(node.textCs))) {
            reset();
            return {ErrorCode::InvalidValue, "nodes.localization",
                    "Localized speaker and text fields must be strings."};
        }
        const JsonValue nextNode = jsonNode.find("nextNode");
        if (nextNode.valid() && !nextNode.string(node.nextNode)) {
            reset();
            return {ErrorCode::InvalidValue, "nextNode",
                    "Dialogue nextNode must be a string when present."};
        }

        const JsonValue jsonChoices = jsonNode.find("choices");
        if (!jsonChoices.valid()) {
            continue;
        }
        if (!jsonChoices.isArray() || jsonChoices.size() > kMaxDialogueChoices) {
            reset();
            return {ErrorCode::InvalidValue, "choices",
                    "Dialogue choices must be an array of at most eight entries."};
        }

        const std::size_t choiceOffset = nodeIndex * kMaxDialogueChoices;
        for (std::size_t choiceIndex = 0; choiceIndex < jsonChoices.size();
             ++choiceIndex) {
            const JsonValue jsonChoice = jsonChoices.at(choiceIndex);
            const Status choiceFields = validateFields(jsonChoice, kChoiceFields, "choices");
            if (!choiceFields) {
                reset();
                return choiceFields;
            }
            DialogueChoice& choice = choices_[choiceOffset + choiceIndex];
            if (!jsonChoice.find("id").string(choice.id) ||
                !jsonChoice.find("label").string(choice.label) ||
                !jsonChoice.find("targetNode").string(choice.targetNode)) {
                reset();
                return {ErrorCode::InvalidValue, "choices",
                        "Each dialogue choice requires string id, label, and targetNode fields."};
            }
            const JsonValue localizedLabel = jsonChoice.find("labelCs");
            if (localizedLabel.valid() && !localizedLabel.string(choice.labelCs)) {
                reset();
                return {ErrorCode::InvalidValue, "choices.labelCs",
                        "Localized choice labels must be strings."};
            }

            const JsonValue visibleWhen = jsonChoice.find("visibleWhen");
            if (visibleWhen.valid()) {
                if (conditionCount_ == conditions_.size()) {
                    reset();
                    return {ErrorCode::CapacityExceeded, "visibleWhen",
                            "Dialogue JSON exceeds the conditional-choice limit."};
                }
                JsonConditionTree& condition = conditions_[conditionCount_];
                const Status conditionStatus = condition.parse(visibleWhen);
                if (!conditionStatus) {
                    reset();
                    return conditionStatus;
                }
                const Status conditionFields = validateConditionFields(visibleWhen);
                if (!conditionFields) {
                    reset();
                    return conditionFields;
                }
                choice.visibleWhen = condition.root();
                ++conditionCount_;
            }

            const JsonValue onSelect = jsonChoice.find("onSelect");
            if (onSelect.valid()) {
                const Status actionFields = validateFields(onSelect, kActionFields, "onSelect");
                if (!actionFields) {
                    reset();
                    return actionFields;
                }
                std::string_view actionId;
                if (!onSelect.find("id").string(actionId) || !validNamespaceKey(actionId)) {
                    reset();
                    return {ErrorCode::InvalidValue, "onSelect",
                            "onSelect requires a valid namespaced action id."};
                }
                choice.onSelect.id = actionId;

                const JsonValue jsonParameters = onSelect.find("parameters");
                if (jsonParameters.valid()) {
                    if (!jsonParameters.isArray()) {
                        reset();
                        return {ErrorCode::InvalidValue, "parameters",
                                "Action parameters must be an array."};
                    }
                    if (jsonParameters.size() > kMaxActionParametersPerChoice ||
                        jsonParameters.size() >
                            parameters_.size() - parameterCount_) {
                        reset();
                        return {ErrorCode::CapacityExceeded, "parameters",
                                "Dialogue action parameters exceed their bounded capacity."};
                    }
                    const std::size_t firstParameter = parameterCount_;
                    for (std::size_t parameterIndex = 0;
                         parameterIndex < jsonParameters.size(); ++parameterIndex) {
                        const JsonValue jsonParameter = jsonParameters.at(parameterIndex);
                        const Status parameterFields =
                            validateFields(jsonParameter, kParameterFields, "parameters");
                        if (!parameterFields) {
                            reset();
                            return parameterFields;
                        }
                        std::string_view parameterName;
                        StateValue parameterValue{};
                        if (!jsonParameter.find("name").string(parameterName) ||
                            !validActionParameterName(parameterName) ||
                            !parseStateValue(jsonParameter.find("value"), parameterValue)) {
                            reset();
                            return {ErrorCode::InvalidValue, "parameters",
                                    "Each action parameter requires a valid name and typed value."};
                        }
                        for (std::size_t previous = firstParameter;
                             previous < parameterCount_; ++previous) {
                            if (parameters_[previous].name == parameterName) {
                                reset();
                                return {ErrorCode::DuplicateId, "parameters",
                                        "Action parameter names must be unique."};
                            }
                        }
                        parameters_[parameterCount_++] = {parameterName, parameterValue};
                    }
                    choice.onSelect.parameters = {
                        parameters_.data() + firstParameter,
                        jsonParameters.size()};
                }
            }
        }
        if (!jsonChoices.size()) {
            continue;
        }
        node.choices = {choices_.data() + choiceOffset, jsonChoices.size()};
    }

    definition_ = {id, static_cast<std::uint32_t>(version), startNode,
                   {nodes_.data(), jsonNodes.size()}};
    const Status validation = DialogueModule::validateDefinition(definition_);
    if (!validation) {
        reset();
        return {validation.code, "dialogue", validation.detail};
    }
    loaded_ = true;
    return Status::success();
}

void DialogueDocument::reset() noexcept {
    document_.reset();
    nodes_.fill(DialogueNode{});
    choices_.fill(DialogueChoice{});
    parameters_.fill(ActionParameter{});
    for (JsonConditionTree& condition : conditions_) {
        condition.reset();
    }
    definition_ = {};
    conditionCount_ = 0;
    parameterCount_ = 0;
    loaded_ = false;
}

const ModuleInfo& DialogueModule::info() const noexcept {
    return kDialogueModuleInfo;
}

CapabilityManifestFragment narrativeCapabilityManifestFragment() noexcept {
    return {.modules = kNarrativeModuleManifest,
            .contentSchemas = kNarrativeSchemaManifest};
}

bool DialogueModule::initialize(ModuleContext& context) noexcept {
    (void)context;
    if (initialized_ || !validate()) {
        return false;
    }
    initialized_ = true;
    return true;
}

void DialogueModule::shutdown() noexcept {
    initialized_ = false;
}

Status DialogueModule::add(const DialogueDefinition& definition) noexcept {
    if (initialized_) {
        return {ErrorCode::InvalidState, definition.id,
                "Dialogue definitions cannot change after module initialization."};
    }
    const Status status = validateDefinition(definition);
    if (!status) {
        return status;
    }
    for (std::size_t index = 0; index < count_; ++index) {
        if (definitions_[index]->id == definition.id) {
            return {ErrorCode::DuplicateId, definition.id,
                    "Dialogue ID is already registered."};
        }
    }
    if (count_ == definitions_.size()) {
        return {ErrorCode::CapacityExceeded, definition.id,
                "Dialogue registry is full."};
    }
    definitions_[count_++] = &definition;
    return Status::success();
}

Status DialogueModule::validate() const noexcept {
    if (count_ == 0) {
        return {ErrorCode::InvalidState, kDialogueModuleInfo.id,
                "At least one dialogue definition must be registered."};
    }
    for (std::size_t index = 0; index < count_; ++index) {
        const Status status = validateDefinition(*definitions_[index]);
        if (!status) {
            return status;
        }
        for (std::size_t previous = 0; previous < index; ++previous) {
            if (definitions_[previous]->id == definitions_[index]->id) {
                return {ErrorCode::DuplicateId, definitions_[index]->id,
                        "Dialogue ID is already registered."};
            }
        }
    }
    return Status::success();
}

Status DialogueModule::find(std::string_view id,
                            const DialogueDefinition*& definition) const noexcept {
    for (std::size_t index = 0; index < count_; ++index) {
        if (definitions_[index]->id == id) {
            definition = definitions_[index];
            return Status::success();
        }
    }
    definition = nullptr;
    return {ErrorCode::NotFound, id, "Dialogue definition is not registered."};
}

Status DialogueModule::start(std::string_view id,
                             DialogueSession& session) const noexcept {
    if (!initialized_) {
        return {ErrorCode::InvalidState, id,
                "Dialogue module is not initialized."};
    }
    const DialogueDefinition* definition = nullptr;
    const Status found = find(id, definition);
    if (!found) {
        return found;
    }
    const std::size_t startIndex = findNodeIndex(*definition, definition->startNode);
    if (startIndex >= definition->nodes.size()) {
        return {ErrorCode::InvalidState, id, "Dialogue start node is unavailable."};
    }
    session.reset();
    session.definition_ = definition;
    session.nodeIndex_ = startIndex;
    return Status::success();
}

Status DialogueModule::validateDefinition(
    const DialogueDefinition& definition) noexcept {
    if (!isValidContentId(definition.id) || definition.version == 0 ||
        !validLocalId(definition.startNode) || definition.nodes.empty() ||
        definition.nodes.size() > kMaxDialogueNodes) {
        return {ErrorCode::InvalidValue, definition.id,
                "Dialogue ID, version, start node, or node count is invalid."};
    }

    bool hasStartNode = false;
    for (std::size_t nodeIndex = 0; nodeIndex < definition.nodes.size(); ++nodeIndex) {
        const DialogueNode& node = definition.nodes[nodeIndex];
        if (!validLocalId(node.id) || node.text.empty() || node.text.size() > 2048 ||
            !validUtf8(node.speaker) || !validUtf8(node.text) ||
            node.speaker.size() > 128 ||
            (!node.speakerCs.empty() &&
             (node.speakerCs.size() > 128 || !validUtf8(node.speakerCs))) ||
            (!node.textCs.empty() &&
             (node.textCs.size() > 2048 || !validUtf8(node.textCs))) ||
            node.choices.size() > kMaxDialogueChoices ||
            (!node.nextNode.empty() && !validLocalId(node.nextNode))) {
            return {ErrorCode::InvalidValue, node.id,
                    "Dialogue node text, IDs, transitions, or choice count are invalid."};
        }
        hasStartNode = hasStartNode || node.id == definition.startNode;
        for (std::size_t previous = 0; previous < nodeIndex; ++previous) {
            if (definition.nodes[previous].id == node.id) {
                return {ErrorCode::DuplicateId, node.id,
                        "Dialogue node IDs must be unique."};
            }
        }

        if (!node.nextNode.empty() &&
            findNodeIndex(definition, node.nextNode) >= definition.nodes.size()) {
            return {ErrorCode::MissingDependency, node.nextNode,
                    "Dialogue node transition target is missing."};
        }
        for (std::size_t choiceIndex = 0;
             choiceIndex < node.choices.size(); ++choiceIndex) {
            const DialogueChoice& choice = node.choices[choiceIndex];
            if (!validLocalId(choice.id) || choice.label.empty() ||
                choice.label.size() > 512 || !validUtf8(choice.label) ||
                (!choice.labelCs.empty() &&
                 (choice.labelCs.size() > 512 || !validUtf8(choice.labelCs))) ||
                (!choice.targetNode.empty() && !validLocalId(choice.targetNode)) ||
                (!choice.onSelect.id.empty() &&
                 !validNamespaceKey(choice.onSelect.id)) ||
                (choice.onSelect.id.empty() &&
                 !choice.onSelect.parameters.empty())) {
                return {ErrorCode::InvalidValue, choice.id,
                        "Dialogue choice metadata or action request is invalid."};
            }
            if (!choice.targetNode.empty() &&
                findNodeIndex(definition, choice.targetNode) >= definition.nodes.size()) {
                return {ErrorCode::MissingDependency, choice.targetNode,
                        "Dialogue choice target is missing."};
            }
            for (std::size_t previous = 0; previous < choiceIndex; ++previous) {
                if (node.choices[previous].id == choice.id) {
                    return {ErrorCode::DuplicateId, choice.id,
                            "Choice IDs must be unique within a dialogue node."};
                }
            }
        }
    }
    if (!hasStartNode) {
        return {ErrorCode::MissingDependency, definition.startNode,
                "Dialogue start node is not defined."};
    }
    return Status::success();
}

Status DialogueSession::inspect(const GameState& state,
                                DialogueLineView& line,
                                DialogueLanguage language) const noexcept {
    if (!active() || nodeIndex_ >= definition_->nodes.size()) {
        return {ErrorCode::InvalidState, {}, "Dialogue session is not active."};
    }
    const DialogueNode& node = definition_->nodes[nodeIndex_];
    std::size_t availableChoices{};
    for (const DialogueChoice& choice : node.choices) {
        bool visible{};
        const Status status = isChoiceVisible(choice, state, visible);
        if (!status) {
            return status;
        }
        availableChoices += visible ? 1U : 0U;
    }
    line = {definition_->id, node.id,
            localizedText(node.speaker, node.speakerCs, language),
            localizedText(node.text, node.textCs, language),
            availableChoices, availableChoices == 0};
    return Status::success();
}

Status DialogueSession::choiceAt(const GameState& state, std::size_t ordinal,
                                 DialogueChoiceView& view,
                                 DialogueLanguage language) const noexcept {
    if (!active() || nodeIndex_ >= definition_->nodes.size()) {
        return {ErrorCode::InvalidState, {}, "Dialogue session is not active."};
    }
    const DialogueNode& node = definition_->nodes[nodeIndex_];
    std::size_t visibleOrdinal{};
    for (const DialogueChoice& choice : node.choices) {
        bool visible{};
        const Status status = isChoiceVisible(choice, state, visible);
        if (!status) {
            return status;
        }
        if (visible && visibleOrdinal++ == ordinal) {
            view = {choice.id, localizedText(choice.label, choice.labelCs, language)};
            return Status::success();
        }
    }
    return {ErrorCode::NotFound, {}, "Visible dialogue choice index is out of range."};
}

Status DialogueSession::advance(const GameState& state) noexcept {
    if (!active() || nodeIndex_ >= definition_->nodes.size()) {
        return {ErrorCode::InvalidState, {}, "Dialogue session is not active."};
    }
    const DialogueNode& node = definition_->nodes[nodeIndex_];
    DialogueLineView line{};
    const Status inspected = inspect(state, line);
    if (!inspected) {
        return inspected;
    }
    if (line.availableChoices != 0) {
        return {ErrorCode::InvalidState, node.id,
                "Visible dialogue choices must be resolved before advancing."};
    }
    return moveTo(node.nextNode);
}

Status DialogueSession::select(std::string_view choiceId, const GameState& state,
                               const ActionRegistry* actions) noexcept {
    if (!active() || nodeIndex_ >= definition_->nodes.size()) {
        return {ErrorCode::InvalidState, choiceId, "Dialogue session is not active."};
    }
    const DialogueNode& node = definition_->nodes[nodeIndex_];
    const DialogueChoice* selected = nullptr;
    for (const DialogueChoice& choice : node.choices) {
        if (choice.id == choiceId) {
            selected = &choice;
            break;
        }
    }
    if (selected == nullptr) {
        return {ErrorCode::NotFound, choiceId,
                "Choice is not part of the current dialogue node."};
    }

    bool visible{};
    const Status visibility = isChoiceVisible(*selected, state, visible);
    if (!visibility) {
        return visibility;
    }
    if (!visible) {
        return {ErrorCode::InvalidState, choiceId,
                "Dialogue choice is currently unavailable."};
    }
    if (!selected->onSelect.id.empty()) {
        if (actions == nullptr) {
            return {ErrorCode::InvalidArgument, selected->onSelect.id,
                    "Dialogue choice requires an action registry."};
        }
        const ActionResult result = actions->execute(selected->onSelect);
        if (!result.status) {
            return result.status;
        }
    }
    return moveTo(selected->targetNode);
}

void DialogueSession::reset() noexcept {
    definition_ = nullptr;
    nodeIndex_ = 0;
}

std::size_t DialogueSession::findNode(std::string_view id) const noexcept {
    return definition_ == nullptr ? 0 : findNodeIndex(*definition_, id);
}

Status DialogueSession::isChoiceVisible(const DialogueChoice& choice,
                                        const GameState& state,
                                        bool& visible) const noexcept {
    if (choice.visibleWhen == nullptr) {
        visible = true;
        return Status::success();
    }
    const ConditionResult result = evaluate(*choice.visibleWhen, state);
    if (!result.status) {
        return result.status;
    }
    visible = result.value;
    return Status::success();
}

Status DialogueSession::moveTo(std::string_view nodeId) noexcept {
    if (nodeId.empty()) {
        reset();
        return Status::success();
    }
    const std::size_t target = findNode(nodeId);
    if (target >= definition_->nodes.size()) {
        return {ErrorCode::InvalidState, nodeId,
                "Validated dialogue target is no longer available."};
    }
    nodeIndex_ = target;
    return Status::success();
}

} // namespace echo::framework
