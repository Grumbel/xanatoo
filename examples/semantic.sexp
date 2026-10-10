(doc
  :title "Semantic hypertext sketch"
  :lang "en"
  :author "glyphtoo"

  (h 1 :label "top" "Semantic form, not page geometry")

  (p "This document is an "
     (strong "S-expression tree")
     " of roles and identity. Layout is a view over it.")

  (h 2 :label "identity" "Stable identity")

  (p "Headings and blocks can carry "
     (code ":label")
     " so links stay valid when wording changes. See "
     (a "#identity" "this section")
     ".")

  (h 2 :label "disclosure" "Progressive disclosure")

  (stretch :summary "Why stretchtext is semantic"
    (p "Expansion is a "
       (em "reading state")
       ", not a different file. The same node is always in the tree."))

  (h 2 :label "code" "Code as content")

  (codeblock :lang "cpp"
    "NodeId find_anchor_node(const Document& doc, std::string_view href);")

  (h 2 :label "notes" "Notes vs quotations")

  (quote "A document is not just something you render and read.")

  (note "A note is authorial commentary — semantic aside, not a margin region.")

  (h 2 :label "transclude" "Transclusion")

  (include "#identity")

  (p "Include points at a label; the view embeds the passage live.")
)
