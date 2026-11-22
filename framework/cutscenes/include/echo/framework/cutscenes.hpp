#pragma once

#include "echo/framework/capability_manifest.hpp"
#include "echo/framework/data.hpp"

#include <array>
#include <cstddef>
#include <span>
#include <string_view>

namespace echo::framework {

inline constexpr std::size_t kMaxCutscenes = 8;
inline constexpr std::size_t kMaxCutsceneCards = 8;

struct CutsceneCard {
    std::array<char, 48> speaker{};
    std::array<char, 48> speakerCs{};
    std::array<char, 112> text{};
    std::array<char, 112> textCs{};
};

class CutsceneDefinition {
public:
    [[nodiscard]] std::string_view id() const noexcept { return id_.data(); }
    [[nodiscard]] std::span<const CutsceneCard> cards() const noexcept {
        return {cards_.data(), cardCount_};
    }

private:
    std::array<char, 128> id_{};
    std::array<CutsceneCard, kMaxCutsceneCards> cards_{};
    std::size_t cardCount_{};
    friend class CutsceneModule;
};

class CutsceneModule final : public IModule {
public:
    [[nodiscard]] Status load(JsonValue document) noexcept;
    [[nodiscard]] const ModuleInfo& info() const noexcept override;
    bool initialize(ModuleContext&) noexcept override;
    void shutdown() noexcept override;
    [[nodiscard]] const CutsceneDefinition* find(std::string_view id) const noexcept;
    [[nodiscard]] std::span<const CutsceneDefinition> definitions() const noexcept {
        return {cutscenes_.data(), count_};
    }
    [[nodiscard]] bool initialized() const noexcept { return initialized_; }

private:
    std::array<CutsceneDefinition, kMaxCutscenes> cutscenes_{};
    std::size_t count_{};
    bool initialized_{};
};

class CutscenePlayer {
public:
    void start(const CutsceneDefinition& definition) noexcept;
    void advance() noexcept;
    void reset() noexcept;
    [[nodiscard]] bool active() const noexcept { return definition_ != nullptr; }
    [[nodiscard]] const CutsceneCard* currentCard() const noexcept;
    [[nodiscard]] std::size_t currentIndex() const noexcept { return index_; }
    [[nodiscard]] std::size_t cardCount() const noexcept;

private:
    const CutsceneDefinition* definition_{};
    std::size_t index_{};
};

[[nodiscard]] CapabilityManifestFragment cutscenesCapabilityManifestFragment() noexcept;

} // namespace echo::framework
