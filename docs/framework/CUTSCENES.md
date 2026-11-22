# Cutscenes

`EchoFramework::Cutscenes` is a statically linked optional module
(`echo.narrative.cutscenes`). It owns version-1 JSON definitions in bounded
storage: at most eight scenes with at most eight cards each. Every card has
bounded English and Czech speaker/text fields. Unknown, missing, duplicate,
oversized, or malformed fields are rejected, and failed loads preserve the
previous definitions.

`CutscenePlayer` is a small deterministic state machine. The host starts a scene
using an immutable definition, advances one card per explicit call, and resets
or checks playback without allocation. The definition must outlive playback;
do not reload the module while a player refers to one of its definitions.
Playback has no implicit timer, input, rendering, or save behavior. The game
adapter decides when to start and advance a scene.

PROJECT ECHO starts `echo:cutscene/central_signal` when the player enters the
Central Chamber. While a card is shown, fixed-step simulation is paused; the
interact action advances to the next card. The game uses its existing EN/CS
language setting and displays the card in a bounded overlay. Playback is not
stored in autosaves: restoring directly into a room does not replay the scene,
but entering the chamber again during the same run will replay it. This scene
does not grant rewards or change progression state.

The project content validator and startup adapter check the required scene and
its localized text against the bitmap font's character and line limits. The
module is a linear presentation primitive, not a branching dialogue or a
timeline/camera system.
