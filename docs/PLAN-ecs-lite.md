# Plan: "ECS-lite" refactor of the App world

Goal: remove index-based references and the duplicated per-window /
per-prompt code in `src/app_*.cpp` without adopting an ECS framework.
Three independent phases; each ends with a green build, passing
`xanatoo_tests`, a screenshot check, and its own commit. Do them in order
(A → B → C); do not start the next phase with the previous one uncommitted.

## Ground rules

- Build: `nix develop --command bash -c 'cmake --build /tmp/xanatoo-build'`
  (run `ctest` / `./xanatoo_tests` from `/tmp/xanatoo-build`).
- Format touched `src/app_*.cpp` / `app.hpp` with `clang-format --style=Google`.
- No behaviour changes unless a step says so. If a refactor step forces one,
  record it in the commit message.
- Visual check: Xvfb + `xdotool windowfocus` (see how earlier sessions took
  screenshots: run the built binary under `Xvfb :77`, focus the window with
  `xdotool windowfocus $(xdotool search --name xanatoo | head -1)`, send
  keys, `import -window root out.png`). Verify: help (F1), outline
  (Ctrl+Shift+O), backlinks, a pop-out document window, split view
  (Ctrl+\), prompts (Ctrl+O, Ctrl+Shift+S, Ctrl+F, Ctrl+K).
- Commit trailer: `Co-Authored-By: <the executing model>`.
- Update `AGENTS.md` (source layout / rules) and `TODO.md` at the end of
  each phase.

---

## Phase A — Stable buffer IDs

Problem: `Pane::buf`, `SoftWindow::buf`, `tab_order` and many call sites use
`int` indices into `app.buffers`. Erasing/reordering shifts them; code
compensates ad hoc (`drop_height_hints_for_buf(bi)`, "Drop soft windows whose
buffer was removed / out of range" in `draw_soft_windows`).

Design:

```cpp
using BufferId = uint32_t;            // 0 = none
constexpr BufferId kNoBuffer = 0;

struct DocBuffer { BufferId id; Document doc; std::string path; bool dirty;
                   History history; };
// App:
std::vector<DocBuffer> buffers;       // dense storage, order irrelevant
BufferId next_buffer_id = 1;
std::vector<BufferId> tab_order;      // was vector<int>
```

Accessors (free functions in `app_buffers.cpp`):

- `DocBuffer* find_buf(App&, BufferId)` (nullptr if gone)
- `DocBuffer& buf(App&, BufferId)` (asserts)
- `BufferId add_buffer(App&, Document, std::string path)`
- `void remove_buffer(App&, BufferId)` — also clears panes / soft windows /
  tab_order entries referencing it (single place for that invariant).

Steps:

1. Add `id` to `DocBuffer`, `next_buffer_id`, accessors, `add_buffer`,
   `remove_buffer`. Keep old `int buf` fields for now (compile stays green).
2. Convert `Pane::buf`, `SoftWindow::buf`, `tab_order` to `BufferId`.
   Initialise panes to the first buffer's id (`app_init.cpp`). Replace
   `app.buffers[static_cast<size_t>(p.buf)]` with `buf(app, p.buf)`
   (mechanical; grep `buffers\[static_cast<size_t>`). Functions taking
   `int bi` (`buffer_tab_label`, `drop_height_hints_for_buf`,
   `allow_discard_dirty`, `find_buffer`, `ensure_buffer`, `pop_out_focused`,
   `pop_in_soft`) take/return `BufferId`.
3. `UiCommand::SelectTab` arg is a tab index (position in `tab_order`) — keep
   that, resolve via `tab_order[idx]`.
4. Replace the manual buffer erase / compensation code (CloseTab, Ctrl+W,
   soft-window close, `draw_soft_windows` erase_if) with `remove_buffer`.
5. Delete now-dead range checks (`bi < 0 || bi >= size`).

Done when: `grep -n "buffers\[" src/` only appears inside `app_buffers.cpp`.

---

## Phase B — One `Window` record for panes, soft windows and panels

Problem: `Pane`, `SoftWindow` and three `FloatPanel`s (`help`, `ol`, `bl`)
share x/y/w/h, drag, resize, scroll, scrollbar-drag, but each has its own
hit-test, drag, clamp, draw and close code in `app_events.cpp`
(`on_mousebuttondown` ~800 lines, `on_mousemotion` ~450) and `app_frame.cpp`.

Design (data only; no virtuals):

```cpp
enum class WindowRole : uint8_t { Help, Outline, Backlinks, Document };

struct FloatWindow {                 // replaces FloatPanel + SoftWindow
  WindowRole role;
  BufferId buf = kNoBuffer;          // Document role only
  std::string title;                 // override; empty → derive
  Rect rect;                         // x,y,w,h (existing int fields)
  // interaction (as in FloatPanel)
  bool dragging, scroll_dragging; int drag_dx, drag_dy, resize, resize_start_*;
  float scroll;
  // Document role extras (as in SoftWindow): Selection sel; LayoutResult
  // layout; bool needs_layout; maximized/dock/restore_*; last_title_click
};
std::vector<FloatWindow> floats;     // z-order: back() is topmost
```

`App::help`, `ol`, `bl` become entries found by role (`find_float(app,
WindowRole::Help)`), present only while open. Remove `help_mode`,
`outline_mode`(floating case), `backlinks_mode` bools **only if** every use
can be expressed as "float exists"; otherwise keep the bools and just store
geometry in the record (do not change semantics in this phase).

Steps:

1. Introduce `FloatWindow` + `find_float`, migrate `FloatPanel help/ol/bl`
   to three persistent `FloatWindow` members first (types only, same
   fields) — compile, commit-ready.
2. Merge `SoftWindow` into `FloatWindow`; `soft_wins` → `floats` filtered by
   `role == Document`. Update `app_softwin.cpp` function signatures.
3. Extract shared systems into `app_windows.cpp`:
   - `hit_test_floats(app, x, y) -> {FloatWindow*, Zone}` where
     `Zone ∈ {Title, Close, Client, ResizeE, ResizeS, ResizeSE, None}`
   - `begin_drag/update_drag/end_drag`, `clamp_float`, `raise_float`
   - `draw_float(app, FloatWindow&)` (frame via `draw_floating_frame`,
     scrollbar via `draw_client_scrollbar`, then a role-specific
     `draw_client_*` callback chosen by `switch (role)`).
4. Rewrite the four duplicated blocks in `on_mousebuttondown`,
   `on_mousemotion`, `on_mousebuttonup`, `on_mousewheel` to call the shared
   systems; keep only role-specific *actions* (outline row click, backlink
   row click, help link click, document client click) in per-role functions.
5. Draw order: iterate `floats` back→front instead of the fixed
   help→outline→backlinks→soft sequence. (This changes z-order slightly:
   record it.)

Keep out of scope: the two main `Pane`s stay as they are (fixed array of 2,
tied to split view); only share the scrollbar geometry helper with them.

Done when: no `app.ol.` / `app.bl.` / `app.help.` hit-test arithmetic is left
outside `app_windows.cpp`, and `on_mousebuttondown` shrinks below ~400 lines.

---

## Phase C — Single `Prompt` component

Problem: nine mode flags + nine buffers (`search_mode/search_query`,
`replace_*`, `save_as_mode/save_as_path`, `path_in_window_*`,
`open_file_*`, `url_bar_*`, `image_insert_*`/`image_insert_as_figure`,
`include_insert_*`, `link_edit_*`, `summary_edit_*`) → ~240 references,
repeated Esc/Backspace/Enter/Paste/status-text code in `on_keydown`,
`on_textinput`, `compute_status`, `minibuf_*`.

Design:

```cpp
enum class PromptKind : uint8_t { None, Search, Replace /*search+replace*/,
  SaveAs, OpenFile, OpenInWindow, UrlBar, InsertImage, InsertFigure,
  InsertInclude, EditLink, EditCite, EditSummary };

struct Prompt {
  PromptKind kind = PromptKind::None;
  std::string text;          // the single-line buffer (find text / path / URL …)
  std::string text2;         // replacement text (Replace only)
  bool focus_second = false; // was replace_focus
  // target for EditLink/EditCite/EditSummary:
  NodeId node = INVALID_NODE; size_t span = 0;
};
Prompt prompt;  // in App; at most one active prompt (verify below)
```

Pre-check (do first, report in commit message): confirm no code path
legitimately has two flags true at once. `search_mode` + another is the
likeliest; grep for assignments where one flag is set without clearing
others. If both can coexist, model `Search` as a separate `search`
sub-record and keep `prompt` for the rest.

Steps:

1. Add `Prompt` and helper systems in `app_prompts.cpp`:
   `open_prompt(app, kind, initial)`, `close_prompt(app)`,
   `prompt_text(app) -> std::string&`, `prompt_is_path(kind)` (minibuffer
   kinds), `prompt_label(kind)` (status label), `prompt_hint(kind)`.
2. Keep old flags as thin `bool` accessors temporarily?  **No** — do the
   migration in one pass per prompt kind, compile after each:
   Search/Replace → SaveAs → OpenFile/OpenInWindow/UrlBar → Insert* →
   Edit*.  For each kind: replace `app.<x>_mode = true; app.<x>_buf = …`
   with `open_prompt`, reads with `app.prompt.kind == K` / `prompt.text`.
3. Collapse `on_keydown`'s status-prompt branches: one `handle_prompt_key`
   (Esc → `close_prompt`; Backspace → `utf8_pop_back(prompt_text)`; Enter →
   `switch (kind)` commit function per kind, each extracted from the
   existing inline code verbatim; Ctrl+V → append clipboard sans newlines).
   `on_textinput`: append to `prompt_text` (or minibuf for path kinds).
4. `compute_status`: label/hint from `prompt_label/hint` + `prompt.kind`,
   tone `Prompt` for all non-None kinds.
5. Remove the dead flags from `App`; `AGENTS.md`: document `Prompt`.

Done when: `grep -c "_mode" src/app_events.cpp` drops by the nine prompt
flags, `on_keydown` no longer contains nine copies of the Backspace/Esc
logic, all prompts behave as before (manual checklist: each prompt opens,
types, Backspace over a multi-byte char, Esc cancels, Enter commits,
Ctrl+V pastes without newlines, Tab completion + history still work for
path prompts).

---

## Phase D (optional, only if A–C went smoothly)

- Split `draw_pane` (~750 lines) into `draw_pane_text`, `draw_pane_overlays`
  (hover affordances, drop indicator, structure marks), `draw_pane_scrollbar`.
- Move remaining context-dependent keys out of `on_keydown` into per-context
  functions (`handle_outline_key`, `handle_backlinks_key`, `handle_nav_key`).
- Consider splitting `Node` into per-kind payload components **only** with a
  separate design note first — it touches parse/serialize/layout/editor and
  is out of scope for this plan.

## Risks / watch-outs

- `Hover` and `drag_*` fields store pane indices (0/1) and `NodeId`s — those
  stay valid; only buffer and float references change.
- `history` (undo) snapshots whole documents; moving `DocBuffer` between
  vector slots must not invalidate `History` (it is value-owned; fine) —
  but never hold `DocBuffer&` across `add_buffer`/`remove_buffer`
  (vector reallocation). Re-fetch by id.
- Gamepad code synthesises SDL mouse events; mouse-handler refactors must
  keep `me.button.*` / `motion.*` semantics unchanged.
- Soft-window `Escape` pops the topmost float; keep that after the merge.
