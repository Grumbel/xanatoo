# xanatoo: Interactive document composition engine

A lightweight, realtime, editable document system for OpenGL, inspired by Markdown, typesetting, and Ted Nelson's hypertext ideas.

The central idea is that a document is not just something you render and read. It is a live workspace: users can edit text, expand ideas, rearrange content, follow relationships, compare versions, and manipulate images directly in the document.

## 1. Core application concept

A document consists of text, images, and layout blocks. It has a simple textual representation, but when displayed, it becomes an interactive composition rather than a static page.

Direct editing Edit text in place, insert images, create headings, split and merge paragraphs, and manipulate blocks without switching to a separate source editor.

Stretchtext Expand or contract passages in place. A compact summary can unfold into a detailed explanation, with the surrounding layout reflowing immediately.

Multiple views Display related documents or passages side by side. Select a passage in one view and inspect its source, related material, or another version in a second view.

Living relationships Links connect specific passages, images, and blocks, rather than merely pointing to whole documents. Connections can be inspected and edited.

Manipulable composition Move blocks, resize images, adjust columns, create sidebars, and change the layout interactively. Layout changes need not change the underlying content.

## 2. The user experience

The application should feel like a cross between a minimal text editor, a typesetting system, and an interactive canvas.

[Looking for a Private Note-Taking App? Here’s Why Notesnook Stands Out - TinkeringProd](https://images.openai.com/static-rsc-4/1tOGkYQvbA62DOWiAZBYApEzYkRfUGVcvi8fbSXbKTfsH4W-VLZzkpSTe3gk-2CTt2QnOsA3a0NK-do7GJtMklv7IVkzF3ADuc2Hkcneo64TVxTY3FZMAFRIpb3xIlw4qdbiCKwlLxHdqJKhNNnCmA4x3ElaEUW6BgFux521t4Y?purpose=inline)

[tinkeringprod.com](https://tinkeringprod.com/looking-for-a-private-note-taking-app-heres-why-notesnook-stands-out/)

A typical interaction:

1. Write a paragraph using Markdown-like syntax or edit it directly in the rendered view.
2. Select a phrase and turn it into a linked reference or expandable explanation.
3. Expand the explanation inline. The rest of the document reflows without a page reload.
4. Open the original source in a parallel pane, with the related passage highlighted.
5. Drag an image or block to another position, resize it, or change its placement relative to the text.
6. Undo any operation, or compare the current document against an earlier version.

The distinction is that reading, editing, navigation, and layout manipulation all happen in the same environment.

## 3. A small, implementable feature set

I'd build this incrementally rather than attempt to implement all of Xanadu.

Suggested development stages

## 01

Document model and layout engine

- Paragraphs, headings, lists, inline emphasis, links and images.
- Text shaping, line breaking, margins, padding, columns and nested blocks.
- A small declarative markup format.
- Immediate layout updates after edits.

## 02

Interactive editing

- Text cursor, selection, keyboard navigation and clipboard.
- Inline editing, block insertion, deletion and reordering.
- Mouse hit-testing for text, images and layout controls.
- Undo/redo and persistent document saving.

## 03

Interactive hypertext

- Expandable and contractible passages.
- Links to specific blocks and text ranges.
- Multiple document views and inline transclusion.
- Hover previews and navigation between related passages.

## 04

Advanced composition

- Drag-and-drop layout editing and adjustable columns.
- Version comparison and source tracking.
- Interactive diagrams and image hotspots.
- Optional visual links between panes and content regions.

## 4. Architecture

Keep the system small and separate content, layout, interaction, and rendering.

\#chatgpt-mermaid-\_r_109\_{font-family:-apple-system-body,ui-sans-serif,-apple-system,system-ui,"Segoe UI",Helvetica,"Apple Color Emoji",Arial,sans-serif,"Segoe UI Emoji","Segoe UI Symbol";font-size:16px;fill:rgb(13, 13, 13);}@keyframes edge-animation-frame{from{stroke-dashoffset:0;}}@keyframes dash{to{stroke-dashoffset:0;}}#chatgpt-mermaid-\_r_109\_ .edge-animation-slow{stroke-dasharray:9,5!important;stroke-dashoffset:900;animation:dash 50s linear infinite;stroke-linecap:round;}#chatgpt-mermaid-\_r_109\_ .edge-animation-fast{stroke-dasharray:9,5!important;stroke-dashoffset:900;animation:dash 20s linear infinite;stroke-linecap:round;}#chatgpt-mermaid-\_r_109\_ .error-icon{fill:rgb(243, 243, 243);}#chatgpt-mermaid-\_r_109\_ .error-text{fill:rgb(13, 13, 13);stroke:rgb(13, 13, 13);}#chatgpt-mermaid-\_r_109\_ .edge-thickness-normal{stroke-width:1px;}#chatgpt-mermaid-\_r_109\_ .edge-thickness-thick{stroke-width:3.5px;}#chatgpt-mermaid-\_r_109\_ .edge-pattern-solid{stroke-dasharray:0;}#chatgpt-mermaid-\_r_109\_ .edge-thickness-invisible{stroke-width:0;fill:none;}#chatgpt-mermaid-\_r_109\_ .edge-pattern-dashed{stroke-dasharray:3;}#chatgpt-mermaid-\_r_109\_ .edge-pattern-dotted{stroke-dasharray:2;}#chatgpt-mermaid-\_r_109\_ .marker{fill:rgb(143, 143, 143);stroke:rgb(143, 143, 143);}#chatgpt-mermaid-\_r_109\_ .marker.cross{stroke:rgb(143, 143, 143);}#chatgpt-mermaid-\_r_109\_ svg{font-family:-apple-system-body,ui-sans-serif,-apple-system,system-ui,"Segoe UI",Helvetica,"Apple Color Emoji",Arial,sans-serif,"Segoe UI Emoji","Segoe UI Symbol";font-size:16px;}#chatgpt-mermaid-\_r_109\_ p{margin:0;}#chatgpt-mermaid-\_r_109\_ .label{font-family:-apple-system-body,ui-sans-serif,-apple-system,system-ui,"Segoe UI",Helvetica,"Apple Color Emoji",Arial,sans-serif,"Segoe UI Emoji","Segoe UI Symbol";color:rgb(13, 13, 13);}#chatgpt-mermaid-\_r_109\_ .cluster-label text{fill:rgb(13, 13, 13);}#chatgpt-mermaid-\_r_109\_ .cluster-label span{color:rgb(13, 13, 13);}#chatgpt-mermaid-\_r_109\_ .cluster-label span p{background-color:transparent;}#chatgpt-mermaid-\_r_109\_ .label text,#chatgpt-mermaid-\_r_109\_ span{fill:rgb(13, 13, 13);color:rgb(13, 13, 13);}#chatgpt-mermaid-\_r_109\_ .node rect,#chatgpt-mermaid-\_r_109\_ .node circle,#chatgpt-mermaid-\_r_109\_ .node ellipse,#chatgpt-mermaid-\_r_109\_ .node polygon,#chatgpt-mermaid-\_r_109\_ .node path{fill:rgb(222, 234, 251);stroke:rgb(83, 154, 248);stroke-width:1px;}#chatgpt-mermaid-\_r_109\_ .rough-node .label text,#chatgpt-mermaid-\_r_109\_ .node .label text,#chatgpt-mermaid-\_r_109\_ .image-shape .label,#chatgpt-mermaid-\_r_109\_ .icon-shape .label{text-anchor:middle;}#chatgpt-mermaid-\_r_109\_ .node .katex path{fill:#000;stroke:#000;stroke-width:1px;}#chatgpt-mermaid-\_r_109\_ .rough-node .label,#chatgpt-mermaid-\_r_109\_ .node .label,#chatgpt-mermaid-\_r_109\_ .image-shape .label,#chatgpt-mermaid-\_r_109\_ .icon-shape .label{text-align:center;}#chatgpt-mermaid-\_r_109\_ .node.clickable{cursor:pointer;}#chatgpt-mermaid-\_r_109\_ .root .anchor path{fill:rgb(143, 143, 143)!important;stroke-width:0;stroke:rgb(143, 143, 143);}#chatgpt-mermaid-\_r_109\_ .arrowheadPath{fill:rgb(143, 143, 143);}#chatgpt-mermaid-\_r_109\_ .edgePath .path{stroke:rgb(143, 143, 143);stroke-width:1px;}#chatgpt-mermaid-\_r_109\_ .flowchart-link{stroke:rgb(143, 143, 143);fill:none;}#chatgpt-mermaid-\_r_109\_ .edgeLabel{background-color:rgb(252, 252, 252);text-align:center;}#chatgpt-mermaid-\_r_109\_ .edgeLabel p{background-color:rgb(252, 252, 252);}#chatgpt-mermaid-\_r_109\_ .edgeLabel rect{opacity:0.5;background-color:rgb(252, 252, 252);fill:rgb(252, 252, 252);}#chatgpt-mermaid-\_r_109\_ .labelBkg{background-color:rgba(252, 252, 252, 0.5);}#chatgpt-mermaid-\_r_109\_ .cluster rect{fill:rgb(243, 243, 243);stroke:rgba(0, 0, 0, 0.1);stroke-width:1px;}#chatgpt-mermaid-\_r_109\_ .cluster text{fill:rgb(13, 13, 13);}#chatgpt-mermaid-\_r_109\_ .cluster span{color:rgb(13, 13, 13);}#chatgpt-mermaid-\_r_109\_ div.mermaidTooltip{position:absolute;text-align:center;max-width:200px;padding:2px;font-family:-apple-system-body,ui-sans-serif,-a

Important implementation decisions:

- Document model: semantic blocks and inline spans with stable IDs. Do not make screen coordinates the identity of content.
- Layout engine: calculates line breaks, geometry and reflow independently of OpenGL. Rendering should consume the calculated layout.
- Interaction: hit-testing maps screen coordinates back to text positions, blocks, images and links.
- Editing: represent changes as operations that can be undone and redone. Layout is recalculated from the resulting document state.
- Markup: use Markdown-like syntax for simple documents, with explicit extensions for layout and interactive behaviour.
- Rendering: use OpenGL for drawing, but keep text shaping and font metrics in a dedicated text subsystem.

For the first version, a tree-based document model is sufficient. Stable IDs and explicit references make later transclusion and version comparison possible without committing to a complex distributed document database.

## 5. Example markup

A deliberately small hypothetical syntax:

```
# The network

The web connects documents, but
[hypertext](#hypertext) connects ideas.

:::stretch summary="What is hypertext?"
  Hypertext is a system of linked information
  that can be explored non-linearly.

  :::note
  This explanation can itself contain
  links, images, and further expansions.
  :::
:::

:::columns
  :::column
    ## Original

    A passage from the original document.
  :::

  :::column
    ## Commentary

    An editable annotation linked to
    the original passage.
  :::
:::

![Network diagram](network.png)
```

The exact syntax is open for experimentation. The key requirement is that the same document can be edited through either its source representation or its rendered interactive representation, without losing structure.

## 6. What makes xanatoo different

| Conventional text renderer               | xanatoo                                             |
| ---------------------------------------- | ---------------------------------------------------- |
| Displays formatted text                  | Displays and edits live content                      |
| Links navigate elsewhere                 | Links can expose relationships in place              |
| Collapsed content is an interface widget | Stretchtext is a document-level behaviour            |
| Images are embedded assets               | Images are manipulable, addressable content          |
| Layout is mostly predetermined           | Layout can be changed interactively                  |
| Documents are viewed individually        | Related passages can be composed into parallel views |
| Editing and reading are separate modes   | Reading and editing coexist                          |

The scope should remain deliberately modest: xanatoo is not a web browser, word processor, or full Xanadu implementation. It is a realtime composition engine for interactive documents, where layout and navigation are part of the content experience.

The defining design principle: content should be addressable, editable, and rearrangeable, and changes to its presentation should be reflected immediately without destroying the relationships between its parts.
