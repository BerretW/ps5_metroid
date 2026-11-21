# Narrative and dialogue

`EchoFramework::Narrative` provides a bounded dialogue graph with stable content IDs, optional state conditions, and action dispatch through `ActionRegistry`. It is independent of SDL, rendering, audio, and platform APIs.

## Limits and lifetime

- One `DialogueModule` registers up to 32 definitions.
- A definition has 1–64 nodes; each node has at most 8 choices.
- One `DialogueDocument` owns one JSON definition. It accepts at most 8 conditional choices and 128 total action parameters (at most 16 per choice), in addition to the shared 1 MiB JSON input limit.
- Dialogue IDs use the shared `namespace:type/name` content-ID format. Node and choice IDs are local ASCII identifiers.
- Speaker text is limited to 128 bytes, line text to 2 KiB, and choice labels to 512 bytes. All visible text must be valid UTF-8.
- A node may have choices and a `nextNode` fallback. `advance(state)` follows the fallback only when no choice is currently visible; without choices it is the ordinary linear transition. Empty targets end the session. Graph loops are allowed.
- Static definitions, their arrays/spans, choice conditions, action requests, and referenced strings must remain immutable and alive for the module/session lifetime.

`DialogueDocument::parse` reads one definition using schema version 1. Its non-copyable, non-movable object owns the parsed JSON strings, node/choice arrays, condition trees, and action parameters. Keep it alive and unchanged for as long as a module or session references its `definition()`; a successfully loaded document cannot be parsed again. Register the returned definition before passing the module to `ModuleRegistry`. Initialization validates the complete catalog; shutdown prevents new sessions but does not mutate the definitions. A session exposes the current line and only choices whose optional `Condition` evaluates true. Selecting a choice may invoke a compiled action. The transition happens only if the action succeeds; action handlers are responsible for any state they mutate and are not transactionally rolled back by the dialogue module.

## JSON format

Each file describes exactly one dialogue. `schemaVersion` is the format version; `version` is the content revision. Nodes require `id`, `speaker`, and `text`. `speakerCs` and `textCs` optionally provide Czech translations; `nextNode` and `choices` are also optional. Choices require `id`, `label`, and `targetNode`; `labelCs` optionally provides a Czech translation. Optional `visibleWhen` uses the shared [condition format](CONDITIONS.md), and optional `onSelect` names a compiled action with typed parameters.

```json
{
  "schemaVersion": 1,
  "id": "echo:dialogue/engineer",
  "version": 1,
  "startNode": "start",
  "nodes": [
    {
      "id": "start",
      "speaker": "Engineer",
      "speakerCs": "INŽENÝRKA",
      "text": "The lab is locked.",
      "textCs": "LABORATOŘ JE ZAMČENÁ.",
      "choices": [
        {
          "id": "take_key",
          "label": "Take the key",
          "labelCs": "VEZMI SI KLÍČ",
          "targetNode": "after_key",
          "visibleWhen": {
            "op": "exists",
            "scope": "global",
            "key": "story.generator_repaired"
          },
          "onSelect": {
            "id": "echo.action.set_flag",
            "parameters": [
              {"name": "key", "value": "story.maintenance_key"},
              {"name": "value", "value": true}
            ]
          }
        }
      ]
    },
    {"id": "after_key", "speaker": "Engineer", "text": "Good luck."}
  ]
}
```

The loader validates schema/content versions, graph targets, UTF-8 and existing graph limits before exposing a definition. Unknown or duplicate object fields, including condition fields, are rejected; an explicitly supplied action needs a nonempty namespaced ID. English fields remain required, so older dialogue files are unchanged. When the selected language is Czech but a translation is empty or absent, the session returns the English string.

An initialized module must contain at least one definition. Nodes with no visible conditional choices may follow their `nextNode` fallback via `advance(state)`; nodes with visible choices must be resolved with `select`.

The module does not persist conversation progress or execute scripts from content. Static definitions remain supported. See the [headless example](../../framework/examples/narrative_demo/main.cpp) for the lifecycle and [narrative tests](../../framework/tests/narrative_tests.cpp) for static and JSON-loaded definitions, conditional choices, transitions, actions, and errors.

## PROJECT ECHO integration

The reusable module is selectable with `ECHO_FRAMEWORK_ENABLE_NARRATIVE=ON` and depends on `ECHO_FRAMEWORK_ENABLE_DATA`. PROJECT ECHO requires and links both. Startup loads [game configuration](../../assets/framework/game.json), [Mira's dialogue](../../assets/framework/engineer.json), and, when selected, the [item catalog](../../assets/framework/items.json), each limited to 16 KiB. It fails explicitly if required content or modules are invalid. The game projects ownership into `inventory.maintenance_key`; its compiled `echo.inventory.grant` action accepts only compiled project items and resolves matching definitions in the selected inventory module. Content cannot execute scripts or select arbitrary actions.

Mira's content revision 6 includes Echo Step advice, conditional Health Cell and
unique Kinetic Coil offers. Conditions reflect authoritative health and ownership
across backpack, equipment and active compartments; storing the Coil unequipped
does not enable another grant. New cells enter the backpack and Mira instructs
the player to move them to Active before use. H/R1 consumes one active cell and
heals up to two health. The Coil auto-equips into an empty Body slot.

The current dialogue panel supports single-line ASCII letters, digits, space, `+`, `%`, `:`, `-`, `.`, `/`, and the Czech accented letters rendered by its bitmap font. Speaker names may contain at most 39 Unicode code points, lines 80, and choices 72; the adapter rejects malformed UTF-8, unsupported glyphs, or oversized text. PROJECT ECHO's Options screen selects EN/CS, and the choice is stored with existing settings; old settings without a language field default to English.

Conversation position is not saved. The existing autosave format and inventory remain authoritative, so Continue restarts any conversation and reflects whether the key and Echo Step are owned. See [controls](../CONTROLS.md). A previous controller package worked on PS5, but this dialogue content revision requires a new hardware test.
