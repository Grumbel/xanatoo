<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com> -->

# UI structure and drawing order

The desktop UI takes the classic GNOME 2 HIG as a useful baseline: predictable
window controls, visible interactive targets, restrained chrome, and consistent
keyboard/mouse behaviour. The dock/tile model is intentionally more flexible.

## Native panels and ImGui

- A panel that can be docked, detached, resized, or maximized is a `FloatWindow`.
  `float_stack` drives both painting and hit-testing; no separate ImGui title
  bar or independent ImGui placement is allowed for that panel.
- Rich forms (Reply, Bookmarks, Annotations, Nostr) use borderless ImGui
  content in the native panel client rect. All title bars, close/maximize
  buttons, docking and resizing remain native.
- Preferences is an application settings **dialog**, not a dockable work panel.
  Main menus, toolbars, context menus, tooltips, and the virtual keyboard are
  also ImGui chrome rather than `FloatWindow`s.
- A native press must not act through an ImGui popup or embedded widget:
  consult `Ui::captures_point` before native floating-window hit-tests.

## Paint order

1. Document panes, including overlays, footnotes, scrollbars and page chrome.
2. Centre tile split chrome.
3. All native floating/docked frames and client content, back to front, with
   resize grips between docked neighbours.
4. Fixed status line (on top of a floating frame that extends too far down),
   and transient docking preview / native hover tooltip.
5. Modal lightbox above native document chrome.
6. ImGui menu bar, toolbars, popups, settings dialog, and embedded panel forms,
   rendered in `Ui::end_frame` above the SDL chrome.

**Remaining integration work:** ImGui embedded panel forms are still composited
in the final ImGui pass, independently of the native `float_stack`. When a
free native window overlaps a panel with embedded ImGui widgets, its widgets
can paint above the foreground native frame. A proper solution needs a single
ordered compositor for both backends (or per-panel ImGui rendering), not
another ad-hoc SDL rectangle drawn over the text. Test that case when the
render/compositor paths can be unified.

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
and invalid persisted weight fallback; interactive SDL/ImGui overlap testing
still needs a desktop build.
