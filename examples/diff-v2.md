# Release notes — 0.3 release candidate

:author: xanatoo examples
:lang: en

This pair (`diff-v1.md` / `diff-v2.md`) is meant to be opened **side by side**
with **View → Diff Marks** (or **Examples → Diff demo**). Orange bars mark
blocks that differ; word-level marks highlight changed spans inside similar
paragraphs.

## Shipping checklist

- [x] Dual-pane open from the command line
- [x] Status-bar path prompts
- [x] Structure highlight colours
- [x] Help window (F1)
- [x] Thread / Message markup for mailing-list archives
- [ ] Word-level diff on very large corpora

## Behaviour

Caret blink uses a 500 ms duty cycle. Soft windows cascade when several
documents are opened from the Examples menu.

Transclusion targets are resolved by stable labels first, then by heading
text as a fallback. Cross-document includes stay read-only mirrors.

## Performance notes

The block LCS is quadratic in the number of text nodes. For handbook-sized
files this stays interactive; megabyte pastes should be split first.

## Quotes from review

> The orange gutter is enough for review; ship a unified view only if users
> ask for it twice.

> Word marks on similar paragraphs are the real win.

## Closing

RC for the 0.3 branch — numbers below are freeze candidates.

Version: 0.3.0-rc.1
