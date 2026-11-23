# Quests

`EchoFramework::Quests` is a statically linked, optional module (`echo.rpg.quests`).
It loads version-1 linear or version-2 branched JSON into owned fixed-capacity
storage before initialization: up to eight quests, each with up to eight ordered
objectives and four branch choices. A quest has a
stable `namespace:quest/name` ID. Each objective has a unique local ID, a
unique `namespace:fact/name` ID, and nonempty text. IDs and text have bounded
storage; unknown or duplicate fields, duplicates, and invalid content are
rejected. Failed loads leave the previous catalog intact.

The host supplies the set of facts currently true and the explicitly selected
branch ID. `evaluateQuest` returns the first unmet objective or a bounded set of
choices, plus the number of consecutive completed steps. Facts later in the
sequence do not skip earlier objectives. Branch entries may name a fact, a
bounded objective string, and a catalog item/quantity reward; the module
validates and stores that data but never grants the reward itself. Applications
must supply authoritative facts and persist selection/claim state if progress
must survive reloads. Returned views and definition pointers remain valid only
while the module and its loaded catalog are alive and unchanged; do not reload
while consumers retain them.

PROJECT ECHO selects the module and loads its main quest from `game.json`. After the Maintenance Key, the player selects
either the Kinetic Coil route or the Lab Sentry route, then continues through
Echo Step to the Central Chamber exit. The selected route grants a Shield Cell
or Med Kit once the quest is complete. A full backpack leaves the reward
pending; making room and continuing play claims it without duplication. The
HUD displays objectives and route choices. The project validates route facts,
reward resources, and the bitmap font's text bounds.

The route choice and reward-claimed flag are stored in autosave format 8.
Formats 1–7 migrate explicitly; the version-one migration still grants the
Maintenance Key to preserve its previous unlocked-route behavior. Reward
insertion is bounded by the compiled item catalog and inventory stack limits.
Branch `rewardItem` and `rewardQuantity` come from quest JSON rather than a
fixed project reward table. Each reward must refer to a supported catalog item
and have a positive quantity no greater than its stack limit. If a unique
item (stack limit 1) is already present in the backpack, equipment or active
slot, the reward remains pending rather than creating a duplicate. Pending
rewards use the currently loaded definition; already-claimed rewards are not
granted again when the data changes. The main quest may reorder its three
supported facts and move the branch point, provided the key objective precedes
the choice and the exit objective follows it. These restrictions preserve
existing save validation and place the choice before the exit in quest
evaluation. World layouts must also keep the exit inaccessible until the
pre-choice facts can be obtained: game completion stops further choice input.
The project still fixes quest IDs, supported facts and route-to-save-slot mapping.

This is the first branching quest milestone, not a complete quest system.
Quest acceptance and independent journals remain future work; see
[cutscenes](CUTSCENES.md) for the separate linear scene module.
