#include "echo/framework/cutscenes.hpp"

#include <cstdlib>
#include <iostream>
#include <string_view>

namespace {
int failures{};
void check(bool value, std::string_view message) {
    if (!value) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}
echo::framework::JsonDocument parse(std::string_view source) {
    echo::framework::JsonDocument document{};
    echo::framework::JsonError error{};
    check(static_cast<bool>(echo::framework::JsonDocument::parse(
              source.data(), source.size(), document, error)), "cutscene JSON parses");
    return document;
}
}

int main() {
    using namespace echo::framework;
    CutsceneModule module{};
    auto document = parse(R"({"schemaVersion":1,"cutscenes":[
      {"id":"echo:cutscene/test","cards":[
        {"speaker":"RADIO","speakerCs":"RADIO","text":"FIRST","textCs":"PRVNI"},
        {"speaker":"MIRA","speakerCs":"MIRA","text":"SECOND","textCs":"DRUHA"}]}]})");
    check(static_cast<bool>(module.load(document.root())),
          "bounded localized scene loads");
    const CutsceneDefinition* definition = module.find("echo:cutscene/test");
    check(definition != nullptr && definition->cards().size() == 2,
          "cutscene definitions are copied out of the parsed JSON");
    if (definition == nullptr) return EXIT_FAILURE;
    CutscenePlayer player{};
    player.start(*definition);
    check(player.active() && player.currentIndex() == 0 &&
              std::string_view(player.currentCard()->text.data()) == "FIRST",
          "playback starts on the first card");
    player.advance();
    check(player.active() && player.currentIndex() == 1 &&
              std::string_view(player.currentCard()->textCs.data()) == "DRUHA",
          "playback advances deterministically through cards");
    player.advance();
    check(!player.active() && player.currentCard() == nullptr,
          "advancing the final card ends playback");
    player.advance();
    check(!player.active(), "advancing inactive playback is harmless");
    player.start(*definition);
    player.reset();
    check(!player.active(), "playback can be reset");

    for (std::string_view invalid : {
             R"({"schemaVersion":2,"cutscenes":[]})",
             R"({"schemaVersion":1,"cutscenes":[{"id":"echo:cutscene/x","cards":[]}]})",
             R"({"schemaVersion":1,"cutscenes":[{"id":"echo:cutscene/x","cards":[{"speaker":"A","speakerCs":"A","text":"A","textCs":"A","extra":0}]}]})",
             R"({"schemaVersion":1,"cutscenes":[{"id":"echo:cutscene/x","cards":[{"speaker":"A","speakerCs":"A","text":"A","textCs":""}]}]})",
             R"({"schemaVersion":1,"cutscenes":[{"id":"echo:cutscene/x","cards":[{"speaker":"A","speakerCs":"A","text":"ABCDEFGHIJKLMNOPQRSTUVWXYZABCDEFGHIJKLMNOPQRSTUVWXYZABCDEFGHIJKLMNOPQRSTUVWXYZABCDEFGHIJKLMNOPQRSTUVWXYZABCDEFGHIJKLMNOPQRSTUV","textCs":"A"}]}]})"}) {
        auto bad = parse(invalid);
        check(!module.load(bad.root()), "invalid cutscene definitions are rejected");
        check(module.find("echo:cutscene/test") == definition,
              "failed reload preserves the original definition");
    }

    ModuleContext context{};
    check(module.initialize(context), "loaded cutscene module initializes");
    auto empty = parse(R"({"schemaVersion":1,"cutscenes":[]})");
    check(module.load(empty.root()).code == ErrorCode::InvalidState,
          "initialized cutscene content cannot be replaced");
    module.shutdown();
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
