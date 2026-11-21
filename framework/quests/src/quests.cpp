#include "echo/framework/quests.hpp"

#include <algorithm>
#include <cstring>

namespace echo::framework {
namespace {

constexpr std::array<std::string_view, 1> kServices{"echo.service.quests"};
constexpr ModuleInfo kModuleInfo{"echo.rpg.quests", "Quests", 1, {}, {}, kServices, {}};
constexpr std::array<ManifestModuleDescriptor, 1> kModules{{
    {"echo.rpg.quests", "Quests", 1, {}, {}, kServices, {}},
}};
constexpr std::array<ManifestSchemaDescriptor, 1> kSchemas{{
    {"echo.rpg.quests_json", 1},
}};
constexpr std::array<std::string_view, 2> kDocumentFields{"schemaVersion", "quests"};
constexpr std::array<std::string_view, 2> kQuestFields{"id", "objectives"};
constexpr std::array<std::string_view, 3> kObjectiveFields{"id", "fact", "text"};

[[nodiscard]] bool exactFields(JsonValue object,
                               std::span<const std::string_view> allowed) noexcept {
    if (!object.isObject() || object.size() != allowed.size()) return false;
    for (std::size_t index = 0; index < object.size(); ++index) {
        std::string_view name;
        JsonValue value{};
        if (!object.memberAt(index, name, value) ||
            std::find(allowed.begin(), allowed.end(), name) == allowed.end()) return false;
        for (std::size_t previous = 0; previous < index; ++previous) {
            std::string_view earlier;
            JsonValue ignored{};
            if (!object.memberAt(previous, earlier, ignored) || earlier == name) return false;
        }
    }
    return true;
}

template <std::size_t Size>
[[nodiscard]] bool copyText(std::string_view input, std::array<char, Size>& output) noexcept {
    if (input.empty() || input.size() >= Size || input.find('\0') != std::string_view::npos) {
        return false;
    }
    std::memcpy(output.data(), input.data(), input.size());
    output[input.size()] = '\0';
    return true;
}

[[nodiscard]] bool localId(std::string_view id) noexcept {
    if (id.empty() || id.size() >= 64) return false;
    for (const char c : id) {
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
              c == '_' || c == '-')) return false;
    }
    return true;
}

} // namespace

QuestProgress evaluateQuest(const QuestDefinition& quest,
                            std::span<const std::string_view> facts) noexcept {
    QuestProgress result{0, quest.objectives().size(), {}};
    for (const QuestObjective& objective : quest.objectives()) {
        if (std::find(facts.begin(), facts.end(), std::string_view(objective.fact.data())) ==
            facts.end()) {
            result.objective = objective.text.data();
            return result;
        }
        ++result.completed;
    }
    return result;
}

Status QuestModule::load(JsonValue document) noexcept {
    if (initialized_) {
        return {ErrorCode::InvalidState, "echo.rpg.quests",
                "Quest definitions cannot change while the module is initialized."};
    }
    std::int64_t version{};
    const JsonValue entries = document.find("quests");
    if (!exactFields(document, kDocumentFields) ||
        !document.find("schemaVersion").integer(version) || version != 1 ||
        !entries.isArray() || entries.size() == 0 || entries.size() > kMaxQuests) {
        return {ErrorCode::InvalidValue, "quests",
                "Quest content requires schema version 1 and a bounded nonempty quest list."};
    }
    QuestModule candidate{};
    for (std::size_t index = 0; index < entries.size(); ++index) {
        const JsonValue entry = entries.at(index);
        std::string_view id;
        const JsonValue objectives = entry.find("objectives");
        if (!exactFields(entry, kQuestFields) || !entry.find("id").string(id) ||
            !isValidContentId(id) || id.find(":quest/") == std::string_view::npos ||
            !objectives.isArray() || objectives.size() == 0 ||
            objectives.size() > kMaxQuestObjectives || candidate.find(id) != nullptr) {
            return {ErrorCode::InvalidValue, "quests.quest",
                    "Quest IDs must be unique and have bounded nonempty objectives."};
        }
        QuestDefinition& quest = candidate.quests_[candidate.count_];
        if (!copyText(id, quest.id_)) {
            return {ErrorCode::CapacityExceeded, "quests.quest", "Quest ID is too long."};
        }
        for (std::size_t step = 0; step < objectives.size(); ++step) {
            const JsonValue value = objectives.at(step);
            std::string_view objectiveId, fact, text;
            if (!exactFields(value, kObjectiveFields) ||
                !value.find("id").string(objectiveId) || !localId(objectiveId) ||
                !value.find("fact").string(fact) || !isValidContentId(fact) ||
                fact.find(":fact/") == std::string_view::npos ||
                !value.find("text").string(text)) {
                return {ErrorCode::InvalidValue, "quests.objective",
                        "Objective requires a valid ID, fact ID and text."};
            }
            for (std::size_t previous = 0; previous < step; ++previous) {
                if (quest.objectives_[previous].id.data() == objectiveId ||
                    quest.objectives_[previous].fact.data() == fact) {
                    return {ErrorCode::DuplicateId, "quests.objective",
                            "Objective IDs and facts must be unique within a quest."};
                }
            }
            QuestObjective& objective = quest.objectives_[step];
            if (!copyText(objectiveId, objective.id) || !copyText(fact, objective.fact) ||
                !copyText(text, objective.text)) {
                return {ErrorCode::CapacityExceeded, "quests.objective",
                        "Objective ID, fact or text exceeds its fixed storage."};
            }
            ++quest.objectiveCount_;
        }
        ++candidate.count_;
    }
    quests_ = candidate.quests_;
    count_ = candidate.count_;
    return Status::success();
}

const ModuleInfo& QuestModule::info() const noexcept { return kModuleInfo; }

bool QuestModule::initialize(ModuleContext&) noexcept {
    initialized_ = count_ != 0;
    return initialized_;
}

void QuestModule::shutdown() noexcept { initialized_ = false; }

const QuestDefinition* QuestModule::find(std::string_view id) const noexcept {
    for (std::size_t index = 0; index < count_; ++index) {
        if (quests_[index].id() == id) return &quests_[index];
    }
    return nullptr;
}

CapabilityManifestFragment questsCapabilityManifestFragment() noexcept {
    return {.modules = kModules, .contentSchemas = kSchemas};
}

} // namespace echo::framework
