#pragma once

#include "echo/framework/data.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace echo::framework {

inline constexpr std::size_t kMaxDialogues = 32;
inline constexpr std::size_t kMaxDialogueNodes = 64;
inline constexpr std::size_t kMaxDialogueChoices = 8;
inline constexpr std::size_t kMaxDialogueConditions = 8;
inline constexpr std::size_t kMaxDialogueParameters = 128;
inline constexpr std::size_t kMaxActionParametersPerChoice = 16;

enum class DialogueLanguage : std::uint8_t { English, Czech };

struct DialogueChoice {
    std::string_view id;
    std::string_view label;
    std::string_view targetNode;
    const Condition* visibleWhen{};
    ActionRequest onSelect{};
    std::string_view labelCs{};
};

struct DialogueNode {
    std::string_view id;
    std::string_view speaker;
    std::string_view text;
    std::string_view nextNode;
    std::span<const DialogueChoice> choices{};
    std::string_view speakerCs{};
    std::string_view textCs{};
};

struct DialogueDefinition {
    std::string_view id;
    std::uint32_t version{};
    std::string_view startNode;
    std::span<const DialogueNode> nodes{};
};

class DialogueDocument {
public:
    DialogueDocument() noexcept = default;
    DialogueDocument(const DialogueDocument&) = delete;
    DialogueDocument& operator=(const DialogueDocument&) = delete;
    DialogueDocument(DialogueDocument&&) = delete;
    DialogueDocument& operator=(DialogueDocument&&) = delete;

    [[nodiscard]] Status parse(const char* data, std::size_t size,
                               JsonError& error) noexcept;
    [[nodiscard]] const DialogueDefinition* definition() const noexcept {
        return loaded_ ? &definition_ : nullptr;
    }

private:
    void reset() noexcept;

    JsonDocument document_{};
    std::array<DialogueNode, kMaxDialogueNodes> nodes_{};
    std::array<DialogueChoice, kMaxDialogueNodes * kMaxDialogueChoices> choices_{};
    std::array<ActionParameter, kMaxDialogueParameters> parameters_{};
    std::array<JsonConditionTree, kMaxDialogueConditions> conditions_{};
    DialogueDefinition definition_{};
    std::size_t conditionCount_{};
    std::size_t parameterCount_{};
    bool loaded_{};

    friend class DialogueModule;
};

struct DialogueLineView {
    std::string_view dialogueId;
    std::string_view nodeId;
    std::string_view speaker;
    std::string_view text;
    std::size_t availableChoices{};
    bool canAdvance{};
};

struct DialogueChoiceView {
    std::string_view id;
    std::string_view label;
};

class DialogueSession {
public:
    [[nodiscard]] bool active() const noexcept { return definition_ != nullptr; }
    [[nodiscard]] Status inspect(const GameState& state,
                                 DialogueLineView& line,
                                 DialogueLanguage language = DialogueLanguage::English) const noexcept;
    [[nodiscard]] Status choiceAt(const GameState& state, std::size_t ordinal,
                                  DialogueChoiceView& choice,
                                  DialogueLanguage language = DialogueLanguage::English) const noexcept;
    [[nodiscard]] Status advance(const GameState& state) noexcept;
    [[nodiscard]] Status select(std::string_view choiceId, const GameState& state,
                                const ActionRegistry* actions = nullptr) noexcept;
    void reset() noexcept;

private:
    [[nodiscard]] std::size_t findNode(std::string_view id) const noexcept;
    [[nodiscard]] Status isChoiceVisible(const DialogueChoice& choice,
                                         const GameState& state,
                                         bool& visible) const noexcept;
    [[nodiscard]] Status moveTo(std::string_view nodeId) noexcept;

    const DialogueDefinition* definition_{};
    std::size_t nodeIndex_{};
    friend class DialogueModule;
};

class DialogueModule final : public IModule {
public:
    [[nodiscard]] const ModuleInfo& info() const noexcept override;
    bool initialize(ModuleContext& context) noexcept override;
    void shutdown() noexcept override;

    [[nodiscard]] Status add(const DialogueDefinition& definition) noexcept;
    [[nodiscard]] Status validate() const noexcept;
    [[nodiscard]] Status find(std::string_view id,
                              const DialogueDefinition*& definition) const noexcept;
    [[nodiscard]] Status start(std::string_view id,
                               DialogueSession& session) const noexcept;
    [[nodiscard]] bool initialized() const noexcept { return initialized_; }

private:
    [[nodiscard]] static Status validateDefinition(
        const DialogueDefinition& definition) noexcept;

    std::array<const DialogueDefinition*, kMaxDialogues> definitions_{};
    std::size_t count_{};
    bool initialized_{};

    friend class DialogueDocument;
};

} // namespace echo::framework
