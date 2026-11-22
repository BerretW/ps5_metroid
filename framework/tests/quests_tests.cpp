#include "echo/framework/quests.hpp"

#include <array>
#include <cstdlib>
#include <iostream>
#include <string>
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
              source.data(), source.size(), document, error)), "quest JSON parses");
    return document;
}
}

int main() {
    using namespace echo::framework;
    QuestModule module{};
    {
        auto document = parse(R"({"schemaVersion":1,"quests":[
          {"id":"echo:quest/test","objectives":[
            {"id":"first","fact":"echo:fact/a","text":"FIRST"},
            {"id":"second","fact":"echo:fact/b","text":"SECOND"}]}]})");
        check(static_cast<bool>(module.load(document.root())), "valid quests load");
    }
    const QuestDefinition* quest = module.find("echo:quest/test");
    check(quest != nullptr, "quest definitions outlive their JSON document");
    if (quest == nullptr) return EXIT_FAILURE;
    const std::array<std::string_view, 2> facts{"echo:fact/a", "echo:fact/b"};
    QuestProgress progress = evaluateQuest(*quest, {});
    check(progress.completed == 0 && progress.total == 2 && progress.objective == "FIRST",
          "first missing fact determines the objective");
    progress = evaluateQuest(*quest, {facts.data() + 1, 1});
    check(progress.completed == 0 && progress.objective == "FIRST",
          "a later fact does not skip an earlier objective");
    progress = evaluateQuest(*quest, {facts.data(), 1});
    check(progress.completed == 1 && progress.objective == "SECOND",
          "completed first fact advances the objective");
    progress = evaluateQuest(*quest, facts);
    check(progress.completed == 2 && progress.objective.empty(), "all facts complete the quest");

    QuestModule branchModule{};
    auto branchDocument = parse(R"({"schemaVersion":2,"quests":[
      {"id":"echo:quest/branch","objectives":[
        {"id":"first","fact":"echo:fact/a","text":"FIRST"},
        {"id":"last","fact":"echo:fact/b","text":"LAST"}],
       "branch":{"after":1,"prompt":"CHOOSE",
        "choices":[
          {"id":"coil","label":"COIL","fact":"echo:fact/coil","objective":"GET COIL","rewardItem":"echo:item/shield_cell","rewardQuantity":1},
          {"id":"sentry","label":"SENTRY","fact":"echo:fact/sentry","objective":"DEFEAT SENTRY","rewardItem":"echo:item/med_kit","rewardQuantity":1}]}}]})");
    check(static_cast<bool>(branchModule.load(branchDocument.root())),
          "bounded branched quest and branch rewards load");
    const QuestDefinition* branched = branchModule.find("echo:quest/branch");
    check(branched != nullptr, "branched quest remains available after loading");
    if (branched != nullptr) {
        const std::array<std::string_view, 1> firstFact{"echo:fact/a"};
        progress = evaluateQuest(*branched, firstFact);
        check(progress.completed == 1 && progress.choiceRequired &&
                  progress.choices.size() == 2 && progress.objective == "CHOOSE",
              "the quest pauses for an explicit bounded branch choice");
        progress = evaluateQuest(*branched, firstFact, "coil");
        check(progress.completed == 1 && !progress.choiceRequired &&
                  progress.objective == "GET COIL",
              "selected route waits for its branch-specific fact");
        const std::array<std::string_view, 3> coilFacts{
            "echo:fact/a", "echo:fact/coil", "echo:fact/b"};
        progress = evaluateQuest(*branched, coilFacts, "coil");
        check(progress.completed == 3 && progress.total == 3 &&
                  progress.objective.empty(),
              "the selected branch completes before the shared final objective");
        check(!isValidQuestBranch(*branched, "unknown"),
              "branch lookup rejects an unlisted route");
    }

    for (std::string_view invalid : {
             R"({"schemaVersion":2,"quests":[]})",
             R"({"schemaVersion":1,"quests":[{"id":"echo:quest/x","objectives":[]}]})",
             R"({"schemaVersion":1,"quests":[{"id":"echo:quest/x","objectives":[{"id":"a","fact":"echo:fact/a","text":"A"},{"id":"a","fact":"echo:fact/b","text":"B"}]}]})",
             R"({"schemaVersion":1,"quests":[{"id":"echo:quest/x","objectives":[{"id":"a","fact":"echo:fact/a","text":"A"},{"id":"b","fact":"echo:fact/a","text":"B"}]}]})",
             R"({"schemaVersion":1,"quests":[{"id":"echo:quest/x","objectives":[{"id":"a","fact":"bad","text":"A"}]}]})",
             R"({"schemaVersion":1,"quests":[{"id":"echo:quest/x","objectives":[{"id":"a","fact":"echo:fact/a","text":"ABCDEFGHIJKLMNOPQRSTUVWXYZABCDEFGHIJKLMNOPQRSTUVWXYZABCDEFGHIJKL"}]}]})",
             R"({"schemaVersion":1,"quests":[{"id":"echo:quest/x","objectives":[{"id":"a","fact":"echo:fact/a","text":"A"},{"id":"b","fact":"echo:fact/b","text":"B"},{"id":"c","fact":"echo:fact/c","text":"C"},{"id":"d","fact":"echo:fact/d","text":"D"},{"id":"e","fact":"echo:fact/e","text":"E"},{"id":"f","fact":"echo:fact/f","text":"F"},{"id":"g","fact":"echo:fact/g","text":"G"},{"id":"h","fact":"echo:fact/h","text":"H"},{"id":"i","fact":"echo:fact/i","text":"I"}]}]})",
             R"({"schemaVersion":1,"quests":[{"id":"echo:quest/x","objectives":[{"id":"a","fact":"echo:fact/a","text":"A","extra":0}]}]})",
             R"({"schemaVersion":2,"quests":[{"id":"echo:quest/x","objectives":[{"id":"a","fact":"echo:fact/a","text":"A"},{"id":"b","fact":"echo:fact/b","text":"B"}],"branch":{"after":0,"prompt":"CHOOSE","choices":[{"id":"a","label":"A","fact":"echo:fact/c","objective":"C","rewardItem":"echo:item/med_kit","rewardQuantity":1},{"id":"b","label":"B","fact":"echo:fact/d","objective":"D","rewardItem":"echo:item/med_kit","rewardQuantity":1}]}}]})",
             R"({"schemaVersion":2,"quests":[{"id":"echo:quest/x","objectives":[{"id":"a","fact":"echo:fact/a","text":"A"},{"id":"b","fact":"echo:fact/b","text":"B"}],"branch":{"after":1,"prompt":"CHOOSE","choices":[{"id":"a","label":"A","fact":"echo:fact/c","objective":"C","rewardItem":"echo:item/med_kit","rewardQuantity":1},{"id":"b","label":"B","fact":"echo:fact/c","objective":"D","rewardItem":"echo:item/med_kit","rewardQuantity":1}]}}]})",
             R"({"schemaVersion":2,"quests":[{"id":"echo:quest/x","objectives":[{"id":"a","fact":"echo:fact/a","text":"A"},{"id":"b","fact":"echo:fact/b","text":"B"}],"branch":{"after":1,"prompt":"CHOOSE","choices":[{"id":"a","label":"A","fact":"echo:fact/c","objective":"C","rewardItem":"echo:item/med_kit","rewardQuantity":1}]}}]})"}) {
        auto document = parse(invalid);
        check(!module.load(document.root()), "malformed quest definitions are rejected");
        check(module.find("echo:quest/test") != nullptr, "failed load preserves the catalog");
    }
    const auto catalog = [](std::size_t questCount, std::size_t objectiveCount) {
        std::string json = R"({"schemaVersion":1,"quests":[)";
        for (std::size_t quest = 0; quest < questCount; ++quest) {
            if (quest != 0) json += ',';
            json += R"({"id":"echo:quest/q)" + std::to_string(quest) + R"(","objectives":[)";
            for (std::size_t objective = 0; objective < objectiveCount; ++objective) {
                if (objective != 0) json += ',';
                json += R"({"id":"s)" + std::to_string(objective) +
                        R"(","fact":"echo:fact/f)" + std::to_string(objective) +
                        R"(","text":"GO"})";
            }
            json += "]}";
        }
        return json + "]}";
    };
    QuestModule bounded{};
    auto maximum = parse(catalog(kMaxQuests, kMaxQuestObjectives));
    check(bounded.load(maximum.root()) && bounded.definitions().size() == kMaxQuests,
          "the exact quest and objective capacity is accepted");
    auto tooMany = parse(catalog(kMaxQuests + 1, 1));
    check(!bounded.load(tooMany.root()) && bounded.definitions().size() == kMaxQuests,
          "exceeding quest capacity fails atomically");
    ModuleContext context{};
    check(module.initialize(context), "loaded quest module initializes");
    auto replacement = parse(R"({"schemaVersion":1,"quests":[]})");
    check(module.load(replacement.root()).code == ErrorCode::InvalidState,
          "initialized quest definitions cannot be replaced");
    module.shutdown();
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
