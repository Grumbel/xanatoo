// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>

#include "xanatoo/parse.hpp"
#include "xanatoo/util.hpp"
#include <cctype>
#include <algorithm>
#include <vector>
#include <cstdio>

namespace xanatoo {

namespace {

struct Line {
  std::string_view text;
  bool blank = false;
};

std::vector<Line> split_lines(std::string_view src) {
  std::vector<Line> lines;
  size_t i = 0;
  while (i < src.size()) {
    size_t start = i;
    while (i < src.size() && src[i] != '\n') ++i;
    std::string_view line = src.substr(start, i - start);
    if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
    bool blank = true;
    for (char c : line) {
      if (!std::isspace(static_cast<unsigned char>(c))) {
        blank = false;
        break;
      }
    }
    lines.push_back({line, blank});
    if (i < src.size()) ++i;  // skip \n
  }
  return lines;
}

// Detect a list-item marker at the start of a trimmed line.
// Returns bytes to skip past the marker, or 0 if not a list item.
// Sets *ordered_index to 1-based index for "N. " markers, or 0 for "- "/"* ".
size_t list_marker_len(std::string_view trimmed, int* ordered_index) {
  if (ordered_index) *ordered_index = 0;
  if (trimmed.size() >= 2 &&
      (trimmed[0] == '-' || trimmed[0] == '*' || trimmed[0] == '+') &&
      trimmed[1] == ' ') {
    return 2;
  }
  // Ordered: one or more digits, then ". "
  size_t i = 0;
  while (i < trimmed.size() &&
         std::isdigit(static_cast<unsigned char>(trimmed[i])))
    ++i;
  if (i > 0 && i + 1 < trimmed.size() && trimmed[i] == '.' &&
      trimmed[i + 1] == ' ') {
    if (ordered_index) {
      int n = 0;
      for (size_t k = 0; k < i; ++k)
        n = n * 10 + (trimmed[k] - '0');
      *ordered_index = n > 0 ? n : 1;
    }
    return i + 2;
  }
  return 0;
}

// Parse inline spans for a single paragraph/heading line.
void parse_inlines(Node& node, std::string_view text) {
  size_t i = 0;
  std::string plain;

  auto flush_plain = [&]() {
    if (!plain.empty()) {
      InlineSpan s;
      s.kind = InlineKind::Text;
      s.text = plain;
      node.spans.push_back(std::move(s));
      plain.clear();
    }
  };

  while (i < text.size()) {
    // **strong**
    if (i + 1 < text.size() && text[i] == '*' && text[i + 1] == '*') {
      size_t end = text.find("**", i + 2);
      if (end != std::string_view::npos) {
        flush_plain();
        InlineSpan s;
        s.kind = InlineKind::Strong;
        s.text = std::string(text.substr(i + 2, end - (i + 2)));
        node.spans.push_back(std::move(s));
        i = end + 2;
        continue;
      }
    }
    // ~~strikethrough~~ (GFM)
    if (i + 1 < text.size() && text[i] == '~' && text[i + 1] == '~') {
      size_t end = text.find("~~", i + 2);
      if (end != std::string_view::npos) {
        flush_plain();
        InlineSpan s;
        s.kind = InlineKind::Strike;
        s.text = std::string(text.substr(i + 2, end - (i + 2)));
        node.spans.push_back(std::move(s));
        i = end + 2;
        continue;
      }
    }
    // *emphasis*
    if (text[i] == '*') {
      size_t end = text.find('*', i + 1);
      if (end != std::string_view::npos) {
        flush_plain();
        InlineSpan s;
        s.kind = InlineKind::Emphasis;
        s.text = std::string(text.substr(i + 1, end - (i + 1)));
        node.spans.push_back(std::move(s));
        i = end + 1;
        continue;
      }
    }
    // [text](url) or [text](#id) or [text](cite:key); optional {rel=…}
    if (text[i] == '[') {
      size_t mid = text.find("](", i + 1);
      if (mid != std::string_view::npos) {
        size_t end = text.find(')', mid + 2);
        if (end != std::string_view::npos) {
          flush_plain();
          InlineSpan s;
          s.text = std::string(text.substr(i + 1, mid - (i + 1)));
          std::string href(text.substr(mid + 2, end - (mid + 2)));
          // Citations: [Nelson 1965](cite:nelson65)
          if (href.rfind("cite:", 0) == 0) {
            s.kind = InlineKind::Cite;
            s.href = href.substr(5);
          } else {
            s.kind = InlineKind::Link;
            s.href = std::move(href);
          }
          size_t next = end + 1;
          // Optional {rel=glossary} after the closing paren
          if (next < text.size() && text[next] == '{') {
            size_t rpos = text.find("rel=", next);
            size_t bend = text.find('}', next);
            if (rpos != std::string_view::npos && bend != std::string_view::npos &&
                rpos < bend) {
              rpos += 4;
              s.rel = std::string(text.substr(rpos, bend - rpos));
              next = bend + 1;
            }
          }
          node.spans.push_back(std::move(s));
          i = next;
          continue;
        }
      }
    }
    // `code`
    if (text[i] == '`') {
      size_t end = text.find('`', i + 1);
      if (end != std::string_view::npos) {
        flush_plain();
        InlineSpan s;
        s.kind = InlineKind::Code;
        s.text = std::string(text.substr(i + 1, end - (i + 1)));
        node.spans.push_back(std::move(s));
        i = end + 1;
        continue;
      }
    }
    // <http://…> / <https://…> / <mailto:…> autolinks
    if (text[i] == '<') {
      size_t end = text.find('>', i + 1);
      if (end != std::string_view::npos && end > i + 1) {
        std::string_view inner = text.substr(i + 1, end - (i + 1));
        bool is_url =
            inner.starts_with("http://") || inner.starts_with("https://") ||
            inner.starts_with("mailto:");
        if (is_url) {
          flush_plain();
          InlineSpan s;
          s.kind = InlineKind::Link;
          s.href = std::string(inner);
          s.text = std::string(inner);
          node.spans.push_back(std::move(s));
          i = end + 1;
          continue;
        }
      }
    }
    // Bare http(s):// URLs (GFM-style autolink)
    if ((text[i] == 'h' || text[i] == 'H') && i + 7 < text.size()) {
      std::string_view rest = text.substr(i);
      size_t scheme = 0;
      if (rest.size() >= 8 &&
          (rest.substr(0, 8) == "https://" || rest.substr(0, 8) == "HTTPS://"))
        scheme = 8;
      else if (rest.size() >= 7 &&
               (rest.substr(0, 7) == "http://" || rest.substr(0, 7) == "HTTP://"))
        scheme = 7;
      if (scheme > 0) {
        size_t end = scheme;
        while (end < rest.size()) {
          unsigned char c = static_cast<unsigned char>(rest[end]);
          if (c <= 32 || c == '<' || c == '>' || c == '"' || c == '\'' ||
              c == '`' || c == ')' || c == '(' || c == '[' || c == ']' ||
              c == '{' || c == '}')
            break;
          ++end;
        }
        // Trim trailing punctuation commonly not part of the URL
        while (end > scheme) {
          char t = rest[end - 1];
          if (t == '.' || t == ',' || t == ';' || t == ':' || t == '!' ||
              t == '?' || t == '"')
            --end;
          else
            break;
        }
        if (end > scheme) {
          flush_plain();
          InlineSpan s;
          s.kind = InlineKind::Link;
          s.href = std::string(rest.substr(0, end));
          s.text = s.href;
          node.spans.push_back(std::move(s));
          i += end;
          continue;
        }
      }
    }

    plain.push_back(text[i]);
    ++i;
  }
  flush_plain();
}

}  // namespace

Document parse_markup(std::string_view source) {
  Document doc;
  NodeId root = doc.create(NodeKind::Document);

  auto lines = split_lines(source);
  size_t i = 0;

  // Simple stack for nested containers (stretch, columns, column)
  std::vector<NodeId> stack;
  stack.push_back(root);

  auto current_parent = [&]() -> NodeId { return stack.back(); };

  while (i < lines.size()) {
    const Line& L = lines[i];

    if (L.blank) {
      ++i;
      continue;
    }

    std::string_view t = L.text;
    // Trim leading spaces for block detection (but keep for paragraphs)
    size_t lead = 0;
    while (lead < t.size() && (t[lead] == ' ' || t[lead] == '\t')) ++lead;
    std::string_view trimmed = t.substr(lead);

    // Closing fence
    if (trimmed == ":::" || trimmed.starts_with("::: ")) {
      // Treat bare ::: as close of current container if possible
      if (stack.size() > 1) {
        stack.pop_back();
      }
      ++i;
      continue;
    }

    // Opening fences
    if (trimmed.starts_with(":::stretch")) {
      NodeId n = doc.create(NodeKind::Stretch, current_parent());
      // Parse summary="..." if present
      auto pos = trimmed.find("summary=\"");
      if (pos != std::string_view::npos) {
        pos += 9; // after summary="
        auto end = trimmed.find('"', pos);
        if (end != std::string_view::npos) {
          doc.get(n).summary = std::string(trimmed.substr(pos, end - pos));
        }
      }
      doc.get(n).expanded = false; // start collapsed — classic stretchtext
      stack.push_back(n);
      ++i;
      continue;
    }
    if (trimmed.starts_with(":::columns")) {
      NodeId n = doc.create(NodeKind::Columns, current_parent());
      stack.push_back(n);
      ++i;
      continue;
    }
    if (trimmed.starts_with(":::column")) {
      NodeId n = doc.create(NodeKind::Column, current_parent());
      // Optional weight=1.5 (relative column width)
      auto wpos = trimmed.find("weight=");
      if (wpos != std::string_view::npos) {
        wpos += 7;
        size_t wend = wpos;
        while (wend < trimmed.size() &&
               (std::isdigit(static_cast<unsigned char>(trimmed[wend])) ||
                trimmed[wend] == '.'))
          ++wend;
        try {
          float w = std::stof(std::string(trimmed.substr(wpos, wend - wpos)));
          if (w > 0.1f && w < 20.f) doc.get(n).display_scale = w;
        } catch (...) {}
      }
      stack.push_back(n);
      ++i;
      continue;
    }
    if (trimmed.starts_with(":::note")) {
      NodeId n = doc.create(NodeKind::Note, current_parent());
      // Optional subtype: :::note kind=tip  (stored in src)
      auto kpos = trimmed.find("kind=");
      if (kpos != std::string_view::npos) {
        kpos += 5;
        size_t kend = kpos;
        while (kend < trimmed.size() &&
               (std::isalnum(static_cast<unsigned char>(trimmed[kend])) ||
                trimmed[kend] == '-' || trimmed[kend] == '_'))
          ++kend;
        if (kend > kpos)
          doc.get(n).src = std::string(trimmed.substr(kpos, kend - kpos));
      }
      // Note is a leaf with spans (same as BlockQuote); gather body until :::
      ++i;
      std::string body;
      while (i < lines.size()) {
        std::string_view lt = lines[i].text;
        size_t llead = 0;
        while (llead < lt.size() && (lt[llead] == ' ' || lt[llead] == '\t'))
          ++llead;
        std::string_view ltrim = lt.substr(llead);
        if (ltrim == ":::" || ltrim.starts_with("::: ")) {
          ++i;
          break;
        }
        if (!body.empty()) body.push_back(' ');
        body.append(ltrim);
        ++i;
      }
      if (!body.empty()) parse_inlines(doc.get(n), body);
      continue;
    }
    // :::code / :::codeblock [lang=cpp] … :::
    if (trimmed.starts_with(":::codeblock") || trimmed.starts_with(":::code")) {
      NodeId n = doc.create(NodeKind::CodeBlock, current_parent());
      auto lpos = trimmed.find("lang=");
      if (lpos != std::string_view::npos) {
        lpos += 5;
        size_t lend = lpos;
        while (lend < trimmed.size() &&
               (std::isalnum(static_cast<unsigned char>(trimmed[lend])) ||
                trimmed[lend] == '+' || trimmed[lend] == '-' ||
                trimmed[lend] == '_' || trimmed[lend] == '#'))
          ++lend;
        if (lend > lpos)
          doc.get(n).src = std::string(trimmed.substr(lpos, lend - lpos));
      }
      ++i;
      std::string body;
      while (i < lines.size()) {
        std::string_view lt = lines[i].text;
        size_t llead = 0;
        while (llead < lt.size() && (lt[llead] == ' ' || lt[llead] == '\t'))
          ++llead;
        std::string_view ltrim = lt.substr(llead);
        if (ltrim == ":::" || ltrim.starts_with("::: ")) {
          ++i;
          break;
        }
        if (!body.empty()) body.push_back('\n');
        body.append(lt);
        ++i;
      }
      if (!body.empty()) doc.append_text(n, body);
      continue;
    }
    // GFM fenced code: ```lang … ```
    if (trimmed.starts_with("```") || trimmed.starts_with("~~~")) {
      const char fence_ch = trimmed[0];
      size_t fence_len = 0;
      while (fence_len < trimmed.size() && trimmed[fence_len] == fence_ch)
        ++fence_len;
      if (fence_len >= 3) {
        NodeId n = doc.create(NodeKind::CodeBlock, current_parent());
        // Optional language tag after the fence
        size_t lang_start = fence_len;
        while (lang_start < trimmed.size() &&
               (trimmed[lang_start] == ' ' || trimmed[lang_start] == '\t'))
          ++lang_start;
        if (lang_start < trimmed.size()) {
          size_t lang_end = lang_start;
          while (lang_end < trimmed.size() &&
                 (std::isalnum(static_cast<unsigned char>(trimmed[lang_end])) ||
                  trimmed[lang_end] == '+' || trimmed[lang_end] == '-' ||
                  trimmed[lang_end] == '_' || trimmed[lang_end] == '#' ||
                  trimmed[lang_end] == '.'))
            ++lang_end;
          if (lang_end > lang_start)
            doc.get(n).src =
                std::string(trimmed.substr(lang_start, lang_end - lang_start));
        }
        ++i;
        std::string body;
        while (i < lines.size()) {
          std::string_view lt = lines[i].text;
          size_t llead = 0;
          while (llead < lt.size() && (lt[llead] == ' ' || lt[llead] == '\t'))
            ++llead;
          std::string_view ltrim = lt.substr(llead);
          // Closing fence: same character, at least as long, no other content
          size_t close_len = 0;
          while (close_len < ltrim.size() && ltrim[close_len] == fence_ch)
            ++close_len;
          if (close_len >= fence_len) {
            size_t after = close_len;
            while (after < ltrim.size() &&
                   (ltrim[after] == ' ' || ltrim[after] == '\t'))
              ++after;
            if (after == ltrim.size()) {
              ++i;
              break;
            }
          }
          if (!body.empty()) body.push_back('\n');
          body.append(lt);
          ++i;
        }
        if (!body.empty()) doc.append_text(n, body);
        continue;
      }
    }
    if (trimmed.starts_with(":::deflist")) {
      NodeId n = doc.create(NodeKind::DefList, current_parent());
      stack.push_back(n);
      ++i;
      continue;
    }
    if (trimmed.starts_with(":::figure")) {
      NodeId n = doc.create(NodeKind::Figure, current_parent());
      stack.push_back(n);
      ++i;
      continue;
    }
    if (trimmed.starts_with(":::biblio") ||
        trimmed.starts_with(":::bibliography")) {
      NodeId n = doc.create(NodeKind::Biblio, current_parent());
      stack.push_back(n);
      ++i;
      continue;
    }
    if (trimmed.starts_with(":::table")) {
      NodeId n = doc.create(NodeKind::Table, current_parent());
      stack.push_back(n);
      ++i;
      continue;
    }
    // Self-closing include:
    //   :::include #LocalHeading
    //   :::include LocalHeading          → #LocalHeading
    //   :::include other.md#Section      → cross-document (path kept)
    //   :::include other.md              → whole external document body
    //   :::include "path with spaces.md#H"
    //   :::include #H excerpt="phrase"   → range within target
    if (trimmed.starts_with(":::include")) {
      NodeId n = doc.create(NodeKind::Transclude, current_parent());
      std::string_view rest = trimmed.substr(10);  // after :::include
      while (!rest.empty() && (rest[0] == ' ' || rest[0] == '\t'))
        rest.remove_prefix(1);

      auto looks_like_path = [](const std::string& s) {
        if (s.empty() || s[0] == '#') return false;
        size_t hash = s.find('#');
        std::string path_part =
            hash == std::string::npos ? s : s.substr(0, hash);
        if (path_part.empty()) return false;
        return path_part.find('/') != std::string::npos ||
               path_part.find('\\') != std::string::npos ||
               path_part.find('.') != std::string::npos;
      };

      std::string excerpt;
      std::string rest_owned(rest);
      {
        auto epos = rest_owned.find("excerpt=");
        if (epos != std::string::npos) {
          size_t after = epos + 8;
          while (after < rest_owned.size() &&
                 (rest_owned[after] == ' ' || rest_owned[after] == '\t'))
            ++after;
          if (after < rest_owned.size() &&
              (rest_owned[after] == '"' || rest_owned[after] == '\'')) {
            char eq = rest_owned[after];
            ++after;
            size_t eend = rest_owned.find(eq, after);
            if (eend != std::string::npos) {
              excerpt = rest_owned.substr(after, eend - after);
              std::string cleaned = rest_owned.substr(0, epos);
              if (eend + 1 < rest_owned.size())
                cleaned += rest_owned.substr(eend + 1);
              while (!cleaned.empty() &&
                     (cleaned.front() == ' ' || cleaned.front() == '\t'))
                cleaned.erase(cleaned.begin());
              while (!cleaned.empty() &&
                     (cleaned.back() == ' ' || cleaned.back() == '\t'))
                cleaned.pop_back();
              rest_owned = std::move(cleaned);
            }
          }
        }
      }

      std::string target;
      if (!rest_owned.empty() &&
          (rest_owned[0] == '"' || rest_owned[0] == '\'')) {
        char q = rest_owned[0];
        size_t qend = rest_owned.find(q, 1);
        if (qend != std::string::npos)
          target = rest_owned.substr(1, qend - 1);
        else
          target = rest_owned.substr(1);
      } else {
        target = rest_owned;
        while (!target.empty() &&
               (target.back() == ' ' || target.back() == '\t'))
          target.pop_back();
      }

      if (!target.empty() && target[0] != '#' && !looks_like_path(target))
        target = "#" + target;
      doc.get(n).src = target;
      if (!excerpt.empty()) doc.get(n).summary = excerpt;
      ++i;
      continue;
    }

    // Image ![alt](src) optional {scale=1.5}
    if (trimmed.starts_with("![")) {
      size_t mid = trimmed.find("](");
      size_t end = trimmed.find(')', mid == std::string_view::npos ? 0 : mid);
      if (mid != std::string_view::npos && end != std::string_view::npos) {
        NodeId n = doc.create(NodeKind::Image, current_parent());
        doc.get(n).alt = std::string(trimmed.substr(2, mid - 2));
        doc.get(n).src = std::string(trimmed.substr(mid + 2, end - (mid + 2)));
        // Optional {scale=…} after the closing paren
        size_t brace = trimmed.find("{scale=", end);
        if (brace != std::string_view::npos) {
          size_t bend = trimmed.find('}', brace);
          if (bend != std::string_view::npos) {
            std::string num(trimmed.substr(brace + 7, bend - (brace + 7)));
            try {
              float s = std::stof(num);
              if (s > 0.05f && s < 10.f) doc.get(n).display_scale = s;
            } catch (...) {}
          }
        }
        ++i;
        continue;
      }
    }

    // Horizontal rule: a line of only -, *, or _ (at least three)
    if (trimmed.size() >= 3) {
      char hc = trimmed[0];
      if (hc == '-' || hc == '*' || hc == '_') {
        bool all_same = true;
        for (char ch : trimmed) {
          if (ch != hc) {
            all_same = false;
            break;
          }
        }
        if (all_same) {
          doc.create(NodeKind::HorizontalRule, current_parent());
          ++i;
          continue;
        }
      }
    }

    // Headings
    int heading_level = 0;
    if (trimmed.starts_with("###### ")) heading_level = 6;
    else if (trimmed.starts_with("##### ")) heading_level = 5;
    else if (trimmed.starts_with("#### ")) heading_level = 4;
    else if (trimmed.starts_with("### ")) heading_level = 3;
    else if (trimmed.starts_with("## ")) heading_level = 2;
    else if (trimmed.starts_with("# ")) heading_level = 1;

    if (heading_level > 0) {
      size_t skip = heading_level + 1;
      NodeId n = doc.create(NodeKind::Heading, current_parent());
      doc.get(n).level = heading_level;
      parse_inlines(doc.get(n), trimmed.substr(skip));
      ++i;
      continue;
    }

    // Setext headings: Title\n===== → H1, Title\n----- → H2
    if (!trimmed.empty() && i + 1 < lines.size() && !lines[i + 1].blank) {
      std::string_view next = lines[i + 1].text;
      size_t nlead = 0;
      while (nlead < next.size() && (next[nlead] == ' ' || next[nlead] == '\t'))
        ++nlead;
      std::string_view ntrim = next.substr(nlead);
      if (ntrim.size() >= 3) {
        char uc = ntrim[0];
        if (uc == '=' || uc == '-') {
          bool all = true;
          for (char ch : ntrim) {
            if (ch != uc) {
              all = false;
              break;
            }
          }
          if (all && !trimmed.starts_with("#") && !trimmed.starts_with(">") &&
              !trimmed.starts_with("```") && !trimmed.starts_with("~~~") &&
              !trimmed.starts_with("|") && !trimmed.starts_with(":::")) {
            int setext_level = (uc == '=') ? 1 : 2;
            NodeId n = doc.create(NodeKind::Heading, current_parent());
            doc.get(n).level = setext_level;
            parse_inlines(doc.get(n), trimmed);
            i += 2;
            continue;
          }
        }
      }
    }

    // Unordered / ordered list (supports nested items via leading indent)
    {
      int ord = 0;
      size_t mlen = list_marker_len(trimmed, &ord);
      if (mlen > 0) {
        // Stack of (list_id, indent of items in that list)
        struct Frame {
          NodeId list;
          int indent;
        };
        std::vector<Frame> frames;
        int base_indent = static_cast<int>(lines[i].text.size() - trimmed.size());
        // recomputed properly:
        {
          std::string_view raw = lines[i].text;
          size_t lead = 0;
          while (lead < raw.size() && (raw[lead] == ' ' || raw[lead] == '\t'))
            ++lead;
          base_indent = static_cast<int>(lead);
        }
        NodeId top = doc.create(NodeKind::List, current_parent());
        doc.get(top).level = (ord > 0) ? 1 : 0;
        frames.push_back({top, base_indent});
        int item_index = 0;

        while (i < lines.size() && !lines[i].blank) {
          std::string_view lt = lines[i].text;
          size_t llead = 0;
          while (llead < lt.size() && (lt[llead] == ' ' || lt[llead] == '\t'))
            ++llead;
          std::string_view ltrim = lt.substr(llead);
          int item_ord = 0;
          size_t imlen = list_marker_len(ltrim, &item_ord);
          if (imlen == 0) break;
          int ind = static_cast<int>(llead);

          // Pop to a frame whose indent is strictly less than this line's
          // when this line is less-or-equally indented than the current frame
          while (frames.size() > 1 && ind <= frames.back().indent)
            frames.pop_back();

          // Deeper indent → nest under the previous item of the current list
          if (ind > frames.back().indent) {
            Node& cur_list = doc.get(frames.back().list);
            if (cur_list.children.empty()) {
              // No host item yet; treat as same level
            } else {
              NodeId host = cur_list.children.back();
              if (doc.get(host).kind == NodeKind::ListItem) {
                NodeId nested = doc.create(NodeKind::List, host);
                // Inherit ordered/unordered from the marker on this line
                doc.get(nested).level = (item_ord > 0) ? 1 : 0;
                frames.push_back({nested, ind});
              }
            }
          }

          NodeId list = frames.back().list;
          bool list_ordered = doc.get(list).level == 1;
          bool item_ordered = item_ord > 0;
          // Kind mismatch at this level: close the list run
          if (item_ordered != list_ordered && frames.size() == 1) break;
          // If nested and kind differs, still accept (nested can differ)

          NodeId item = doc.create(NodeKind::ListItem, list);
          ++item_index;
          doc.get(item).level = list_ordered
                                    ? (item_ord > 0 ? item_ord : item_index)
                                    : 0;
          std::string_view body = ltrim.substr(imlen);
          // GFM task list: "- [ ] text" / "- [x] text"
          if (body.size() >= 3 && body[0] == '[') {
            char mid = body[1];
            if ((mid == ' ' || mid == 'x' || mid == 'X') && body[2] == ']') {
              doc.get(item).label =
                  (mid == 'x' || mid == 'X') ? "task-done" : "task";
              body.remove_prefix(3);
              while (!body.empty() && (body[0] == ' ' || body[0] == '\t'))
                body.remove_prefix(1);
            }
          }
          parse_inlines(doc.get(item), body);
          ++i;
        }
        // Ordered item numbers were set while scanning; no second pass.
        continue;
      }
    }

    // Block quote: consecutive lines starting with "> "
    if (trimmed.starts_with("> ")) {
      std::string quote;
      while (i < lines.size() && !lines[i].blank) {
        std::string_view lt = lines[i].text;
        size_t llead = 0;
        while (llead < lt.size() && (lt[llead] == ' ' || lt[llead] == '\t'))
          ++llead;
        std::string_view ltrim = lt.substr(llead);
        if (!ltrim.starts_with("> ")) break;
        if (!quote.empty()) quote.push_back(' ');
        quote.append(ltrim.substr(2));
        ++i;
      }
      if (!quote.empty()) {
        NodeId n = doc.create(NodeKind::BlockQuote, current_parent());
        parse_inlines(doc.get(n), quote);
      }
      continue;
    }

    // GFM-style pipe table (standalone or inside :::table)
    if (trimmed.size() >= 2 && trimmed[0] == '|' &&
        trimmed.find('|', 1) != std::string_view::npos) {
      NodeId parent = current_parent();
      NodeId table = parent;
      if (doc.get(parent).kind != NodeKind::Table)
        table = doc.create(NodeKind::Table, parent);

      auto split_cells = [](std::string_view row) {
        std::vector<std::string> cells;
        if (!row.empty() && row.front() == '|') row.remove_prefix(1);
        if (!row.empty() && row.back() == '|') row.remove_suffix(1);
        size_t start = 0;
        for (size_t p = 0; p <= row.size(); ++p) {
          if (p == row.size() || row[p] == '|') {
            std::string_view cell = row.substr(start, p - start);
            while (!cell.empty() &&
                   (cell.front() == ' ' || cell.front() == '\t'))
              cell.remove_prefix(1);
            while (!cell.empty() &&
                   (cell.back() == ' ' || cell.back() == '\t'))
              cell.remove_suffix(1);
            cells.emplace_back(cell);
            start = p + 1;
          }
        }
        return cells;
      };
      auto is_sep_row = [](std::string_view row) {
        for (char c : row) {
          if (c != '|' && c != '-' && c != ':' && c != ' ' && c != '\t')
            return false;
        }
        return row.find('-') != std::string_view::npos;
      };

      bool first_row = true;
      while (i < lines.size() && !lines[i].blank) {
        std::string_view lt = lines[i].text;
        size_t llead = 0;
        while (llead < lt.size() && (lt[llead] == ' ' || lt[llead] == '\t'))
          ++llead;
        std::string_view ltrim = lt.substr(llead);
        if (ltrim.empty() || ltrim[0] != '|') break;
        if (is_sep_row(ltrim)) {
          ++i;
          continue;
        }
        auto cells = split_cells(ltrim);
        NodeId row = doc.create(NodeKind::TableRow, table);
        bool make_header = false;
        if (first_row && i + 1 < lines.size()) {
          std::string_view nt = lines[i + 1].text;
          size_t nlead = 0;
          while (nlead < nt.size() &&
                 (nt[nlead] == ' ' || nt[nlead] == '\t'))
            ++nlead;
          if (is_sep_row(nt.substr(nlead))) make_header = true;
        }
        for (const auto& cell_text : cells) {
          NodeId cell = doc.create(NodeKind::TableCell, row);
          if (make_header) doc.get(cell).level = 1;
          if (!cell_text.empty()) parse_inlines(doc.get(cell), cell_text);
        }
        first_row = false;
        ++i;
      }
      continue;
    }

    // Definition list body (inside :::deflist): term lines + ": definition"
    if (doc.get(current_parent()).kind == NodeKind::DefList) {
      if (trimmed.starts_with(": ")) {
        NodeId def = doc.create(NodeKind::Definition, current_parent());
        parse_inlines(doc.get(def), trimmed.substr(2));
        ++i;
        continue;
      }
      if (!trimmed.starts_with(":::")) {
        NodeId term = doc.create(NodeKind::Term, current_parent());
        parse_inlines(doc.get(term), trimmed);
        ++i;
        continue;
      }
    }

    // Bibliography entry: [^key]: text
    if (trimmed.starts_with("[^")) {
      size_t close = trimmed.find("]:");
      if (close != std::string_view::npos && close > 2) {
        NodeId parent = current_parent();
        if (doc.get(parent).kind != NodeKind::Biblio)
          parent = doc.create(NodeKind::Biblio, parent);
        NodeId entry = doc.create(NodeKind::BiblioEntry, parent);
        doc.get(entry).label = std::string(trimmed.substr(2, close - 2));
        std::string_view body = trimmed.substr(close + 2);
        while (!body.empty() && (body.front() == ' ' || body.front() == '\t'))
          body.remove_prefix(1);
        if (!body.empty()) parse_inlines(doc.get(entry), body);
        ++i;
        continue;
      }
    }

    // Caption under a figure: non-image text line becomes Caption
    if (doc.get(current_parent()).kind == NodeKind::Figure &&
        !trimmed.starts_with("![") && !trimmed.starts_with(":::")) {
      NodeId cap = doc.create(NodeKind::Caption, current_parent());
      parse_inlines(doc.get(cap), trimmed);
      ++i;
      continue;
    }

    // Paragraph: gather consecutive non-blank, non-fence lines
    std::string para;
    while (i < lines.size() && !lines[i].blank) {
      std::string_view lt = lines[i].text;
      size_t llead = 0;
      while (llead < lt.size() && (lt[llead] == ' ' || lt[llead] == '\t')) ++llead;
      std::string_view ltrim = lt.substr(llead);
      if (ltrim.starts_with(":::") || ltrim.starts_with("# ") ||
          ltrim.starts_with("## ") || ltrim.starts_with("![") ||
          ltrim.starts_with("> ") || ltrim.starts_with("|") ||
          ltrim.starts_with("[^") ||
          list_marker_len(ltrim, nullptr) > 0) {
        break;
      }
      if (!para.empty()) para.push_back(' ');
      // Prefer the original line content without leading indent for simplicity
      para.append(ltrim);
      ++i;
    }
    if (!para.empty()) {
      NodeId n = doc.create(NodeKind::Paragraph, current_parent());
      parse_inlines(doc.get(n), para);
    }
  }

  return doc;
}

namespace {

void serialize_inlines(const Node& node, std::string& out) {
  for (const auto& sp : node.spans) {
    switch (sp.kind) {
      case InlineKind::Strong:
        out += "**";
        out += sp.text;
        out += "**";
        break;
      case InlineKind::Emphasis:
        out += "*";
        out += sp.text;
        out += "*";
        break;
      case InlineKind::Strike:
        out += "~~";
        out += sp.text;
        out += "~~";
        break;
      case InlineKind::Code:
        out += "`";
        out += sp.text;
        out += "`";
        break;
      case InlineKind::Link:
        out += "[";
        out += sp.text;
        out += "](";
        out += sp.href;
        out += ")";
        if (!sp.rel.empty()) {
          out += "{rel=";
          out += sp.rel;
          out += "}";
        }
        break;
      case InlineKind::Cite:
        out += "[";
        out += sp.text;
        out += "](cite:";
        out += sp.href;
        out += ")";
        break;
      default:
        out += sp.text;
        break;
    }
  }
}

void serialize_node(const Document& doc, NodeId id, std::string& out,
                    int indent) {
  const Node& node = doc.get(id);
  std::string pad(static_cast<size_t>(indent), ' ');

  switch (node.kind) {
    case NodeKind::Document:
      for (NodeId c : node.children) {
        serialize_node(doc, c, out, indent);
      }
      break;

    case NodeKind::Heading: {
      out += pad;
      out.append(static_cast<size_t>(std::max(1, std::min(node.level, 6))), '#');
      out += ' ';
      serialize_inlines(node, out);
      out += "\n\n";
      break;
    }

    case NodeKind::Paragraph: {
      out += pad;
      serialize_inlines(node, out);
      out += "\n\n";
      break;
    }

    case NodeKind::BlockQuote: {
      out += pad;
      out += "> ";
      serialize_inlines(node, out);
      out += "\n\n";
      break;
    }

    case NodeKind::HorizontalRule: {
      out += pad;
      out += "---\n\n";
      break;
    }

    case NodeKind::Transclude: {
      out += pad;
      out += ":::include ";
      out += node.src;
      if (!node.summary.empty()) {
        out += " excerpt=\"";
        out += node.summary;
        out += "\"";
      }
      out += "\n\n";
      break;
    }

    case NodeKind::Note: {
      out += pad;
      out += ":::note";
      if (!node.src.empty()) {
        out += " kind=";
        out += node.src;
      }
      out += "\n";
      out += pad;
      serialize_inlines(node, out);
      out += "\n";
      out += pad;
      out += ":::\n\n";
      break;
    }

    case NodeKind::CodeBlock: {
      // Prefer GFM fences for broader interop; lang stays on node.src.
      out += pad;
      out += "```";
      if (!node.src.empty()) out += node.src;
      out += "\n";
      for (const auto& sp : node.spans) out += sp.text;
      if (out.empty() || out.back() != '\n') out += "\n";
      out += pad;
      out += "```\n\n";
      break;
    }

    case NodeKind::DefList: {
      out += pad;
      out += ":::deflist\n";
      for (NodeId c : node.children)
        serialize_node(doc, c, out, indent);
      out += pad;
      out += ":::\n\n";
      break;
    }

    case NodeKind::Term: {
      out += pad;
      serialize_inlines(node, out);
      out += "\n";
      break;
    }

    case NodeKind::Definition: {
      out += pad;
      out += ": ";
      serialize_inlines(node, out);
      out += "\n";
      break;
    }

    case NodeKind::Figure: {
      out += pad;
      out += ":::figure\n";
      for (NodeId c : node.children)
        serialize_node(doc, c, out, indent);
      out += pad;
      out += ":::\n\n";
      break;
    }

    case NodeKind::Caption: {
      out += pad;
      serialize_inlines(node, out);
      out += "\n\n";
      break;
    }

    case NodeKind::Biblio: {
      out += pad;
      out += ":::biblio\n";
      for (NodeId c : node.children)
        serialize_node(doc, c, out, indent);
      out += pad;
      out += ":::\n\n";
      break;
    }

    case NodeKind::BiblioEntry: {
      out += pad;
      out += "[^";
      out += node.label.empty() ? node.src : node.label;
      out += "]: ";
      serialize_inlines(node, out);
      out += "\n";
      break;
    }

    case NodeKind::Table: {
      out += pad;
      out += ":::table\n";
      for (NodeId row_id : node.children) {
        const Node& row = doc.get(row_id);
        if (row.kind != NodeKind::TableRow) {
          serialize_node(doc, row_id, out, indent);
          continue;
        }
        out += pad;
        out += "|";
        bool any_header = false;
        for (NodeId cell_id : row.children) {
          const Node& cell = doc.get(cell_id);
          if (cell.kind != NodeKind::TableCell) continue;
          if (cell.level > 0) any_header = true;
          out += " ";
          serialize_inlines(cell, out);
          out += " |";
        }
        out += "\n";
        if (any_header) {
          out += pad;
          out += "|";
          for (NodeId cell_id : row.children) {
            if (doc.get(cell_id).kind != NodeKind::TableCell) continue;
            out += " --- |";
          }
          out += "\n";
        }
      }
      out += pad;
      out += ":::\n\n";
      break;
    }

    case NodeKind::TableRow:
    case NodeKind::TableCell:
      for (NodeId c : node.children)
        serialize_node(doc, c, out, indent);
      break;

    case NodeKind::ListItem: {
      out += pad;
      // level > 0 means ordered list item with that display number
      if (node.level > 0) {
        out += std::to_string(node.level);
        out += ". ";
      } else {
        out += "- ";
      }
      if (node.label == "task" || node.label == "task-done") {
        out += (node.label == "task-done") ? "[x] " : "[ ] ";
      }
      serialize_inlines(node, out);
      out += "\n";
      // Nested lists under this item
      for (NodeId c : node.children) {
        if (doc.get(c).kind == NodeKind::List)
          serialize_node(doc, c, out, indent + 2);
      }
      break;
    }

    case NodeKind::Image: {
      out += pad;
      out += "![";
      out += node.alt;
      out += "](";
      out += node.src;
      out += ")";
      if (node.display_scale > 0.05f &&
          (node.display_scale < 0.99f || node.display_scale > 1.01f)) {
        out += "{scale=";
        out += std::to_string(node.display_scale);
        // trim excess float digits: keep ~2
        // (to_string is fine for round-trip)
        out += "}";
      }
      out += "\n\n";
      break;
    }

    case NodeKind::Stretch: {
      out += pad;
      out += ":::stretch";
      if (!node.summary.empty()) {
        out += " summary=\"";
        out += node.summary;
        out += "\"";
      }
      out += "\n";
      for (NodeId c : node.children) {
        serialize_node(doc, c, out, indent + 2);
      }
      out += pad;
      out += ":::\n\n";
      break;
    }

    case NodeKind::Columns: {
      out += pad;
      out += ":::columns\n";
      for (NodeId c : node.children) {
        serialize_node(doc, c, out, indent + 2);
      }
      out += pad;
      out += ":::\n\n";
      break;
    }

    case NodeKind::Column: {
      out += pad;
      out += ":::column";
      if (node.display_scale > 0.15f &&
          (node.display_scale < 0.99f || node.display_scale > 1.01f)) {
        out += " weight=";
        out += std::to_string(node.display_scale);
      }
      out += "\n";
      for (NodeId c : node.children) {
        serialize_node(doc, c, out, indent + 2);
      }
      out += pad;
      out += ":::\n\n";
      break;
    }

    case NodeKind::List:
      for (NodeId c : node.children) {
        serialize_node(doc, c, out, indent);
      }
      out += "\n";
      break;

    default:
      for (NodeId c : node.children) {
        serialize_node(doc, c, out, indent);
      }
      break;
  }
}

}  // namespace

std::string serialize_markup(const Document& doc) {
  std::string out;
  if (doc.root != INVALID_NODE) {
    serialize_node(doc, doc.root, out, 0);
  }
  return out;
}

// ---------------------------------------------------------------------------
// S-expression format (experimental)
// ---------------------------------------------------------------------------

namespace {

struct SExpr {
  enum class Kind { List, Atom, String } kind = Kind::Atom;
  std::string text;  // atom or string value
  std::vector<SExpr> kids;
};

class SExprParser {
 public:
  explicit SExprParser(std::string_view src) : s_(src) {}

  SExpr parse_top() {
    skip();
    return parse_one();
  }

 private:
  std::string_view s_;
  size_t i_ = 0;

  void skip() {
    while (i_ < s_.size()) {
      char c = s_[i_];
      if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
        ++i_;
        continue;
      }
      if (c == ';') {
        while (i_ < s_.size() && s_[i_] != '\n') ++i_;
        continue;
      }
      break;
    }
  }

  SExpr parse_one() {
    skip();
    if (i_ >= s_.size()) return {};
    if (s_[i_] == '(') {
      ++i_;
      SExpr list;
      list.kind = SExpr::Kind::List;
      for (;;) {
        skip();
        if (i_ >= s_.size()) break;
        if (s_[i_] == ')') {
          ++i_;
          break;
        }
        list.kids.push_back(parse_one());
      }
      return list;
    }
    if (s_[i_] == '"') {
      ++i_;
      SExpr str;
      str.kind = SExpr::Kind::String;
      while (i_ < s_.size() && s_[i_] != '"') {
        if (s_[i_] == '\\' && i_ + 1 < s_.size()) {
          char e = s_[i_ + 1];
          if (e == 'n')
            str.text.push_back('\n');
          else if (e == 't')
            str.text.push_back('\t');
          else
            str.text.push_back(e);
          i_ += 2;
        } else {
          str.text.push_back(s_[i_]);
          ++i_;
        }
      }
      if (i_ < s_.size() && s_[i_] == '"') ++i_;
      return str;
    }
    SExpr atom;
    atom.kind = SExpr::Kind::Atom;
    while (i_ < s_.size()) {
      char c = s_[i_];
      if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '(' ||
          c == ')' || c == ';')
        break;
      atom.text.push_back(c);
      ++i_;
    }
    return atom;
  }
};

bool is_atom(const SExpr& e, const char* name) {
  return e.kind == SExpr::Kind::Atom && e.text == name;
}

std::string sexpr_text(const SExpr& e) {
  if (e.kind == SExpr::Kind::String || e.kind == SExpr::Kind::Atom)
    return e.text;
  return {};
}

bool sexpr_bool(const SExpr& e, bool def = false) {
  if (e.kind != SExpr::Kind::Atom) return def;
  if (e.text == "#t" || e.text == "true") return true;
  if (e.text == "#f" || e.text == "false") return false;
  return def;
}

float sexpr_float(const SExpr& e, float def = 0.f) {
  if (e.kind != SExpr::Kind::Atom && e.kind != SExpr::Kind::String) return def;
  try {
    return std::stof(e.text);
  } catch (...) {
    return def;
  }
}

int sexpr_int(const SExpr& e, int def = 0) {
  if (e.kind != SExpr::Kind::Atom && e.kind != SExpr::Kind::String) return def;
  try {
    return std::stoi(e.text);
  } catch (...) {
    return def;
  }
}

size_t read_kwargs(const std::vector<SExpr>& kids, size_t start,
                   std::string* summary, bool* expanded, float* weight,
                   float* scale, std::string* alt, std::string* label = nullptr,
                   std::string* lang = nullptr) {
  size_t i = start;
  while (i + 1 < kids.size() && kids[i].kind == SExpr::Kind::Atom &&
         !kids[i].text.empty() && kids[i].text[0] == ':') {
    const std::string& k = kids[i].text;
    const SExpr& v = kids[i + 1];
    if (k == ":summary" && summary) *summary = sexpr_text(v);
    else if (k == ":expanded" && expanded) *expanded = sexpr_bool(v, true);
    else if (k == ":weight" && weight) *weight = sexpr_float(v, 1.f);
    else if (k == ":scale" && scale) *scale = sexpr_float(v, 1.f);
    else if (k == ":alt" && alt) *alt = sexpr_text(v);
    else if ((k == ":label" || k == ":anchor") && label) *label = sexpr_text(v);
    else if ((k == ":lang" || k == ":language") && lang) *lang = sexpr_text(v);
    // :title / :author handled at doc root only
    i += 2;
  }
  return i;
}

size_t read_meta_kwargs(const std::vector<SExpr>& kids, size_t start,
                        DocumentMeta& meta) {
  size_t i = start;
  while (i + 1 < kids.size() && kids[i].kind == SExpr::Kind::Atom &&
         !kids[i].text.empty() && kids[i].text[0] == ':') {
    const std::string& k = kids[i].text;
    const SExpr& v = kids[i + 1];
    if (k == ":title") meta.title = sexpr_text(v);
    else if (k == ":lang" || k == ":language") meta.lang = sexpr_text(v);
    else if (k == ":author") meta.author = sexpr_text(v);
    i += 2;
  }
  return i;
}

void append_inline_from_sexpr(Node& node, const SExpr& e) {
  if (e.kind == SExpr::Kind::String ||
      (e.kind == SExpr::Kind::Atom && !e.text.empty() && e.text[0] != ':')) {
    InlineSpan sp;
    sp.kind = InlineKind::Text;
    sp.text = sexpr_text(e);
    if (!sp.text.empty()) node.spans.push_back(std::move(sp));
    return;
  }
  if (e.kind != SExpr::Kind::List || e.kids.empty()) return;
  const SExpr& head = e.kids[0];
  if (!is_atom(head, "em") && !is_atom(head, "strong") &&
      !is_atom(head, "strike") && !is_atom(head, "s") &&
      !is_atom(head, "code") && !is_atom(head, "a") &&
      !is_atom(head, "cite") && !is_atom(head, "text"))
    return;

  if (is_atom(head, "a")) {
    InlineSpan sp;
    sp.kind = InlineKind::Link;
    size_t i = 1;
    if (i < e.kids.size() &&
        !(e.kids[i].kind == SExpr::Kind::Atom && !e.kids[i].text.empty() &&
          e.kids[i].text[0] == ':')) {
      sp.href = sexpr_text(e.kids[i]);
      ++i;
    }
    while (i + 1 < e.kids.size() && e.kids[i].kind == SExpr::Kind::Atom &&
           !e.kids[i].text.empty() && e.kids[i].text[0] == ':') {
      const std::string& k = e.kids[i].text;
      if (k == ":rel" || k == ":relation") sp.rel = sexpr_text(e.kids[i + 1]);
      else if (k == ":href") sp.href = sexpr_text(e.kids[i + 1]);
      i += 2;
    }
    for (; i < e.kids.size(); ++i) {
      if (e.kids[i].kind == SExpr::Kind::Atom && !e.kids[i].text.empty() &&
          e.kids[i].text[0] == ':') {
        if (i + 1 < e.kids.size()) ++i;
        continue;
      }
      sp.text += sexpr_text(e.kids[i]);
    }
    node.spans.push_back(std::move(sp));
    return;
  }
  if (is_atom(head, "cite")) {
    InlineSpan sp;
    sp.kind = InlineKind::Cite;
    size_t i = 1;
    while (i + 1 < e.kids.size() && e.kids[i].kind == SExpr::Kind::Atom &&
           !e.kids[i].text.empty() && e.kids[i].text[0] == ':') {
      const std::string& k = e.kids[i].text;
      if (k == ":key" || k == ":id") sp.href = sexpr_text(e.kids[i + 1]);
      i += 2;
    }
    // Bare first string may be the key when no :key
    if (sp.href.empty() && i < e.kids.size()) {
      sp.href = sexpr_text(e.kids[i]);
      ++i;
    }
    for (; i < e.kids.size(); ++i) {
      if (e.kids[i].kind == SExpr::Kind::Atom && !e.kids[i].text.empty() &&
          e.kids[i].text[0] == ':') {
        if (i + 1 < e.kids.size()) ++i;
        continue;
      }
      if (!sp.text.empty()) sp.text.push_back(' ');
      sp.text += sexpr_text(e.kids[i]);
    }
    if (sp.text.empty() && !sp.href.empty()) sp.text = sp.href;
    node.spans.push_back(std::move(sp));
    return;
  }
  InlineKind kind = InlineKind::Text;
  if (is_atom(head, "em"))
    kind = InlineKind::Emphasis;
  else if (is_atom(head, "strong"))
    kind = InlineKind::Strong;
  else if (is_atom(head, "strike") || is_atom(head, "s"))
    kind = InlineKind::Strike;
  else if (is_atom(head, "code"))
    kind = InlineKind::Code;
  InlineSpan sp;
  sp.kind = kind;
  for (size_t i = 1; i < e.kids.size(); ++i) sp.text += sexpr_text(e.kids[i]);
  node.spans.push_back(std::move(sp));
}

void fill_inlines(Node& node, const std::vector<SExpr>& kids, size_t from) {
  for (size_t i = from; i < kids.size(); ++i) {
    // Skip :keyword value pairs (e.g. :id 12) so they never become text.
    if (kids[i].kind == SExpr::Kind::Atom && !kids[i].text.empty() &&
        kids[i].text[0] == ':') {
      if (i + 1 < kids.size()) ++i;
      continue;
    }
    append_inline_from_sexpr(node, kids[i]);
  }
}

void parse_sexpr_node(Document& doc, NodeId parent, const SExpr& e);

void parse_sexpr_children(Document& doc, NodeId parent,
                          const std::vector<SExpr>& kids, size_t from) {
  for (size_t i = from; i < kids.size(); ++i) {
    const SExpr& c = kids[i];
    if (c.kind == SExpr::Kind::Atom && !c.text.empty() && c.text[0] == ':') {
      ++i;
      continue;
    }
    if (c.kind == SExpr::Kind::String) {
      NodeId n = doc.create(NodeKind::Paragraph, parent);
      append_inline_from_sexpr(doc.get(n), c);
      continue;
    }
    parse_sexpr_node(doc, parent, c);
  }
}

void parse_sexpr_node(Document& doc, NodeId parent, const SExpr& e) {
  if (e.kind != SExpr::Kind::List || e.kids.empty()) {
    if (e.kind == SExpr::Kind::String) {
      NodeId n = doc.create(NodeKind::Paragraph, parent);
      append_inline_from_sexpr(doc.get(n), e);
    }
    return;
  }
  const SExpr& head = e.kids[0];
  if (head.kind != SExpr::Kind::Atom) return;
  const std::string& tag = head.text;

  if (tag == "doc") {
    size_t i = read_meta_kwargs(e.kids, 1, doc.meta);
    parse_sexpr_children(doc, parent, e.kids, i);
    return;
  }

  if (tag == "h" || tag == "heading") {
    NodeId n = doc.create(NodeKind::Heading, parent);
    size_t i = 1;
    int level = 1;
    std::string label;
    // Optional kwargs before or after level
    if (i < e.kids.size() && e.kids[i].kind == SExpr::Kind::Atom &&
        !e.kids[i].text.empty() && e.kids[i].text[0] != ':')
      level = std::max(1, std::min(6, sexpr_int(e.kids[i++], 1)));
    i = read_kwargs(e.kids, i, nullptr, nullptr, nullptr, nullptr, nullptr, &label);
    if (i < e.kids.size() && e.kids[i].kind == SExpr::Kind::Atom &&
        e.kids[i].text.size() == 1 && e.kids[i].text[0] >= '1' &&
        e.kids[i].text[0] <= '6')
      level = std::max(1, std::min(6, sexpr_int(e.kids[i++], 1)));
    doc.get(n).level = level;
    if (!label.empty()) doc.get(n).label = label;
    fill_inlines(doc.get(n), e.kids, i);
    return;
  }

  if (tag == "p" || tag == "para" || tag == "paragraph") {
    NodeId n = doc.create(NodeKind::Paragraph, parent);
    std::string label;
    size_t i = read_kwargs(e.kids, 1, nullptr, nullptr, nullptr, nullptr, nullptr,
                           &label);
    if (!label.empty()) doc.get(n).label = label;
    fill_inlines(doc.get(n), e.kids, i);
    return;
  }

  if (tag == "quote" || tag == "blockquote") {
    NodeId n = doc.create(NodeKind::BlockQuote, parent);
    std::string label;
    size_t i = read_kwargs(e.kids, 1, nullptr, nullptr, nullptr, nullptr, nullptr,
                           &label);
    if (!label.empty()) doc.get(n).label = label;
    fill_inlines(doc.get(n), e.kids, i);
    return;
  }

  if (tag == "note" || tag == "aside" || tag == "callout") {
    NodeId n = doc.create(NodeKind::Note, parent);
    std::string label, kind;
    size_t i = read_kwargs(e.kids, 1, nullptr, nullptr, nullptr, nullptr, nullptr,
                           &label, &kind);
    if (!label.empty()) doc.get(n).label = label;
    if (!kind.empty()) doc.get(n).src = kind;  // note subtype
    fill_inlines(doc.get(n), e.kids, i);
    return;
  }

  if (tag == "codeblock" || tag == "pre") {
    NodeId n = doc.create(NodeKind::CodeBlock, parent);
    std::string label, lang;
    size_t i = read_kwargs(e.kids, 1, nullptr, nullptr, nullptr, nullptr, nullptr,
                           &label, &lang);
    if (!label.empty()) doc.get(n).label = label;
    if (!lang.empty()) doc.get(n).src = lang;
    // Remaining strings become the code body (joined)
    std::string body;
    for (; i < e.kids.size(); ++i) {
      if (e.kids[i].kind == SExpr::Kind::Atom && !e.kids[i].text.empty() &&
          e.kids[i].text[0] == ':') {
        ++i;
        continue;
      }
      if (!body.empty()) body.push_back('\n');
      body += sexpr_text(e.kids[i]);
    }
    if (!body.empty()) {
      InlineSpan sp;
      sp.kind = InlineKind::Code;
      sp.text = body;
      doc.get(n).spans.push_back(std::move(sp));
    }
    return;
  }

  // (code :lang "…" "body…") as block when :lang present or multiple lines
  if (tag == "code") {
    std::string label, lang;
    size_t i = read_kwargs(e.kids, 1, nullptr, nullptr, nullptr, nullptr, nullptr,
                           &label, &lang);
    bool looks_block = !lang.empty() || (e.kids.size() - i) > 1;
    if (looks_block || !lang.empty()) {
      NodeId n = doc.create(NodeKind::CodeBlock, parent);
      if (!label.empty()) doc.get(n).label = label;
      if (!lang.empty()) doc.get(n).src = lang;
      std::string body;
      for (; i < e.kids.size(); ++i) {
        if (e.kids[i].kind == SExpr::Kind::Atom && !e.kids[i].text.empty() &&
            e.kids[i].text[0] == ':') {
          ++i;
          continue;
        }
        if (!body.empty()) body.push_back('\n');
        body += sexpr_text(e.kids[i]);
      }
      InlineSpan sp;
      sp.kind = InlineKind::Code;
      sp.text = body;
      doc.get(n).spans.push_back(std::move(sp));
      return;
    }
    // else: single inline-ish — still create a short code block for top-level
    NodeId n = doc.create(NodeKind::CodeBlock, parent);
    fill_inlines(doc.get(n), e.kids, i);
    for (auto& sp : doc.get(n).spans) sp.kind = InlineKind::Code;
    return;
  }

  if (tag == "deflist" || tag == "dl") {
    NodeId n = doc.create(NodeKind::DefList, parent);
    std::string label;
    size_t i = read_kwargs(e.kids, 1, nullptr, nullptr, nullptr, nullptr, nullptr,
                           &label);
    if (!label.empty()) doc.get(n).label = label;
    parse_sexpr_children(doc, n, e.kids, i);
    return;
  }

  if (tag == "term" || tag == "dt") {
    NodeId n = doc.create(NodeKind::Term, parent);
    std::string label;
    size_t i = read_kwargs(e.kids, 1, nullptr, nullptr, nullptr, nullptr, nullptr,
                           &label);
    if (!label.empty()) doc.get(n).label = label;
    fill_inlines(doc.get(n), e.kids, i);
    return;
  }

  if (tag == "def" || tag == "dd" || tag == "definition") {
    NodeId n = doc.create(NodeKind::Definition, parent);
    std::string label;
    size_t i = read_kwargs(e.kids, 1, nullptr, nullptr, nullptr, nullptr, nullptr,
                           &label);
    if (!label.empty()) doc.get(n).label = label;
    // Allow nested blocks or plain inlines
    bool has_block = false;
    for (size_t j = i; j < e.kids.size(); ++j) {
      const SExpr& c = e.kids[j];
      if (c.kind == SExpr::Kind::List && !c.kids.empty() &&
          c.kids[0].kind == SExpr::Kind::Atom) {
        const std::string& ht = c.kids[0].text;
        if (ht != "em" && ht != "strong" && ht != "strike" && ht != "s" &&
            ht != "code" && ht != "a" && ht != "text") {
          has_block = true;
          break;
        }
      }
    }
    if (has_block)
      parse_sexpr_children(doc, n, e.kids, i);
    else
      fill_inlines(doc.get(n), e.kids, i);
    return;
  }

  if (tag == "figure") {
    NodeId n = doc.create(NodeKind::Figure, parent);
    std::string label;
    size_t i = read_kwargs(e.kids, 1, nullptr, nullptr, nullptr, nullptr, nullptr,
                           &label);
    if (!label.empty()) doc.get(n).label = label;
    parse_sexpr_children(doc, n, e.kids, i);
    return;
  }

  if (tag == "caption") {
    NodeId n = doc.create(NodeKind::Caption, parent);
    fill_inlines(doc.get(n), e.kids, 1);
    return;
  }

  if (tag == "biblio" || tag == "bibliography") {
    NodeId n = doc.create(NodeKind::Biblio, parent);
    std::string label;
    size_t i = read_kwargs(e.kids, 1, nullptr, nullptr, nullptr, nullptr, nullptr,
                           &label);
    if (!label.empty()) doc.get(n).label = label;
    parse_sexpr_children(doc, n, e.kids, i);
    return;
  }

  if (tag == "ref" || tag == "biblio-entry") {
    NodeId n = doc.create(NodeKind::BiblioEntry, parent);
    std::string label, key;
    size_t i = read_kwargs(e.kids, 1, nullptr, nullptr, nullptr, nullptr, nullptr,
                           &label, &key);
    // :key may land in lang slot of read_kwargs — also scan manually
    if (!label.empty()) doc.get(n).label = label;
    // Re-scan for :key
    for (size_t j = 1; j + 1 < e.kids.size(); ++j) {
      if (e.kids[j].kind == SExpr::Kind::Atom && e.kids[j].text == ":key") {
        doc.get(n).label = sexpr_text(e.kids[j + 1]);
        break;
      }
    }
    if (doc.get(n).label.empty() && !key.empty()) doc.get(n).label = key;
    // skip kwargs for inlines
    i = 1;
    while (i + 1 < e.kids.size() && e.kids[i].kind == SExpr::Kind::Atom &&
           !e.kids[i].text.empty() && e.kids[i].text[0] == ':')
      i += 2;
    fill_inlines(doc.get(n), e.kids, i);
    return;
  }

  if (tag == "hr") {
    doc.create(NodeKind::HorizontalRule, parent);
    return;
  }

  if (tag == "include" || tag == "transclude") {
    NodeId n = doc.create(NodeKind::Transclude, parent);
    std::string excerpt, label;
    size_t i = read_kwargs(e.kids, 1, &excerpt, nullptr, nullptr, nullptr, nullptr,
                           &label);
    // Also accept :excerpt explicitly via summary slot; re-scan for clarity
    for (size_t j = 1; j + 1 < e.kids.size(); ++j) {
      if (e.kids[j].kind != SExpr::Kind::Atom) continue;
      const std::string& k = e.kids[j].text;
      if (k == ":excerpt" || k == ":range") {
        excerpt = sexpr_text(e.kids[j + 1]);
      } else if (k == ":src" || k == ":href") {
        // handled below as positional too
      }
    }
    // Positional src: first non-kw string
    i = 1;
    while (i + 1 < e.kids.size() && e.kids[i].kind == SExpr::Kind::Atom &&
           !e.kids[i].text.empty() && e.kids[i].text[0] == ':')
      i += 2;
    if (i < e.kids.size()) {
      doc.get(n).src = sexpr_text(e.kids[i]);
      ++i;
    }
    if (!excerpt.empty()) doc.get(n).summary = excerpt;
    if (!label.empty()) doc.get(n).label = label;
    return;
  }

  if (tag == "table") {
    NodeId n = doc.create(NodeKind::Table, parent);
    std::string label;
    size_t i = read_kwargs(e.kids, 1, nullptr, nullptr, nullptr, nullptr, nullptr,
                           &label);
    if (!label.empty()) doc.get(n).label = label;
    parse_sexpr_children(doc, n, e.kids, i);
    return;
  }

  if (tag == "tr" || tag == "row") {
    NodeId n = doc.create(NodeKind::TableRow, parent);
    parse_sexpr_children(doc, n, e.kids, 1);
    return;
  }

  if (tag == "td" || tag == "th" || tag == "cell") {
    NodeId n = doc.create(NodeKind::TableCell, parent);
    doc.get(n).level = (tag == "th") ? 1 : 0;
    float weight = 1.f;
    size_t i =
        read_kwargs(e.kids, 1, nullptr, nullptr, &weight, nullptr, nullptr);
    if (weight > 0.15f && weight < 20.f) doc.get(n).display_scale = weight;
    while (i + 1 < e.kids.size() && e.kids[i].kind == SExpr::Kind::Atom &&
           !e.kids[i].text.empty() && e.kids[i].text[0] == ':')
      i += 2;
    // Mixed content: nested block forms become children; atoms/strings are
    // inlines. Prefer children when any List is present so nested
    // paragraphs/lists/code blocks round-trip.
    size_t first_list = e.kids.size();
    for (size_t j = i; j < e.kids.size(); ++j) {
      if (e.kids[j].kind == SExpr::Kind::List) {
        first_list = j;
        break;
      }
    }
    if (first_list < e.kids.size()) {
      if (first_list > i) {
        std::vector<SExpr> prefix(
            e.kids.begin() + static_cast<std::ptrdiff_t>(i),
            e.kids.begin() + static_cast<std::ptrdiff_t>(first_list));
        fill_inlines(doc.get(n), prefix, 0);
      }
      parse_sexpr_children(doc, n, e.kids, first_list);
    } else {
      fill_inlines(doc.get(n), e.kids, i);
    }
    return;
  }

  if (tag == "img" || tag == "image") {
    NodeId n = doc.create(NodeKind::Image, parent);
    size_t i = 1;
    // Optional leading kwargs (:id, …), then src string, then more kwargs.
    i = read_kwargs(e.kids, i, nullptr, nullptr, nullptr, nullptr, nullptr);
    if (i < e.kids.size()) {
      doc.get(n).src = sexpr_text(e.kids[i]);
      ++i;
    }
    std::string alt;
    float scale = 1.f;
    i = read_kwargs(e.kids, i, nullptr, nullptr, nullptr, &scale, &alt);
    doc.get(n).alt = alt;
    if (scale > 0.05f && scale < 10.f) doc.get(n).display_scale = scale;
    if (alt.empty() && i < e.kids.size() &&
        e.kids[i].kind == SExpr::Kind::String)
      doc.get(n).alt = sexpr_text(e.kids[i]);
    return;
  }

  if (tag == "stretch") {
    NodeId n = doc.create(NodeKind::Stretch, parent);
    std::string summary;
    bool expanded = false;
    size_t i =
        read_kwargs(e.kids, 1, &summary, &expanded, nullptr, nullptr, nullptr);
    doc.get(n).summary = summary;
    doc.get(n).expanded = expanded;
    parse_sexpr_children(doc, n, e.kids, i);
    return;
  }

  if (tag == "columns") {
    NodeId n = doc.create(NodeKind::Columns, parent);
    parse_sexpr_children(doc, n, e.kids, 1);
    return;
  }

  if (tag == "column") {
    NodeId n = doc.create(NodeKind::Column, parent);
    float weight = 1.f;
    size_t i =
        read_kwargs(e.kids, 1, nullptr, nullptr, &weight, nullptr, nullptr);
    doc.get(n).display_scale = weight > 0.05f ? weight : 1.f;
    parse_sexpr_children(doc, n, e.kids, i);
    return;
  }

  if (tag == "ul" || tag == "ol") {
    NodeId list = doc.create(NodeKind::List, parent);
    doc.get(list).level = (tag == "ol") ? 1 : 0;
    int auto_index = 0;
    for (size_t i = 1; i < e.kids.size(); ++i) {
      const SExpr& item = e.kids[i];
      if (item.kind != SExpr::Kind::List || item.kids.empty() ||
          !is_atom(item.kids[0], "li"))
        continue;
      NodeId li = doc.create(NodeKind::ListItem, list);
      size_t j = 1;
      int num = 0;
      if (j < item.kids.size() && item.kids[j].kind == SExpr::Kind::Atom &&
          !item.kids[j].text.empty() &&
          std::isdigit(static_cast<unsigned char>(item.kids[j].text[0]))) {
        num = sexpr_int(item.kids[j], 0);
        ++j;
      }
      if (tag == "ol") {
        ++auto_index;
        doc.get(li).level = num > 0 ? num : auto_index;
      } else {
        doc.get(li).level = 0;
      }
      for (; j < item.kids.size(); ++j) {
        const SExpr& part = item.kids[j];
        if (part.kind == SExpr::Kind::Atom && !part.text.empty() &&
            part.text[0] == ':') {
          const std::string& key = part.text;
          if (key == ":task") {
            doc.get(li).label = "task";
            if (j + 1 < item.kids.size()) ++j;  // consume #t
            continue;
          }
          if (key == ":done") {
            doc.get(li).label = "task-done";
            if (j + 1 < item.kids.size()) ++j;
            continue;
          }
          if (j + 1 < item.kids.size()) ++j;  // skip other kwargs (:id …)
          continue;
        }
        if (part.kind == SExpr::Kind::List && !part.kids.empty() &&
            part.kids[0].kind == SExpr::Kind::Atom &&
            (part.kids[0].text == "ul" || part.kids[0].text == "ol")) {
          parse_sexpr_node(doc, li, part);
        } else {
          append_inline_from_sexpr(doc.get(li), part);
        }
      }
    }
    return;
  }

  NodeId n = doc.create(NodeKind::Paragraph, parent);
  fill_inlines(doc.get(n), e.kids, 0);
}

void escape_sexpr_string(const std::string& s, std::string& out) {
  out.push_back('"');
  for (char c : s) {
    if (c == '"' || c == '\\') {
      out.push_back('\\');
      out.push_back(c);
    } else if (c == '\n') {
      out += "\\n";
    } else if (c == '\t') {
      out += "\\t";
    } else {
      out.push_back(c);
    }
  }
  out.push_back('"');
}

// Compact float for :scale / :weight (trim trailing zeros).
std::string format_sexpr_float(float v) {
  char buf[64];
  std::snprintf(buf, sizeof(buf), "%.4f", static_cast<double>(v));
  std::string s(buf);
  while (!s.empty() && s.back() == '0') s.pop_back();
  if (!s.empty() && s.back() == '.') s.pop_back();
  if (s.empty() || s == "-" || s == "-0") s = "0";
  return s;
}

void emit_node_id(const Node& node, std::string& out) {
  if (node.id == INVALID_NODE) return;
  out += " :id ";
  out += std::to_string(static_cast<unsigned long long>(node.id));
}

void emit_label(const Node& node, std::string& out) {
  if (node.label.empty()) return;
  out += " :label ";
  escape_sexpr_string(node.label, out);
}

void serialize_sexpr_inlines(const Node& node, std::string& out) {
  // Merge adjacent plain-text spans so output is not a string salad.
  std::string pending;
  auto flush_pending = [&]() {
    if (pending.empty()) return;
    out.push_back(' ');
    escape_sexpr_string(pending, out);
    pending.clear();
  };
  for (const auto& sp : node.spans) {
    if (sp.kind == InlineKind::Text) {
      pending += sp.text;
      continue;
    }
    flush_pending();
    out.push_back(' ');
    switch (sp.kind) {
      case InlineKind::Strong:
        out += "(strong ";
        escape_sexpr_string(sp.text, out);
        out += ")";
        break;
      case InlineKind::Emphasis:
        out += "(em ";
        escape_sexpr_string(sp.text, out);
        out += ")";
        break;
      case InlineKind::Strike:
        out += "(strike ";
        escape_sexpr_string(sp.text, out);
        out += ")";
        break;
      case InlineKind::Code:
        out += "(code ";
        escape_sexpr_string(sp.text, out);
        out += ")";
        break;
      case InlineKind::Link:
        out += "(a ";
        escape_sexpr_string(sp.href, out);
        if (!sp.rel.empty()) {
          out += " :rel ";
          escape_sexpr_string(sp.rel, out);
        }
        out.push_back(' ');
        escape_sexpr_string(sp.text, out);
        out += ")";
        break;
      case InlineKind::Cite:
        out += "(cite :key ";
        escape_sexpr_string(sp.href, out);
        out.push_back(' ');
        escape_sexpr_string(sp.text, out);
        out += ")";
        break;
      default:
        escape_sexpr_string(sp.text, out);
        break;
    }
  }
  flush_pending();
}

void serialize_sexpr_node(const Document& doc, NodeId id, std::string& out,
                          int indent);

void serialize_sexpr_children(const Document& doc, const Node& node,
                              std::string& out, int indent) {
  for (NodeId c : node.children) {
    out.push_back('\n');
    serialize_sexpr_node(doc, c, out, indent);
  }
}

void serialize_sexpr_node(const Document& doc, NodeId id, std::string& out,
                          int indent) {
  const Node& node = doc.get(id);
  std::string pad(static_cast<size_t>(indent), ' ');

  switch (node.kind) {
    case NodeKind::Document:
      out += pad;
      out += "(doc";
      if (!doc.meta.title.empty()) {
        out += " :title ";
        escape_sexpr_string(doc.meta.title, out);
      }
      if (!doc.meta.lang.empty()) {
        out += " :lang ";
        escape_sexpr_string(doc.meta.lang, out);
      }
      if (!doc.meta.author.empty()) {
        out += " :author ";
        escape_sexpr_string(doc.meta.author, out);
      }
      serialize_sexpr_children(doc, node, out, indent + 2);
      out += ")\n";
      break;

    case NodeKind::Heading: {
      out += pad;
      out += "(h ";
      out += std::to_string(std::max(1, std::min(6, node.level)));
      emit_node_id(node, out);
      emit_label(node, out);
      serialize_sexpr_inlines(node, out);
      out += ")";
      break;
    }

    case NodeKind::Paragraph: {
      out += pad;
      out += "(p";
      emit_node_id(node, out);
      emit_label(node, out);
      serialize_sexpr_inlines(node, out);
      out += ")";
      break;
    }

    case NodeKind::Note: {
      out += pad;
      out += "(note";
      emit_node_id(node, out);
      emit_label(node, out);
      if (!node.src.empty()) {
        out += " :lang ";  // subtype stored in src
        escape_sexpr_string(node.src, out);
      }
      serialize_sexpr_inlines(node, out);
      out += ")";
      break;
    }

    case NodeKind::CodeBlock: {
      out += pad;
      out += "(codeblock";
      emit_node_id(node, out);
      emit_label(node, out);
      if (!node.src.empty()) {
        out += " :lang ";
        escape_sexpr_string(node.src, out);
      }
      // Body as a single string (prefer plain text of spans)
      std::string body;
      for (const auto& sp : node.spans) body += sp.text;
      out.push_back(' ');
      escape_sexpr_string(body, out);
      out += ")";
      break;
    }

    case NodeKind::DefList: {
      out += pad;
      out += "(deflist";
      emit_node_id(node, out);
      emit_label(node, out);
      serialize_sexpr_children(doc, node, out, indent + 2);
      out.push_back('\n');
      out += pad;
      out += ")";
      break;
    }

    case NodeKind::Term: {
      out += pad;
      out += "(term";
      emit_node_id(node, out);
      emit_label(node, out);
      serialize_sexpr_inlines(node, out);
      out += ")";
      break;
    }

    case NodeKind::Definition: {
      out += pad;
      out += "(def";
      emit_node_id(node, out);
      emit_label(node, out);
      if (!node.spans.empty())
        serialize_sexpr_inlines(node, out);
      if (!node.children.empty())
        serialize_sexpr_children(doc, node, out, indent + 2);
      out += ")";
      break;
    }

    case NodeKind::Figure: {
      out += pad;
      out += "(figure";
      emit_node_id(node, out);
      emit_label(node, out);
      serialize_sexpr_children(doc, node, out, indent + 2);
      out.push_back('\n');
      out += pad;
      out += ")";
      break;
    }

    case NodeKind::Caption: {
      out += pad;
      out += "(caption";
      emit_node_id(node, out);
      serialize_sexpr_inlines(node, out);
      out += ")";
      break;
    }

    case NodeKind::Biblio: {
      out += pad;
      out += "(biblio";
      emit_node_id(node, out);
      emit_label(node, out);
      serialize_sexpr_children(doc, node, out, indent + 2);
      out.push_back('\n');
      out += pad;
      out += ")";
      break;
    }

    case NodeKind::BiblioEntry: {
      out += pad;
      out += "(ref";
      emit_node_id(node, out);
      if (!node.label.empty()) {
        out += " :key ";
        escape_sexpr_string(node.label, out);
      }
      serialize_sexpr_inlines(node, out);
      out += ")";
      break;
    }

    case NodeKind::BlockQuote: {
      out += pad;
      out += "(quote";
      emit_node_id(node, out);
      serialize_sexpr_inlines(node, out);
      out += ")";
      break;
    }

    case NodeKind::HorizontalRule:
      out += pad;
      out += "(hr";
      emit_node_id(node, out);
      out += ")";
      break;

    case NodeKind::Transclude: {
      out += pad;
      out += "(include";
      emit_node_id(node, out);
      emit_label(node, out);
      out += " ";
      escape_sexpr_string(node.src, out);
      if (!node.summary.empty()) {
        out += " :excerpt ";
        escape_sexpr_string(node.summary, out);
      }
      out += ")";
      break;
    }

    case NodeKind::Table: {
      out += pad;
      out += "(table";
      emit_node_id(node, out);
      emit_label(node, out);
      serialize_sexpr_children(doc, node, out, indent + 2);
      out.push_back('\n');
      out += pad;
      out += ")";
      break;
    }

    case NodeKind::TableRow: {
      out += pad;
      out += "(tr";
      emit_node_id(node, out);
      serialize_sexpr_children(doc, node, out, indent + 2);
      out += ")";
      break;
    }

    case NodeKind::TableCell: {
      out += pad;
      out += (node.level > 0) ? "(th" : "(td";
      emit_node_id(node, out);
      if (node.display_scale > 0.15f &&
          (node.display_scale < 0.99f || node.display_scale > 1.01f)) {
        out += " :weight ";
        out += format_sexpr_float(node.display_scale);
      }
      if (!node.children.empty()) {
        // Nested block cell: optional residual inlines then children
        serialize_sexpr_inlines(node, out);
        serialize_sexpr_children(doc, node, out, indent + 2);
      } else {
        serialize_sexpr_inlines(node, out);
      }
      out += ")";
      break;
    }

    case NodeKind::Image: {
      out += pad;
      out += "(img";
      emit_node_id(node, out);
      out += " ";
      escape_sexpr_string(node.src, out);
      if (!node.alt.empty()) {
        out += " :alt ";
        escape_sexpr_string(node.alt, out);
      }
      if (node.display_scale > 0.05f &&
          (node.display_scale < 0.99f || node.display_scale > 1.01f)) {
        out += " :scale ";
        out += format_sexpr_float(node.display_scale);
      }
      out += ")";
      break;
    }

    case NodeKind::Stretch: {
      out += pad;
      out += "(stretch";
      emit_node_id(node, out);
      if (!node.summary.empty()) {
        out += " :summary ";
        escape_sexpr_string(node.summary, out);
      }
      out += " :expanded ";
      out += node.expanded ? "#t" : "#f";
      serialize_sexpr_children(doc, node, out, indent + 2);
      out += ")";
      break;
    }

    case NodeKind::Columns: {
      out += pad;
      out += "(columns";
      emit_node_id(node, out);
      serialize_sexpr_children(doc, node, out, indent + 2);
      out += ")";
      break;
    }

    case NodeKind::Column: {
      out += pad;
      out += "(column";
      emit_node_id(node, out);
      if (node.display_scale > 0.05f &&
          (node.display_scale < 0.99f || node.display_scale > 1.01f)) {
        out += " :weight ";
        out += format_sexpr_float(node.display_scale);
      }
      serialize_sexpr_children(doc, node, out, indent + 2);
      out += ")";
      break;
    }

    case NodeKind::List: {
      out += pad;
      out += (node.level == 1) ? "(ol" : "(ul";
      emit_node_id(node, out);
      for (NodeId c : node.children) {
        out.push_back('\n');
        serialize_sexpr_node(doc, c, out, indent + 2);
      }
      out += ")";
      break;
    }

    case NodeKind::ListItem: {
      out += pad;
      out += "(li";
      if (node.level > 0) {
        out.push_back(' ');
        out += std::to_string(node.level);
      }
      emit_node_id(node, out);
      if (node.label == "task")
        out += " :task #t";
      else if (node.label == "task-done")
        out += " :done #t";
      serialize_sexpr_inlines(node, out);
      for (NodeId c : node.children) {
        if (doc.get(c).kind == NodeKind::List) {
          out.push_back('\n');
          serialize_sexpr_node(doc, c, out, indent + 2);
        }
      }
      out += ")";
      break;
    }

    default:
      break;
  }
}

}  // namespace

bool path_uses_sexpr(std::string_view path) {
  return path_ends_with_ci(path, ".sexp") || path_ends_with_ci(path, ".glyph") ||
         path_ends_with_ci(path, ".ss");
}

bool looks_like_sexpr(std::string_view source) {
  size_t i = 0;
  while (i < source.size()) {
    char c = source[i];
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
      ++i;
      continue;
    }
    if (c == ';') {
      while (i < source.size() && source[i] != '\n') ++i;
      continue;
    }
    return c == '(';
  }
  return false;
}

Document parse_sexpr(std::string_view source) {
  Document doc;
  NodeId root = doc.create(NodeKind::Document);
  SExprParser parser(source);
  SExpr top = parser.parse_top();
  if (top.kind == SExpr::Kind::List && !top.kids.empty() &&
      is_atom(top.kids[0], "doc")) {
    parse_sexpr_children(doc, root, top.kids, 1);
  } else if (top.kind == SExpr::Kind::List) {
    parse_sexpr_children(doc, root, top.kids, 0);
  } else if (top.kind == SExpr::Kind::String) {
    NodeId n = doc.create(NodeKind::Paragraph, root);
    append_inline_from_sexpr(doc.get(n), top);
  }
  return doc;
}

std::string serialize_sexpr(const Document& doc) {
  std::string out;
  if (doc.root != INVALID_NODE) serialize_sexpr_node(doc, doc.root, out, 0);
  return out;
}

Document parse_document(std::string_view source, std::string_view path) {
  if (path_uses_sexpr(path) || looks_like_sexpr(source))
    return parse_sexpr(source);
  return parse_markup(source);
}

std::string serialize_document(const Document& doc, std::string_view path) {
  if (path_uses_sexpr(path)) return serialize_sexpr(doc);
  return serialize_markup(doc);
}

}  // namespace xanatoo
