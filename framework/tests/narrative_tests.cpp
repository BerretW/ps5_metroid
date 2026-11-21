#include "echo/framework/narrative.hpp"

#include <array>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string_view>

namespace {

int failures{};

void check(bool condition, std::string_view message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

void check(echo::framework::Status status, std::string_view message) {
    check(static_cast<bool>(status), message);
}

constexpr echo::framework::StateAddress kQuestAddress{
    echo::framework::StateScope::Global, {}, "echo.dialogue.quest"};
const echo::framework::Condition kHasQuest =
    echo::framework::Condition::exists(kQuestAddress);

echo::framework::ActionResult grantQuest(void* context,
                                         const echo::framework::ActionRequest& request) noexcept {
    auto* state = static_cast<echo::framework::GameState*>(context);
    if (state == nullptr || !request.parameters.empty()) {
        return {{echo::framework::ErrorCode::InvalidArgument, request.id,
                 "Grant action does not accept parameters."}, {}, false};
    }
    return {state->set(kQuestAddress,
                       echo::framework::StateValue::fromBoolean(true)),
            {}, false};
}

constexpr std::array<echo::framework::DialogueChoice, 2> kChoices{{
    {.id = "ask", .label = "Ask about the lab", .targetNode = "hint",
     .visibleWhen = &kHasQuest},
    {.id = "accept", .label = "Take the key", .targetNode = "grant",
     .onSelect = {"echo.action.grant_quest", {}}},
}};
constexpr std::array<echo::framework::DialogueNode, 4> kNodes{{
    {.id = "start", .speaker = "Engineer", .text = "The lab is locked.",
     .choices = kChoices},
    {.id = "hint", .speaker = "Engineer", .text = "The key opens the lab.",
     .nextNode = "start"},
    {.id = "grant", .speaker = "Engineer", .text = "Take this maintenance key.",
     .nextNode = "end"},
    {.id = "end", .speaker = "Engineer", .text = "Good luck."},
}};
constexpr echo::framework::DialogueDefinition kDialogue{
    "echo:dialogue/engineer", 1, "start", kNodes};

constexpr std::array<echo::framework::DialogueNode, 1> kInvalidNodes{{
    {.id = "start", .speaker = "Engineer", .text = "Broken transition.",
     .nextNode = "missing"},
}};
constexpr echo::framework::DialogueDefinition kInvalidDialogue{
    "echo:dialogue/invalid", 1, "start", kInvalidNodes};

constexpr std::array<echo::framework::DialogueChoice, 1> kFallbackChoices{{
    {.id = "conditional", .label = "Continue", .targetNode = "accepted",
     .visibleWhen = &kHasQuest},
}};
constexpr std::array<echo::framework::DialogueNode, 3> kFallbackNodes{{
    {.id = "start", .speaker = "Engineer", .text = "A state-gated choice.",
     .nextNode = "fallback", .choices = kFallbackChoices},
    {.id = "accepted", .speaker = "Engineer", .text = "Condition matched."},
    {.id = "fallback", .speaker = "Engineer", .text = "No choices are available."},
}};
constexpr echo::framework::DialogueDefinition kFallbackDialogue{
    "echo:dialogue/fallback", 1, "start", kFallbackNodes};

void testDialogueModule() {
    using namespace echo::framework;
    DialogueModule dialogue{};
    DialogueSession session{};
    check(dialogue.start("echo:dialogue/engineer", session).code ==
              ErrorCode::InvalidState,
          "dialogue cannot start before module initialization");
    check(dialogue.add(kDialogue), "valid dialogue definition registers");
    check(dialogue.add(kDialogue).code == ErrorCode::DuplicateId,
          "duplicate dialogue IDs are rejected");
    check(dialogue.add(kInvalidDialogue).code == ErrorCode::MissingDependency,
          "dialogue targets must resolve during registration");
    check(dialogue.add(kFallbackDialogue),
          "conditional choice nodes may declare a fallback transition");
    check(dialogue.validate(), "dialogue definitions validate before module startup");

    ModuleRegistry modules{};
    ModuleContext context{};
    DialogueModule emptyDialogue{};
    check(!emptyDialogue.initialize(context),
          "an empty dialogue module cannot initialize as a success-shaped service");
    check(modules.add(dialogue), "dialogue module registers with the shared lifecycle");
    check(modules.initializeAll(context), "dialogue module initializes through ModuleRegistry");
    check(dialogue.initialized() &&
              dialogue.info().id == "echo.narrative.dialogue" &&
              dialogue.info().version == 1 &&
              dialogue.info().provides.size() == 1,
          "dialogue module advertises stable metadata and its service");
    check(dialogue.add(kInvalidDialogue).code == ErrorCode::InvalidState,
          "dialogue catalog is immutable after initialization");

    check(dialogue.start("echo:dialogue/engineer", session),
          "registered dialogue starts at its declared node");
    GameState state{};
    DialogueLineView line{};
    check(session.inspect(state, line) && line.nodeId == "start" &&
              line.speaker == "Engineer" && line.availableChoices == 1 &&
              !line.canAdvance,
          "line inspection filters choices through current game state");
    DialogueChoiceView visibleChoice{};
    check(session.choiceAt(state, 0, visibleChoice) &&
              visibleChoice.id == "accept",
          "choice indexing addresses only visible choices");
    check(session.choiceAt(state, 1, visibleChoice).code == ErrorCode::NotFound,
          "invisible choices do not leak through the view API");
    check(session.advance(state).code == ErrorCode::InvalidState,
          "choice nodes cannot be advanced without selecting an option");
    check(session.select("ask", state).code == ErrorCode::InvalidState,
          "unavailable choices cannot be selected");
    check(session.select("accept", state).code == ErrorCode::InvalidArgument,
          "action-backed choices require an action registry");

    ActionRegistry actions{};
    check(actions.addCapability("echo.dialogue"), "dialogue action capability registers");
    check(actions.registerAction("echo.action.grant_quest", "echo.dialogue",
                                 &grantQuest, &state),
          "dialogue action handler registers");
    actions.setGameActive(true);
    ActionRegistry missingActions{};
    missingActions.setGameActive(true);
    check(session.select("accept", state, &missingActions).code == ErrorCode::UnknownAction &&
              session.inspect(state, line) && line.nodeId == "start" &&
              !state.contains(kQuestAddress),
          "unregistered dialogue actions do not advance the conversation");
    check(session.select("accept", state, &actions),
          "visible choice executes its registered action");
    check(state.contains(kQuestAddress) && session.inspect(state, line) &&
              line.nodeId == "grant" && line.canAdvance,
          "choice action updates state before entering the next line");
    check(session.advance(state) && session.inspect(state, line) && line.nodeId == "end",
          "linear dialogue transitions resolve to validated nodes");
    check(session.advance(state) && !session.active(),
          "terminal dialogue line closes its session when advanced");
    check(session.advance(state).code == ErrorCode::InvalidState,
          "closed dialogue sessions reject further operations");
    check(dialogue.start("echo:dialogue/engineer", session) &&
              session.inspect(state, line) && line.availableChoices == 2 &&
              session.choiceAt(state, 0, visibleChoice) && visibleChoice.id == "ask" &&
              session.select("ask", state) && session.inspect(state, line) &&
              line.nodeId == "hint" && session.advance(state) &&
              session.inspect(state, line) && line.nodeId == "start",
          "state changes reveal conditional choices and graph loops remain usable");

    GameState fallbackState{};
    check(dialogue.start("echo:dialogue/fallback", session) &&
              session.inspect(fallbackState, line) && line.availableChoices == 0 &&
              line.canAdvance && session.advance(fallbackState) &&
              session.inspect(fallbackState, line) && line.nodeId == "fallback",
          "nodes advance through their fallback when all conditional choices are hidden");

    check(dialogue.start("echo:dialogue/missing", session).code == ErrorCode::NotFound,
          "unknown dialogue IDs are rejected");
    modules.shutdownAll();
    check(!dialogue.initialized(), "dialogue module shuts down through ModuleRegistry");
    check(dialogue.start("echo:dialogue/engineer", session).code == ErrorCode::InvalidState,
          "dialogues cannot start after module shutdown");
}

void testOwnedDialogueDocument() {
    using namespace echo::framework;
    constexpr char json[] = R"({
        "schemaVersion": 1,
        "id": "echo:dialogue/json_guide",
        "version": 2,
        "startNode": "start",
        "nodes": [
            {
                "id": "start",
                "speaker": "Guide",
                "text": "Welcome.",
                "speakerCs": "\u0050\u0052\u016E\u0056\u004F\u0044\u004B\u0059\u004E\u011A",
                "textCs": "\u0056\u00CD\u0054\u0045\u004A.",
                "choices": [{
                    "id": "continue",
                    "label": "Continue",
                    "labelCs": "\u0050\u004F\u004B\u0052\u0041\u010C\u0055\u004A",
                    "targetNode": "end",
                    "visibleWhen": {
                        "op": "exists",
                        "scope": "global",
                        "key": "echo.dialogue.unlocked"
                    },
                    "onSelect": {
                        "id": "echo.action.grant_quest",
                        "parameters": [{"name": "amount", "value": 2}]
                    }
                }]
            },
            {"id": "end", "speaker": "Guide", "text": "Good luck."}
        ]
    })";

    std::array<char, sizeof(json)> input{};
    std::memcpy(input.data(), json, sizeof(json));
    DialogueDocument document{};
    JsonError error{};
    const Status loadStatus = document.parse(input.data(), sizeof(json) - 1, error);
    check(loadStatus, "well-formed per-dialogue JSON loads");
    if (!loadStatus) {
        std::cerr << "Dialogue JSON load error: " << loadStatus.detail << '\n';
    }
    input.fill('x');
    const DialogueDefinition* definition = document.definition();
    check(definition != nullptr && definition->id == "echo:dialogue/json_guide" &&
              definition->version == 2 && definition->nodes[0].text == "Welcome.",
          "loaded dialogue owns its JSON strings independently of the source buffer");
    check(document.parse(json, sizeof(json) - 1, error).code == ErrorCode::InvalidState,
          "loaded dialogue documents cannot be replaced and invalidate registered views");

    DialogueModule module{};
    check(definition != nullptr && module.add(*definition),
          "owned JSON definitions register with the dialogue module");
    ModuleContext context{};
    check(module.initialize(context), "module initializes with owned JSON definition");
    DialogueSession session{};
    GameState state{};
    DialogueLineView line{};
    check(module.start("echo:dialogue/json_guide", session) &&
              session.inspect(state, line) && line.text == "Welcome." &&
              line.availableChoices == 0,
          "JSON condition trees keep choices hidden until their state condition matches");
    constexpr StateAddress unlocked{
        StateScope::Global, {}, "echo.dialogue.unlocked"};
    check(state.set(unlocked, StateValue::fromBoolean(true)) &&
              session.inspect(state, line) && line.availableChoices == 1,
          "owned JSON condition trees evaluate against current game state");
    DialogueChoiceView localizedChoice{};
    check(session.inspect(state, line, DialogueLanguage::Czech) &&
              line.speaker == "\x50\x52\xC5\xAE\x56\x4F\x44\x4B\x59\x4E\xC4\x9A" &&
              line.text == "\x56\xC3\x8D\x54\x45\x4A." &&
              session.choiceAt(state, 0, localizedChoice, DialogueLanguage::Czech) &&
              localizedChoice.label == "\x50\x4F\x4B\x52\x41\xC4\x8C\x55\x4A",
          "JSON-owned Czech text and choice labels render through locale-aware views");
    const std::array<DialogueNode, 1> fallbackNodes{{
        {.id = "fallback", .speaker = "Guide", .text = "Good luck."},
    }};
    const DialogueDefinition fallbackDefinition{
        .id = "echo:dialogue/fallback",
        .version = 1,
        .startNode = "fallback",
        .nodes = fallbackNodes,
    };
    DialogueModule fallbackModule{};
    DialogueSession fallbackSession{};
    DialogueLineView fallbackLine{};
    check(fallbackModule.add(fallbackDefinition) &&
              fallbackModule.initialize(context) &&
              fallbackModule.start(fallbackDefinition.id, fallbackSession) &&
              fallbackSession.inspect(state, fallbackLine, DialogueLanguage::Czech) &&
              fallbackLine.text == "Good luck.",
          "missing translations fall back to the original English text");
    fallbackModule.shutdown();
    if (definition != nullptr && !definition->nodes[0].choices.empty()) {
        const DialogueChoice& choice = definition->nodes[0].choices[0];
        check(choice.onSelect.id == "echo.action.grant_quest" &&
                  choice.onSelect.parameters.size() == 1 &&
                  choice.onSelect.parameters[0].name == "amount" &&
                  choice.onSelect.parameters[0].value.type == ValueType::Integer &&
                  choice.onSelect.parameters[0].value.integer == 2,
              "JSON action IDs and typed parameters remain available to the choice");
    } else {
        check(false, "loaded dialogue exposes its action-backed choice");
    }

    constexpr char unsupported[] =
        R"({"schemaVersion":2,"id":"echo:dialogue/future","version":1,"startNode":"start","nodes":[]})";
    DialogueDocument invalid{};
    check(invalid.parse(unsupported, sizeof(unsupported) - 1, error).code ==
              ErrorCode::UnsupportedVersion &&
              invalid.definition() == nullptr,
          "unsupported dialogue schema versions are rejected without exposing partial data");
    const auto rejects = [&](std::string_view input, std::string_view message) {
        const Status status = invalid.parse(input.data(), input.size(), error);
        check(!status && invalid.definition() == nullptr, message);
    };
    rejects(R"({"schemaVersion":1,"id":"echo:dialogue/test","version":1,"startNode":"start","nodes":[{"id":"start","speaker":"One","speaker":"Two","text":"Hello."}]})",
            "duplicate dialogue fields are rejected");
    rejects(R"({"schemaVersion":1,"id":"echo:dialogue/test","version":1,"startNode":"start","nodes":[{"id":"start","speaker":"One","text":"Hello.","nexNode":"end"}]})",
            "unknown dialogue fields are rejected rather than ignored");
    rejects(R"({"schemaVersion":1,"id":"echo:dialogue/test","version":1,"startNode":"start","nodes":[{"id":"start","speaker":"One","text":"Hello.","choices":[{"id":"go","label":"Go","targetNode":"","visibleWhen":{"op":"exists","op":"compare","scope":"global","key":"echo.test"}}]}]})",
            "duplicate condition fields are rejected");
    rejects(R"({"schemaVersion":1,"id":"echo:dialogue/test","version":1,"startNode":"start","nodes":[{"id":"start","speaker":"One","text":"Hello.","choices":[{"id":"go","label":"Go","targetNode":"","onSelect":{"id":""}}]}]})",
            "an explicitly configured action cannot have an empty ID");
}

} // namespace

int main() {
    testDialogueModule();
    testOwnedDialogueDocument();
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
