# Keyboard shortcuts

The current set as of September 2026. Binds live in two places: the Novara Canvas/File/Edit/Trim/Timeline/Clip/Mark/View/Playback menus (ShellMenus.cpp) and the raw key handler on MainWindow (AppActions.cpp). Some calls like Undo/Redo and Play/Pause exist in both; they do the same thing either way. A few menu entries advertise a shortcut but are not wired to a handler yet — those are called out below.

A note on modifiers: "CTRL" means the Control key (on Linux that's Ctrl). Where a key is shown bare (like `M`) it works without any modifier. The Shift-combined frame-step uses a whole second's worth of frames — whatever the project FPS is.

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

The Mark menu also lists Mark In (`I`), Mark Out (`O`), and Clear In/Out (`ALT+X`), and the transport bar has Mark In / Mark Out buttons, but none of them are wired to a handler yet — the 3-point editing GUI (source/timeline marks) is still pending (`docs/PHASE1.md`, E2). Bare `I` is also assigned to the View > Inspector toggle, so the shortcut is ambiguous between a working action and the unimplemented one; one of the two should move.

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

These place whatever is selected in the media pool with a specific insert mode:

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
| `I` | Toggle the Inspector dock (see the Mark In collision above) |

## What is not bound yet

- Slip / slide trimming, ripple vs rolling cut tools as dedicated keys.
- Zone-based playback (in to out, in to start, etc.).
- An explicit "go to bookmark" or marker-jump key (H in Resolve). Bookmark is create-only right now.
- Per-track solo/mute/lock keyboard toggles (they are click-only in the header).
- Viewer zoom-fit / 100% keys.