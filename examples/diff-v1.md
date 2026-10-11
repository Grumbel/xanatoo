# Release notes — 0.3 draft

:author: xanatoo examples
:lang: en

This pair (`diff-v1.md` / `diff-v2.md`) is meant to be opened **side by side**
with **View → Diff Marks** (or **Examples → Diff demo**). Orange bars mark
blocks that differ; word-level marks highlight changed spans inside similar
paragraphs.

## Shipping checklist

- [ ] Dual-pane open from the command line
- [ ] Status-bar path prompts
- [ ] Structure highlight colours
- [ ] Help window (F1)

## Behaviour

Caret blink uses a 600 ms duty cycle. Soft windows cascade when several
documents are opened from the Examples menu.

Transclusion targets are resolved by stable labels first, then by heading
text as a fallback.

## Known issues

1. Diff marks require two independent buffers (not a shared dual view of one
   file).
2. Very large documents make the LCS step noticeable.

## Quotes from review

> The orange gutter is enough; we do not need a separate unified view yet.

## Closing

Draft for the 0.3 branch only — do not treat numbers as final.
