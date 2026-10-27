# Footnotes

A footnote mark[^1] sits in the running text; the note itself lives in a
`Footnote` block, here collected at the end of the page. Click a mark to
jump to its note, or hover it for a preview.

Turn on **View → Footnote overlay** and scroll: the notes of the marks that
are on screen appear in a strip at the bottom of the pane, so you can read
them without leaving the passage.[^2] Clicking a note in the strip jumps to
it.

## Writing them

In Markdown a mark is `[^key]` and the note is a block starting with
`[^key]:`. Keys are labels, not numbers: the mark shows the key, so short
numeric keys read best.[^3] A note may span several paragraphs when the
following ones are indented by four spaces.[^4]

Org files use `[fn:key]`, the S-expression format `(fnref "key")` and
`(footnote :key "key" …)`.

## Why notes and not stretchtext?

Stretchtext expands a passage in place and so changes the text being read.
A footnote keeps the main line intact and puts the aside next to it — the
reader decides whether to look.[^5] Both are ways of layering a document;
xanatoo keeps them as separate node kinds so either can be shown, hidden or
moved without touching the other.

## Filler to scroll past

Lorem ipsum dolor sit amet, consectetur adipiscing elit. Integer posuere
erat a ante venenatis dapibus posuere velit aliquet. Cras mattis consectetur
purus sit amet fermentum.

Vestibulum id ligula porta felis euismod semper. Maecenas sed diam eget
risus varius blandit sit amet non magna. Donec ullamcorper nulla non metus
auctor fringilla.

Nullam quis risus eget urna mollis ornare vel eu leo. Etiam porta sem
malesuada magna mollis euismod. Aenean lacinia bibendum nulla sed
consectetur.

Sed posuere consectetur est at lobortis. Duis mollis, est non commodo
luctus, nisi erat porttitor ligula, eget lacinia odio sem nec elit.

Curabitur blandit tempus porttitor. Praesent commodo cursus magna, vel
scelerisque nisl consectetur et. Morbi leo risus, porta ac consectetur ac,
vestibulum at eros.

## Notes

[^1]: Like this one. Each note is addressable as `#fn:key`, so other
documents can link straight to it.

[^2]: The strip takes at most 40 % of the pane. A note that is already on
screen, or one that would cover the caret, is left out.

[^3]: `[^note-on-keys]` works too, but then the mark reads
"note-on-keys".

[^4]: This is the first paragraph of a longer note.

    And this is the second, indented by four spaces in the source.

[^5]: Ted Nelson's stretchtext and the classic footnote both answer the
same question — how much detail to show — from opposite ends.
