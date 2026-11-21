#include "echo/framework/narrative.hpp"

#include <array>
#include <cstdio>

namespace {

constexpr std::array<echo::framework::DialogueNode, 2> kNodes{{
    {.id = "greeting", .speaker = "Guide", .text = "Welcome to the station.",
     .nextNode = "farewell"},
    {.id = "farewell", .speaker = "Guide", .text = "Stay alert."},
}};
constexpr echo::framework::DialogueDefinition kDialogue{
    "example:dialogue/guide", 1, "greeting", kNodes};

} // namespace

int main() {
    echo::framework::DialogueModule dialogue{};
    if (!dialogue.add(kDialogue)) {
        return 1;
    }
    echo::framework::ModuleRegistry modules{};
    echo::framework::ModuleContext context{};
    if (!modules.add(dialogue) || !modules.initializeAll(context)) {
        return 1;
    }

    echo::framework::DialogueSession session{};
    echo::framework::GameState state{};
    if (!dialogue.start(kDialogue.id, session)) {
        modules.shutdownAll();
        return 1;
    }
    do {
        echo::framework::DialogueLineView line{};
        if (!session.inspect(state, line)) {
            modules.shutdownAll();
            return 1;
        }
        std::printf("%.*s: %.*s\n",
                    static_cast<int>(line.speaker.size()), line.speaker.data(),
                    static_cast<int>(line.text.size()), line.text.data());
        if (!session.advance(state)) {
            modules.shutdownAll();
            return 1;
        }
    } while (session.active());

    modules.shutdownAll();
    return 0;
}
