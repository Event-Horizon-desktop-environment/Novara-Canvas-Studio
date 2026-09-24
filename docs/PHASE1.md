# Phase 1 — Low-hanging fruit (S)

Phase 1 of `roadmap.md`'s difficulty ramp: the easiest, mostly-self-contained wins.
This file is the **working todo list + acceptance criteria** for the phase. Rule of
the phase: *every item lands as a headless law + regression test in `canvas_core`
before its GUI wiring is considered done, and the tree stays warning-free.*

Status legend: [ ] todo · [~] in progress · [x] done · [R] regression-tested.

---

## Rider — `-Werror` on all targets

- [x] `-Werror` lands on `canvas_core` (PUBLIC `-Wall -Wextra -Wpedantic`),
      which propagates to every consumer — the GUI app, core tests, headless/Qt
      tests, and the Vulkan tests. All targets now carry it on GNU/Clang.
- [x] `./build.sh` Release + Debug rebuilds stay warning-free during this phase.
- [x] **Test:** existing suite only (84 tests today; 82/84 pass, the 2 Vulkan
  SKIP-as-fail WIP stubs are not regressions). Guard: `ctest -N` count is
  re-checked before quoting.

---

## E2 — 3-point editing + overwrite/insert modes

Core machinery already exists: `place_clip(..., Placement::{Overwrite, Insert,
AppendAtEnd, PlaceOnTop})` + `place_linked_clip` + the `media_fps` → timeline
duration law (`tl_out = tl_in + llround(src_span * seq.fps / media_fps)`).
What's missing is a named 3-point law + test so the GUI wiring (mark in/mark out
on source, mark in on timeline) never drifts from it.

- [x] `timeline/three_point.hpp` — headless 3-point law: `Marks{src_in, src_out,
      tl_in}`, `timeline_duration(seq_fps, media_fps)`, `make_clip(media, marks,
      seq_fps, media_fps)`, plus `resolve(ResolveInput)` — turns raw mark state
      (unset = -1, inverted windows, playhead fallback, timeline-in/out sizing,
      clamping to the media) into a well-formed `Marks`.
- [x] Test `three_point_test` (written this phase): duration law; Insert = content
      at `>= tl_in` ripples right and nothing is lost (incl. SPLIT of a straddled
      clip — added this phase); Overwrite = overlapped clips are re-cut/replaced
      and nothing shifts; linked A/V variant; undo/redo both modes; `resolve`
      cases (unset marks → whole source at the playhead, inverted out → 1-frame
      span, clamping, timeline-out sizing across fps, negative fallbacks).
- [x] GUI wiring (this session): `ThreePointActions.cpp` — Mark In/Out/Clear
      (`I` / `O` / `Alt+X`, focus rule: source monitor when it has focus — it is
      focused automatically when media is opened — else the timeline), the
      transport-bar Mark buttons, `,` / `.` Insert / Overwrite from Source, and
      `E` / `F9` / `F10` / `F12` now route through `three_point_place` (source
      monitor wins, media-pool selection otherwise; marks only apply to the
      source monitor's media). One `place_linked_clip` command = one undo step;
      `place_media_at` gained a source-range parameter so the drop path and the
      3-point path share one placement. Timeline draws its in/out range as a
      shaded ruler band (`TimelineWidget::set_marks`); the status bar shows both
      monitors' marks as timecode. Inspector toggle moved off the colliding `I`
      to `Alt+I`.
- [ ] **Accept:** a 3-point insert/overwrite lands as ONE undoable edit, honours
      the source marks, and updates the linked mate. (Mechanics + `resolve` are
      regression-pinned by `three_point_test`; the interactive pass still needs
      a manual GUI run.)

---

## E4 — markers → named ranges, export-from-range

Marker/range model is point-only today (`Bookmark{frame, label, id}`) and —
busy-bug — **project files do not persist bookmarks at all** (nothing in
`project.cpp` serializes them; a save+load silently drops every marker).

- [x] `Bookmark` gains `int64_t tl_out = 0` (0 = point marker, `> tl_in` = range).
- [x] `Sequence::add_range(in, out, label)` → `id`; keeps bookmarks sorted.
- [x] `Sequence::bookmarks_in(in, out)` — export-from-range query (start in-range).
- [x] `project.cpp` serializes `bookmarks` + `next_bookmark_id` (added to the
      save/load docs; old files without the key still load, so no version bump).
- [x] `timeline/markers.hpp/.cpp` (written this phase): `markers::chapters_from(seq)`
      → `{seconds, label}` chapter table (point marker = chapter at its frame;
      range = chapter at its start) — the headless law D3's MP4/WebM muxing will
      consume.
- [x] Test `markers_test` (written this phase): point/range add + sorted
      invariants; `bookmarks_in` window query; `chapters_from` mapping (incl.
      empty-label fallback + fps<=0); save/load round-trip of markers +
      `next_bookmark_id` (the old silent-drop bug).
- [x] GUI wiring (this session): `RenderScope::Range` appends to the scope enum
      (existing ordinals unchanged, so saved projects keep their scope); the
      Deliver scope picker gained "In/Out range", with image-scope control
      disabling now driven by `scope_is_image` instead of a hard-coded index
      (`Range` is a video scope). The enqueue path resolves the window through
      the new headless law `deliver_preset::render_range_window(mark_in,
      mark_out, playhead, timeline_frames)` and sets `RenderJob::start_frame` /
      `total_frames` — no marks = whole timeline (status bar says so), else the
      marked window clamped to the sequence. Mark → "Create Range from In/Out"
      (`Alt+R`) turns the timeline marks into a named `Sequence::add_range`
      bookmark (prompted label, feeds chapters), and the ruler now draws named
      ranges as amber bands beside the blue in/out band. Fixed en route:
      "Individual clips" jobs now carry `start_frame = clip.tl_in` (they
      previously all rendered from frame 0).
- [x] **Accept:** markers survive save/load; a named range round-trips; chapters
      derive deterministically from markers. (All three are pinned headlessly:
      `markers_test` covers the save/load round-trip incl. ranges,
      `chapters_test` the deterministic mapping, and the new `render_range`
      test the mark→window law.)

---

## E6 — track hide/mute/rename + flexible insert/reorder

Mute/solo/lock/collapse flags already exist + `set_track_muted/locked/collapsed`.
Missing: track **insert, remove, rename, reorder**. `EditCommand::apply()` asserts
a track exists at each snapshot index, so track-shape edits need a whole-kind
snapshot command.

- [x] `TrackListCommand` (whole-`std::vector<Track>` snapshots per kind) +
      `insert_track`, `remove_track` (guarded: keep ≥ 1 track of each kind),
      `rename_track`, `move_track` in `edit_ops.{hpp,cpp}`.
- [x] Test `track_ops_test` (written this phase): insert at head/middle/tail +
      undo/redo; remove with clips on the track (clips die with the track) +
      guard; rename; move reorder both directions + undo/redo.
- [ ] GUI wiring (later): timeline header + context menu uses the ops; header
      name editing via the Inspector.
- [ ] **Accept:** every track edit is one undoable step; reorder preserves clip
      contents; the last track of a kind cannot be removed.

---

## F6 — full blend-mode set (5 → 8)

The timeline has its **own** `canvas::core::BlendMode` enum in the order
`Normal, Add, Multiply, Screen, Overlay` (0–4), deliberately distinct from the
color-page `grade_graph::BlendMode` (Normal, Screen, Multiply, Overlay,
SoftLight, Add, Subtract, Difference) — a separate, differently-ordered enum that
already carries 8 modes and the float `blend_channel()` law. The timeline
renderer's uint8 switch matched the timeline enum exactly (so there was no
mis-blend bug, contrary to an earlier note). The real stragglers: `visual::
kBlendModeCount = 5` clamps new modes, the byte math is a private renderer copy,
and the timeline enum lacks SoftLight/Subtract/Difference.

- [x] `timeline/blend.hpp/.cpp` (written this phase): canonical **uint8** blend
      law `blend::blend_channel(BlendMode, float opacity, uint8 base, uint8 src)`
      for all 8 modes, in timeline-enum order, defined through the color-page
      float law (mapped by mode NAME) so the two renderers can never disagree;
      `blend_mode_name()`; `kBlendModeCount`.
- [x] `BlendMode` gains `SoftLight, Subtract, Difference` **appended** (values
      5/6/7) so existing project files keep their exact meaning — no
      reinterpretation, no version bump.
- [x] `visual::kBlendModeCount` 5 → 8 (edit_ops clamp now admits all 8).
- [x] Renderer `blit_rgba_transformed` calls `blend::blend_channel` (private
      switch deleted).
- [x] Inspector Visual composite combobox lists all 8 modes (append-only).
- [x] Test `blend_modes_test` (written this phase): per-mode known byte values;
      uint8 ≤ 1 byte vs the grade_graph float law across a value grid for all 8
      modes; opacity dissolve; name table exactness; JSON round-trip of modes 5–7.
- [ ] **Accept:** a clip set to any of the 8 modes renders identically in the
      timeline compositor and the color-page evaluator math, and round-trips JSON.

---

## D1 — EDL (CMX3600) export

- [x] `export/edl.hpp/.cpp` (written this phase): headless CMX3600 writer —
      `edl_timecode(frame, fps)` (HH:MM:SS:FF), `write_cmx3600(seq, title, media)`
      emitting `TITLE:`/`FCM: NON-DROP FRAME` header + one event per clip sorted
      by tl_in (reel = media basename, `AX` for title clips), `* FROM CLIP NAME:`
      notes.
- [x] Test `edl_test` (written this phase): header exactness, event block shape,
      timecode math at known frames, cross-track sort order, clip count.
- [x] GUI wiring (this session): File → "Export EDL..." picks a `*.edl` path via
      `QFileDialog` and writes the core writer's CMX3600 text; success/failure
      reports on the status bar.
- [ ] **Accept:** output parses as CMX3600 (a Resolve/any NLE import opens it) and
      reproduces each clip's rec + src timecodes.

---

## D2 — subtitle burn-in on export

Caption model + title rasteriser already exist (captions.cpp, title.cpp); export
burn-in is the missing glue.

- [x] Test `caption_burn_test` (written this phase): `timeline/caption_burn.hpp/.cpp`
      — shaped captions → timeline windows (ms→frames nearest rounding, fps
      fallback, degenerate windows not inverted), half-open `[start,end)`
      ownership, overlap resolution "later caption wins" (greater start → greater
      end → later index), no-owner → -1 / "".
- [ ] Export path: render captions as title sprites over their owning clips;
      Subtitle-style draw (text + optional box) reusing `title::raster_title_sprite`.
      (`caption_burn::text_at` is the selection seam the renderer will call.)
- [ ] GUI wiring (later): Deliver checkbox "Burn subtitles".
- [ ] **Accept:** every exported frame shows exactly its caption window's text,
      and the headless selection law is regression-pinned.

---

## D3 — ranges → chapters in MP4/WebM

`DeliverSettings.chapters_from_markers` already exists.

- [x] Mux law + test `chapters_test` (written this phase):
      `export/chapters.hpp/.cpp` — `container_supports()` (MP4/MOV/MKV/WebM),
      `for_export(seq, enabled)` (starts from `markers::chapters_from`, each end =
      next start, last = sequence duration, never inverted), `apply()` (install
      only when enabled AND the format supports it).
- [x] `ExportSettings` gains `std::vector<ExportChapter>`; the `RenderQueue`
      worker calls `chapters::apply(es, project->sequence,
      settings.video.chapters_from_markers)` before `export_project()`.
- [x] `exporter.cpp` attaches the table as FFmpeg `AVChapter`s (1/1000 timebase)
      before `avformat_write_header` — `avformat_free_context` owns/frees them.
- [ ] **Accept:** an mp4/webm exports with working chapter markers at the marker
      positions; non-chapter-capable containers ignore the option harmlessly.
      (Mux path compiles/links; needs a manual export to confirm playback menus.)

---

## D4 — still-frame + image-sequence export

- [x] Headless law + test `still_export_test` (written this phase):
      `RenderScope::Still` + `RenderScope::FrameSequence` appended;
      `scope_is_still`/`scope_is_sequence`/`scope_is_image`; `still_output_path`
      (PNG is canonical, supplied ext replaced, dot-in-directory not mistaken for
      one) and `sequence_output_path` (1-based 5-digit zero-padded
      `<stem>_00001.png`, index < 1 clamps).
- [x] Export path (landed 2026-09-17): `export/image_export.{hpp,cpp}` renders
      through the export `RenderSession` and writes lossless PNGs with FFmpeg's
      `png` encoder — still = one file at `start_frame`, sequence = N numbered
      files. `ExportSettings.render_scope` (enum moved to `exporter.hpp`) routes
      it from `RenderQueue::run_job`; `RenderJob::start_frame` carries the
      playhead for a still.
- [x] Test `image_export_test` (written this session): still writes a decodable
      PNG that is pixel-identical to a direct `RenderSession` render of the same
      frame; sequence writes exactly the requested numbered files and nothing
      else; progress is monotonic to 1.0; cancel fails without writing; a video
      scope is rejected. Synthetic source is native `ffv1` (no libx264 needed).
- [x] GUI wiring (this session): Deliver scope combo gained "Current frame
      (still)" and "Frame sequence"; image scopes force a `.png` output path,
      disable the format/codec/encoder/audio/chapters controls, and a still
      takes its frame from the playhead (clamped to the timeline).
- [ ] **Accept:** a still at the playhead and a bounded frame sequence export
      byte-identically under both CPU and the single-clip GPU fast path.

---

## D5 — render-queue priority + pause

- [x] Headless law + test `queue_policy_test` (written this phase):
      `export/queue_policy.hpp` — `Candidate{priority, order, queued}` +
      `next_candidate()` (highest priority, FIFO within a priority, stable
      tie-break, skips non-queued), plus `clamp_priority()` /
      `kPriorityMin`/`kPriorityMax` pinning the queue's priority range.
- [x] `RenderQueue` gains `set_paused()`/`is_paused()` (worker-loop gate; an
      in-flight render finishes; resume never drops a job), `set_priority(id,
      p)` (Queued only), and the worker dispatches via
      `queue_policy::next_candidate`. `RenderJob`/`RenderJobSnapshot` carry
      `priority`; it persists in the project (`render_jobs[].priority`).
- [x] GUI wiring (this session): queue header Pause/Resume toggle drives the
      worker gate; label + enabled state mirror the queue's own flag.
- [x] GUI wiring (this session): per-job up/down arrows in the render-queue rows
      (enabled while Queued only), driving `RenderQueue::bump_priority(id, delta)`,
      which clamps through `queue_policy::clamp_priority`.
- [ ] **Accept:** pausing never drops a job; resumes from the exact queue state.

---

## D8 — autosave + crash recovery + project archive/backup

- [x] Headless law + test `autosave_test` (written this phase):
      `project/autosave.hpp/.cpp` — `Policy{interval_seconds, max_snapshots}`,
      `snapshot_path`/`slot_from_snapshot` (`<project>.autosave-<n>`),
      `newest_slot` (recovery pick), `plan_turnover` (lowest free slot, ring
      successor when full, out-of-range/stale slots rolled off),
      `media_manifest` for the archive.
- [x] GUI wiring (this session): a QTimer at the policy interval fires a turnover
      save while the project is dirty AND file-backed (untitled skipped); `open_file()`
      offers the newest autosave slot for recovery before the manual version loads.
- [x] GUI wiring (this session): File → "Archive Project…" writes a project copy
      plus a sibling `<name>.archive.ncs.manifest.txt` built from
      `autosave::media_manifest`; the working project's save path and dirty
      state are left untouched.
- [ ] **Accept:** a crash loses at most the current autosave interval, and the
      recovery flow restores markers, tracks, and deliver settings.

---

## D9 — PSNR/SSIM QC tool

- [x] Headless laws + test `qc_test` (written this phase):
      `qc/metrics.hpp/.cpp` — `qc::Plane{data, width, height, stride}`,
      `valid()`, `psnr()`, `ssim()` over two decoded frames (stride-safe).
- [ ] Per-export QC pass hook comparing a sample of exported frames vs
      in-process reference renders.
- [ ] GUI wiring: Deliver → "QC report" summarizing min/avg PSNR + SSIM.
- [ ] **Accept:** PSNR/SSIM laws are pinned by tests before any UI shows them.

---

## A5 — loudness normalization on export

`DeliverAudioSettings.{normalize_audio, normalize_target_lufs}` already exist.

- [x] `export/loudness.hpp/.cpp` (written this phase): `normalization_gain_db(
      measured_lufs, target_lufs)` clamped to the `audio_mix` volume bounds; and
      `integrated_loudness_lufs(mono span, sample_rate)` — an R128-flavoured
      400 ms block-RMS estimator with −70 LUFS absolute gate and −10 LU relative
      gate (K-weighting filters land as a follow-up subtask on the same module).
- [x] Test `loudness_test` (written this phase): gain law exactness + clamping;
      full-scale sine ≈ −3.01 LUFS; −20 dB sine ≈ −23.01; silence → −70 → gain
      clamps to +24 dB; relative gating keeps loud content only.
- [x] Export glue (landed 2026-09-17): `loudness::Accumulator` (new, in
      `export/loudness.hpp`) streams the mix in `RenderSession::audio_chunk`
      sized pieces from a separate probe `RenderSession`, reusing the exact
      block/gate math of the mono law — chunked feeding is asserted equal to
      the one-shot estimator, and `integrated_loudness_lufs_interleaved`
      handles multi-channel. `export_project` measures first (phase
      `"Analyze"`), converts `normalize_target_lufs` → one constant gain, and
      applies it to every chunk it mixes into the encoder; `to_export_settings`
      carries `normalize_audio`/`normalize_target_lufs` into `ExportSettings`.
      Parallel chunks are skipped while normalization is on (each chunk would
      re-measure its own segment), like the chapters guard.
- [x] GUI wiring (this session): Deliver Audio tab gained a "Normalize
      Loudness" checkbox + "Target LUFS" spin (−40..−5, default −23); the spin
      follows the checkbox, and both are disabled for image scopes.
- [x] **Accept:** test `loudness_normalize` builds a synthetic tone source
      (native ffv1 + pcm_s16le), exports it twice and measures the muxed
      output with the module's own estimator: raw = −23.01 LUFS, normalized
      = −20.01 LUFS against a −20 target (±1 LU), and the delta equals the
      expected gain. Headless laws were pinned before the glue landed.
- [ ] Follow-up (module stays headless): true BS.1770 K-weighting stages +
      stereo/surround channel weighting.

---

## U2 — action search box

- [x] Headless law + test `actions_index_test` (written this phase):
      `actions/action_registry.hpp/.cpp` — `Action{id, title, keywords,
      shortcut, category}`, `Registry` (dedup-by-id replaces in place),
      `search(query, limit)` and the exposed `score_action` law (title
      exact/prefix/substring > keyword prefix/substring > category, stable
      tie-break, empty query = all).
- [x] GUI wiring (this session): Ctrl+K "Find Action…" overlay
      (`UX/ActionSearch.{hpp,cpp}`) walks the live QMenuBar into the registry
      (title = mnemonic-stripped label, category = owning menu, shortcut hint),
      ranks with the shared `Registry::search`, and Enter triggers the real
      QAction — the menu bar and the box share one source.
- [ ] **Accept:** every main-menu action is reachable by typing 2–3 chars, and
      the registry is the single source the menu bar and the search box share.

---

## Phase exit criteria

- [ ] Every item above is [x] with its test target green in `build/` (and
      `build-release/` once re-enabled).
- [x] `ctest --test-dir build` passes 82/84 (the 2 Vulkan SKIP-as-fail WIP stubs
      excluded, not regressions).
- [x] `./scripts/check_qtdep.sh` passes (new modules add records if needed —
      none of the new modules touch Qt).
- [x] README/CURRENT-STATE/FEATURES test-count and feature claims re-verified.