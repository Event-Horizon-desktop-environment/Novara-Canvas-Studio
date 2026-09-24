# Keyboard shortcuts

The current set as of September 2026. Shortcuts live in one active keymap (`features/shortcuts`): presets plus custom overrides persist in QSettings (`shortcuts/preset`, `shortcuts/custom/...`) and apply to the menus, the Ctrl+K command palette, and the main key handler. Open `Edit > Keyboard Customization...` (`Ctrl+Alt+K`) for the Resolve-style editor: visual keyboard, Active Key panel, searchable command tree, and five presets. DaVinci Resolve is the default preset.

A note on modifiers: "CTRL" means the Control key (on Linux that's Ctrl). Where a key is shown bare (like `M`) it works without any modifier. The Shift-combined frame-step uses a whole second's worth of frames — whatever the project FPS is. Preset tables translate macOS `Cmd` to Linux `Ctrl`.

## Keymap presets

| Preset | Notable differences from the Resolve default |
| --- | --- |
| DaVinci Resolve (default) | `D` enable/disable; `Backspace` lift, `Shift+Backspace` ripple delete; `Ctrl+Backslash` split; `F9`/`F10`/`Shift+F12`/`F12` insert/overwrite/append/place-on-top (`,`/`.` also insert/overwrite); `I`/`O` marks. |
| Adobe Premiere Pro | New Project `Ctrl+Alt+N`; Add Edit `Ctrl+K` (Find Action moves to `Ctrl+Shift+K`); Insert `,` / Overwrite `.`; Lift `;`, Ripple `Shift+Delete`; Select `V`, Razor `C`; Transition `Ctrl+D`, Unlink `Ctrl+L`; Zoom `\`; Clear `Ctrl+Shift+X`; Enable `Ctrl+Shift+E`. |
| Avid Media Composer | Redo `Ctrl+R`; Splice `V`, Overwrite `B`, Lift `Z`, Extract `X`; Clear `G`; Zoom `Ctrl+/`; Import has no default key. |
| Final Cut Pro | Insert `W`, Overwrite `D`, Append `E`, Connect `Q`; Enable `V`; Split `Ctrl+B`; Lift `Delete`, Ripple `Shift+Delete`. |
| Pro Tools | Import `Ctrl+Shift+I`; Redo `Shift+Z`; Transition `F`; Clear `G`; Pause `Ctrl+Space`; Selector `F7`; Zoom `Alt+A`; insert/overwrite and I/O marks have no DAW equivalent and stay unbound. |

Preset sources checked during implementation: Resolve defaults ([shortcut.fyi](https://shortcut.fyi/davinci-resolve-shortcuts.html), [kstanchev cheat sheet](https://shortcuts.kstanchev.com/apps/davinci-resolve)), Premiere Pro ([Noble Desktop Mac reference](https://www.nobledesktop.com/shortcuts/premiere/mac)), Final Cut Pro ([KeyScreen reference](https://keyscreen.app/macos-final-cut-pro-keyboard-shortcuts) plus [Apple's shortcut guide](https://support.apple.com/guide/final-cut-pro/keyboard-shortcuts-ver90ba5929/mac)), Media Composer 2025.12 defaults ([EditorsKeys cheat sheet](https://www.editorskeys.com/blogs/news/avid-media-composer-keyboard-shortcuts-pdf-cheat-sheet)), Pro Tools ([Evercast shortcut guide](https://www.evercast.us/blog/pro-tools-shortcuts)).

## Project and files

| Keys | Action |
| --- | --- |
| `CTRL+N` | New project |
| `CTRL+O` | Open project |
| `CTRL+SHIFT+M` | Project Manager |
| `CTRL+S` | Save project |
| `CTRL+SHIFT+S` | Save project as |
| `CTRL+I` | Import media |
| `CTRL+Q` | Quit |
| `CTRL+,` | Preferences (opens the dialog; more on that below) |

The recent-files list is under File > Open Recent. The `CTRL+,` Preferences entry opens the Settings dialog (Novara Canvas > Preferences...), which groups the app-level settings: audible scrubbing, the hardware-decoder backend preference, and live accent / playhead colour overrides that persist across launches.

## Editing

| Keys | Action |
| --- | --- |
| `CTRL+Z` | Undo |
| `CTRL+SHIFT+Z` | Redo |
| `CTRL+K` | Find Action (searchable command palette) |
| `CTRL+D` | Toggle disable / enable the selected clip |
| `CTRL+T` | Add a transition on the selected clip |
| `CTRL+ALT+T` | Add a title clip |
| `DEL` | Ripple delete the selected clip |
| `SHIFT+DEL` | Lift (delete, keeping the gap) |
| `BACKSPACE` | Lift (same as SHIFT+DEL) |

`DEL` here is the timeline meaning. If the media pool has focus and items are selected, `DEL` deletes the pool items and ripple-deletes any clip selected on the timeline at once; `BACKSPACE` deletes only the pool items. When a transition bubble is selected on the timeline, `DEL` / `BACKSPACE` clear the transition rather than deleting a clip.

Declared in a menu but not wired to a handler yet: `CTRL+BACKSLASH` (Timeline > Add Edit), `CTRL+ALT+L` (Clip > Link/Unlink), and `U` (Trim > Cycle Edit Point Side). Link/unlink is reachable from the timeline right-click menu; Add Edit and cycle-edit-point have no working path at all.

## Markers and in/out points

| Keys | Action |
| --- | --- |
| `M` | Toggle a bookmark at the playhead |
| `I` | Mark In |
| `O` | Mark Out |
| `ALT+X` | Clear In/Out (both monitors) |
| `ALT+R` | Create a named range from the in/out marks |
| `,` | Insert the source range at the mark-in |
| `.` | Overwrite the source range at the mark-in |

Mark In / Mark Out are focus-dependent, Resolve-style: they hit the source monitor when it has focus and the timeline otherwise. Opening media in the source monitor focuses it, so a double-click in the pool followed by `I` / `O` marks the source range; click the timeline (or anywhere else) and `I` / `O` mark the timeline instead. The transport bar's Mark In / Mark Out buttons do the same as the keys. The timeline draws its in/out range as a shaded band on the ruler, and the status bar shows both monitors' marks as timecode.

`Alt+R` names the current timeline in/out range and stores it as a range bookmark (it saves with the project and feeds Deliver's chapters); the ruler shows ranges as amber bands. The same in/out marks drive Deliver's "In/Out range" render scope.

Insert / Overwrite resolve the marks through the headless `three_point::resolve` law: source in/out when the source monitor is what's being placed (whole clip otherwise), timeline mark-in when set and the playhead otherwise, always as a single undoable linked edit. `,` / `.` only use the source monitor's media; `F9` / `F10` / `F12` / `E` prefer the source monitor too and fall back to the media-pool selection. The Mark menu also lists these entries.

## Timeline view

| Keys | Action |
| --- | --- |
| `A` | Select tool |
| `B` | Blade tool |
| `SHIFT+Z` | Zoom to fit the timeline |
| `CTRL+SHIFT+C` | Collapse all tracks into strips |
| `CTRL+SHIFT+E` | Expand all tracks back |
| `CTRL+scroll` | Zoom the timeline in / out |
| scroll | Pan vertically; also stops playhead-follow |

The collapse/expand all actions live in the Timeline menu (Timeline > Collapse All Tracks / Expand All Tracks) and go through the same undoable command as the per-track chevrons in the headers. One Ctrl+Z undoes the whole thing.

## Playback

| Keys | Action |
| --- | --- |
| `SPACE` | Play / pause |
| `K` | Pause |
| `L` | Play |
| `J` | Step back one frame |
| `LEFT` | Step back one frame |
| `RIGHT` | Step forward one frame |
| `SHIFT+LEFT` | Step back one second |
| `SHIFT+RIGHT` | Step forward one second |
| `HOME` | Go to start |
| `END` | Go to end |

`J` and `L` here are simple frame-step / play, not the hold-to-shuttle behaviour some editors give those keys. `L` just starts playback.

## Clip placement

These place the source monitor's media when one is open (with its in/out marks — the whole clip when unmarked), otherwise whatever is selected in the media pool, always at the timeline mark-in when set and the playhead otherwise:

| Keys | Action |
| --- | --- |
| `E` | Append at end |
| `F9` | Insert |
| `F10` | Overwrite |
| `F12` | Place on top |

`INS` no longer places a clip — the key handler swallows it without doing anything, so use `F9` to insert.

## Application

| Keys | Action |
| --- | --- |
| `F11` | Toggle full screen |
| `ALT+I` | Toggle the Inspector dock |

## What is not bound yet

- Slip / slide trimming, ripple vs rolling cut tools as dedicated keys.
- Zone-based playback (in to out, in to start, etc.).
- An explicit "go to bookmark" or marker-jump key (H in Resolve). Bookmark is create-only right now.
- Per-track solo/mute/lock keyboard toggles (they are click-only in the header).
- Viewer zoom-fit / 100% keys.