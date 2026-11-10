<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com> -->

# UI structure and drawing order

The desktop UI takes the classic GNOME 2 HIG as a useful baseline: predictable
window controls, visible interactive targets, restrained chrome, and consistent
keyboard/mouse behaviour. The dock/tile model is intentionally more flexible.

## Navigation and layout menus

- **View → View Source** switches the current pane between the rendered
  document, raw source, S-expression and XML. It reuses the loaded document
  (including database-native and unsaved documents) instead of opening a
  second page or fetching the remote URL again. Source representations are
  read-only pane-local projections, not edits to the underlying buffer.
- **Layout** holds page columns, collection layout, split/tile arrangement,
  margins, text width, line spacing, justification and style sheets. **View**
  holds reading modes, document/source representations and viewing overlays;
  edit-only **Format** remains reserved for document formatting actions.
- Right-click a document **tab** to select it, move it into a document window
  or close it. Right-click a document **window title** to move that *specific*
  window back to a tab, even when another window has focus. Moving a view
  preserves its own navigation history, source projection, scroll and selection.
  Utility panels (Bookmarks, Styles, etc.) cannot be converted into tabs.
- The centre tile-divider badge/right-click menu offers **Swap Panes** and
  **Join Left/Right** (or **Top/Bottom** in stacked mode). Join collapses the
  split while retaining the selected side's view and history. A Trail hover
  preview is reverted first; pending fetches for the discarded side stop.

## Toolbar states

- Ordinary toolbar actions have no button frame until hover; they rise on
  hover and sink only while pressed. Toggle actions remain recessed until
  deactivated, including the browse mode, native view, outline, history,
  downloads and the contextual reader/navigation/site-map buttons.
- **Split Panes** becomes **Join Panes** with a different icon while the
  independent two-pane arrangement is active; that toolbar action keeps the
  left/top pane when joining. For an explicit choice use the divider's
  **Join Left/Right** (or Top/Bottom) context menu.
- The bookmark button uses an outlined ribbon when the current address is
  unbookmarked, and a filled ribbon plus sunken face when bookmarked. A
  bookmark on *another passage* of the current page does not latch the
  button. Reload uses an opposing-arrows icon, distinct from Stop.

## External URL drops

- Dropping a URL from another application navigates the view directly under
  the pointer. With two centre panes, either side can receive the drop;
  linked Trail navigation does not redirect it to the other pane. A document
  floating window also accepts drops, while utility panels block drops onto
  pages behind them.
- SDL text drops accept plain URLs and the first URL in a `text/uri-list`
  (ignoring blank/comment lines). A `file://` URL to a local path is decoded
  and opened as a document. Regular file drops still open documents in
  windows, and dropping images onto a document still inserts at its caret.
- SDL3 supplies exact coordinates for the drop. SDL2's drop event lacks them,
  so the compatibility path uses the mouse position at the time of dropping.

## Native panels and ImGui

- A panel that can be docked, detached, resized, or maximized is a `FloatWindow`.
  `float_stack` drives both painting and hit-testing; no separate ImGui title
  bar or independent ImGui placement is allowed for that panel.
- Rich forms (Reply, Bookmarks, Annotations, Nostr, Lemmy) use borderless ImGui
  content in the native panel client rect. All title bars, close/maximize
  buttons, docking and resizing remain native.
- Preferences is an application settings **dialog**, not a dockable work panel.
  Main menus, toolbars, context menus, tooltips, and the virtual keyboard are
  also ImGui chrome rather than `FloatWindow`s.
- Native mouse/touch routing consults `Ui::captures_point` with the frontmost
  `FloatWindow` at the pointer. A hidden embedded widget cannot intercept a
  frame/title click on a native-only panel or on another embedded panel.
  Popups and dialogs remain above all native frames.

## Paint order

1. Document panes, including overlays, footnotes, scrollbars and page chrome.
2. Centre tile split chrome.
3. All native floating/docked frames and client content, back to front. Each
   embedded ImGui form is submitted immediately after its owning native frame;
   nested ImGui child windows remain part of that form. Resize grips between
   docked neighbours are drawn after the panels.
4. Fixed status line (on top of a floating frame that extends too far down),
   and transient docking preview / native hover tooltip.
5. Modal lightbox above native document chrome.
6. ImGui menu bar, toolbars, popups, settings dialog, and other non-panel
   draw lists, rendered in `Ui::end_frame` above the SDL chrome.

`Ui::prepare_render` finalizes ImGui draw lists after `run_chrome`, before
`draw_floats`. The `ui_panel_layer` classifier recognizes the six embedded
panel roots and their child windows. Tooltips and popups remain foreground
ImGui windows. Each modern SDL2/SDL3 render pass receives only its selected
draw lists, preserving their native FloatWindow z-order. The old SDL2
per-triangle fallback, which caches a whole UI frame as one texture, retains
single-pass rendering for compatibility; its embedded controls can still
overlap foreground native frames.

## Pointer ownership and input stacking

- Every UI frame synchronizes the embedded ImGui root windows **and their
  children** with native `float_stack` back-to-front order. Non-panel ImGui
  chrome retains the foreground layer.
- The frontmost native role under the pointer is compared with the ImGui
  window from the previous frame's hit-test. When they disagree, an obscured
  embedded editor cannot receive mouse-down, motion, or wheel events. The
  next ImGui frame masks that mouse position as unavailable to suppress
  hover/tooltips over the hidden form. Mouse-up still reaches ImGui so an
  interrupted widget drag cannot leave its button stuck.
- Touch and native mouse cursor selection use the same ownership check as
  mouse presses. A gamepad-driven pointer is likewise masked while its native
  foreground window hides a form.

The vendored-ImGui regression exercises two overlapping native panel bodies,
each with a child, and swaps their stacking order without focusing them. It
also verifies that standalone chrome windows stay out of the panel layers.
Desktop checks should still cover actual mouse down/up, wheel and cursor
transitions while dragging floating windows over Reply/Bookmarks/Nostr/Lemmy, plus
the SDL2 triangle-renderer fallback (which still uses one draw pass).

## Resize affordances

- Tile split: horizontal resize pointer for side-by-side panes, vertical for
  stacked panes. The Compare overview remains a scroll handle, not a resize
  handle; the divider badge opens the tile menu.
- Dock edge: horizontal or vertical pointer at the border facing the document.
- Two panels sharing a dock: a small dotted grip at the internal separator.
  Dragging changes the two neighbours' relative lengths; other panels and
  the outer dock width/height do not change. The shares are saved in the
  session and scale with the main window.
- Undocked window: its right/bottom edges and lower-right corner show the
  matching resize pointer. Resizing keeps the pointer shape until release.
- ImGui text inputs and other widgets retain their own I-beam/hand pointers;
  native pointers are never forced on top of an ImGui-owned hit area.

The pure `dock_slices_tests` exercise distribution, resizing, minimum lengths
and invalid persisted weight fallback; `ui_panel_layers_tests` verifies exact
embedded-window/child classification and excludes popups and chrome. A second
test, `ui_panel_imgui_tests`, produces actual vendored ImGui draw lists and
checks parent, child and dialog classifications along with overlapping window
input order. Interactive SDL/ImGui overlap testing still needs a desktop build.
