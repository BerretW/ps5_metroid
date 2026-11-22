#include "echo/framework/cutscenes.hpp"

#include <algorithm>
#include <cstring>

namespace echo::framework {
namespace {

constexpr std::array<std::string_view, 1> kServices{"echo.service.cutscenes"};
constexpr ModuleInfo kModuleInfo{
    "echo.narrative.cutscenes", "Cutscenes", 1, {}, {}, kServices, {}};
constexpr std::array<ManifestModuleDescriptor, 1> kModules{{
    {"echo.narrative.cutscenes", "Cutscenes", 1, {}, {}, kServices, {}},
}};
constexpr std::array<ManifestSchemaDescriptor, 1> kSchemas{{
    {"echo.narrative.cutscene_json", 1},
}};
constexpr std::array<std::string_view, 2> kDocumentFields{"schemaVersion", "cutscenes"};
constexpr std::array<std::string_view, 2> kCutsceneFields{"id", "cards"};
constexpr std::array<std::string_view, 4> kCardFields{
    "speaker", "text", "speakerCs", "textCs"};

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
[[nodiscard]] bool copyText(std::string_view input,
                            std::array<char, Size>& output) noexcept {
    if (input.empty() || input.size() >= Size ||
        input.find('\0') != std::string_view::npos) return false;
    std::memcpy(output.data(), input.data(), input.size());
    output[input.size()] = '\0';
    return true;
}

} // namespace

Status CutsceneModule::load(JsonValue document) noexcept {
    if (initialized_) {
        return {ErrorCode::InvalidState, "echo.narrative.cutscenes",
                "Cutscene definitions cannot change while the module is initialized."};
    }
    std::int64_t version{};
    const JsonValue entries = document.find("cutscenes");
    if (!exactFields(document, kDocumentFields) ||
        !document.find("schemaVersion").integer(version) || version != 1 ||
        !entries.isArray() || entries.size() == 0 || entries.size() > kMaxCutscenes) {
        return {ErrorCode::InvalidValue, "cutscenes",
                "Cutscene content requires schema version 1 and a bounded nonempty list."};
    }
    CutsceneModule candidate{};
    for (std::size_t index = 0; index < entries.size(); ++index) {
        const JsonValue entry = entries.at(index);
        std::string_view id;
        const JsonValue cards = entry.find("cards");
        if (!exactFields(entry, kCutsceneFields) ||
            !entry.find("id").string(id) || !isValidContentId(id) ||
            id.find(":cutscene/") == std::string_view::npos ||
            !cards.isArray() || cards.size() == 0 || cards.size() > kMaxCutsceneCards ||
            candidate.find(id) != nullptr) {
            return {ErrorCode::InvalidValue, "cutscenes.entry",
                    "Cutscenes require unique stable IDs and bounded nonempty card lists."};
        }
        CutsceneDefinition& definition = candidate.cutscenes_[candidate.count_];
        if (!copyText(id, definition.id_)) {
            return {ErrorCode::CapacityExceeded, "cutscenes.id",
                    "Cutscene ID exceeds its fixed storage."};
        }
        for (std::size_t cardIndex = 0; cardIndex < cards.size(); ++cardIndex) {
            const JsonValue value = cards.at(cardIndex);
            std::string_view speaker, text, speakerCs, textCs;
            if (!exactFields(value, kCardFields) ||
                !value.find("speaker").string(speaker) ||
                !value.find("text").string(text) ||
                !value.find("speakerCs").string(speakerCs) ||
                !value.find("textCs").string(textCs)) {
                return {ErrorCode::InvalidValue, "cutscenes.card",
                        "Each cutscene card requires English and Czech speaker and text."};
            }
            CutsceneCard& card = definition.cards_[cardIndex];
            if (!copyText(speaker, card.speaker) ||
                !copyText(text, card.text) ||
                !copyText(speakerCs, card.speakerCs) ||
                !copyText(textCs, card.textCs)) {
                return {ErrorCode::CapacityExceeded, "cutscenes.card",
                        "Cutscene text exceeds its fixed storage."};
            }
            ++definition.cardCount_;
        }
        ++candidate.count_;
    }
    cutscenes_ = candidate.cutscenes_;
    count_ = candidate.count_;
    return Status::success();
}

const ModuleInfo& CutsceneModule::info() const noexcept { return kModuleInfo; }

bool CutsceneModule::initialize(ModuleContext&) noexcept {
    initialized_ = count_ != 0;
    return initialized_;
}

void CutsceneModule::shutdown() noexcept { initialized_ = false; }

const CutsceneDefinition* CutsceneModule::find(std::string_view id) const noexcept {
    for (std::size_t index = 0; index < count_; ++index) {
        if (cutscenes_[index].id() == id) return &cutscenes_[index];
    }
    return nullptr;
}

void CutscenePlayer::start(const CutsceneDefinition& definition) noexcept {
    definition_ = &definition;
    index_ = 0;
}

void CutscenePlayer::advance() noexcept {
    if (definition_ == nullptr) return;
    ++index_;
    if (index_ >= definition_->cards().size()) reset();
}

void CutscenePlayer::reset() noexcept {
    definition_ = nullptr;
    index_ = 0;
}

const CutsceneCard* CutscenePlayer::currentCard() const noexcept {
    return definition_ != nullptr && index_ < definition_->cards().size()
               ? &definition_->cards()[index_]
               : nullptr;
}

std::size_t CutscenePlayer::cardCount() const noexcept {
    return definition_ != nullptr ? definition_->cards().size() : 0;
}

CapabilityManifestFragment cutscenesCapabilityManifestFragment() noexcept {
    return {.modules = kModules, .contentSchemas = kSchemas};
}

} // namespace echo::framework
