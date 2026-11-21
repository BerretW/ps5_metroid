# Quests

`EchoFramework::Quests` is a statically linked, optional module (`echo.rpg.quests`).
It loads version-1 JSON into owned fixed-capacity storage before initialization:
up to eight quests, each with up to eight ordered objectives. A quest has a
stable `namespace:quest/name` ID. Each objective has a unique local ID, a
unique `namespace:fact/name` ID, and nonempty text. IDs and text have bounded
storage; unknown or duplicate fields, duplicates, and invalid content are
rejected. Failed loads leave the previous catalog intact.

The host supplies the set of facts currently true. `evaluateQuest` returns the
first objective whose fact is absent and the number of consecutive completed
objectives. Facts later in the sequence do not skip earlier objectives. The
module does not own facts, grant rewards, persist progress, accept quests, or
execute data as code. Applications must supply authoritative, persistent facts
if they want completed progress to survive reloads. Returned views and
definition pointers remain valid only while the module and its loaded catalog
are alive and unchanged; do not reload while consumers retain them.

PROJECT ECHO selects the module and loads its main quest from `game.json`. Its
three compiled fact sources are the Maintenance Key, Echo Step, and reaching
the Central Chamber exit. The current objective is displayed in the HUD; the
project validates these facts and the bitmap font's 48-character display limit.
Quest progress is derived from existing checkpoint fields, so no additional save
fields, migration, or rewards are needed. Earlier saves retain their existing
migration rules, including the version-one Maintenance Key grant.

This is the first quest milestone, not a complete quest or cutscene system:
branching, quest acceptance, rewards, independent journals, and scripted
cutscenes remain future work.
