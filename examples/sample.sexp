; xanatoo experimental S-expression document
; Open with: xanatoo examples/sample.sexp
; Save as .sexp / .glyph / .ss to keep this form.

(doc
  (h 1 "S-expression documents")
  (p "This is the same document model as the Markdown dialect, "
     "stored with unambiguous nesting. Inline styles are explicit: "
     (em "emphasis") ", " (strong "strong") ", " (code "code") ", and "
     (a "#Living form" "links") ".")

  (stretch :summary "Why s-expressions?" :expanded #f
    (p "Containers, attributes, and nesting are structural rather than "
       "inferred from blank lines and indentation. Round-trips stay faithful "
       "because the grammar is a tree, not a line state machine."))

  (columns
    (column :weight 1.1
      (h 2 "Original")
      (p "A passage in a weighted column."))
    (column
      (h 2 "Commentary")
      (p "Annotations sit beside the original without layout guessing.")))

  (h 2 "Living form")
  (p "Lists, quotes, rules, images, and includes work the same as in "
     "the interactive editor.")

  (ul
    (li "Unordered item")
    (li "With a nested list"
      (ul
        (li "Child bullet"))))

  (ol
    (li 1 "First ordered step")
    (li 2 "Second ordered step"))

  (quote "A document is not just something you render and read.")

  (hr)

  (include "#Living form")

  (img "network.png" :alt "Network diagram" :scale 0.85)
)
