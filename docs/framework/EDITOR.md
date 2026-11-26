# PROJECT ECHO map workspace

The native Windows editor modifies existing PROJECT ECHO rooms through a tile
grid, and offers a secondary property inspector. It is an initial map
workspace, not yet a full visual scene or UI-layout editor. Lua scripts are not
connected to this tool or to the game.

1. Build with `.\scripts\build-windows.ps1 -RunTests`.
2. Run `.\build\windows\echo_editor.exe`.
3. Open a room such as `assets\world\rooms\landing_bay.json`. Select a
   layer and brush, then click or drag on the map. **Undo tile** reverts the
   most recent cell change. Arrow keys move the grid cursor; Space paints.
   The scrollbars navigate larger rooms.
4. Choose **Save** and confirm to replace the opened file. The editor validates
   the complete world and game layout before saving. To edit other properties,
   open `assets\framework\game.json` for attributes and quest text, or
   `assets\world\objects\enemy_stalker.json` for stalker tuning. The same game
   file exposes bounded script-source fields for the enemy-AI and
   UI-visibility authoring prototypes.
5. Rebuild with `.\scripts\build-windows.ps1`, then run
   `.\build\windows\project_echo_vulkan.exe`. The build copies source assets
   beside the game executable. Changes to a running game require a restart;
   existing save data can affect what is visible, so use a new game when
   checking starting values.

The map grid uses symbols and colors to distinguish decoration, solid tiles and
hazards; it does not display the actual tileset artwork or the game's
isometric projection. Only layers defined directly in the opened room can be
painted. Object placement, tileset authoring, zoom and multi-cell undo are not
implemented.

The editor validates the edited fields using the framework's content parsers
and game quest constraints. It rejects out-of-range values, unsupported quest
font glyphs, duplicate JSON members and files beyond runtime size limits.
Unedited JSON fields are retained semantically, although formatting changes.
The save operation checks for changes since opening, writes a new temporary
file beside the selected file and replaces the original only after confirmation.
Do not edit the same file concurrently with another program: the conflict check
is not a cross-process lock. Keep the source files under version control.

These are only specific fields of the current game. Equipment modifiers and
room-placement overrides can affect values in play. Other enemy types, adding
entities or rooms, editing UI layouts and behavior scripts are not supported yet.
The editor does not deploy to PS5; console packaging and hardware checks remain
separate.
