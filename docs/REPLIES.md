# Reply composer (mockup)

The native discussion readers (Hacker News, Reddit, Lemmy, Nostr) display
comments as semantic `Message` cards. Right-click a comment card and select
**Reply…** to open the Reply panel. This also works with local documents
containing `Thread` / `Message` nodes.

For an offline test, open `examples/reply-demo.xml` and right-click any of its
three comments. The Reply panel is a standard Xanatoo floating window: drag
its title to move/dock it, resize it from its edges, or close it with the X.

- The composer accepts multiline text and has **Bold**, **Italic**, **Code**,
  **Link**, and **Quote** buttons. They insert Markdown delimiters around
  the selected text, or at the cursor if nothing is selected.
- The comment being replied to has an amber wash and border in the document,
  and a matching amber scrollbar tick (including when the message leaves the
  active virtual-layout band).
- **Send reply** deliberately does not contact any service. It reports that
  no account is connected, leaving the draft visible for further editing.
- The draft is session-only, scoped to the selected comment; choosing another
  comment starts a new draft. No credentials, persistence, or posting endpoint
  has been introduced. This is an interaction mockup for eventual account
  handling and service-specific reply APIs.

The main `App::reply` record owns the target and draft; `Ui` retains only the
ImGui editor's transient text/selection state. The target is identified by
buffer and message ID/label, not by the caret or current text selection.
The scrollbar marker remembers the last known document Y when the target
card is virtualised out of the active layout. Closing the panel removes both
highlights.
