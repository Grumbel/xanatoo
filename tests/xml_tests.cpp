// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>

#include "xanatoo/parse.hpp"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

using namespace xanatoo;

namespace {

int failures = 0;

void check(bool condition, const char* message) {
  if (condition) return;
  std::cerr << "FAIL: " << message << '\n';
  ++failures;
}

template<class F>
void rejects(F action, const char* message) {
  try {
    action();
    check(false, message);
  } catch (const std::invalid_argument& e) {
    check(std::string(e.what()).find("byte") != std::string::npos, "error has byte offset");
  }
}

bool same_spans(const Node& a, const Node& b) {
  if (a.spans.size() != b.spans.size()) return false;
  for (size_t i = 0; i < a.spans.size(); ++i) {
    const InlineSpan& x = a.spans[i];
    const InlineSpan& y = b.spans[i];
    if (x.kind != y.kind || x.role != y.role || x.text != y.text ||
        x.href != y.href || x.rel != y.rel || x.datetime != y.datetime ||
        x.src != y.src || x.alt != y.alt || x.width != y.width ||
        x.height != y.height || x.display != y.display)
      return false;
    // Source node IDs may be reassigned. Check targets by their labels instead.
    if (x.target != INVALID_NODE || y.target != INVALID_NODE) {
      if (x.target == INVALID_NODE || y.target == INVALID_NODE) return false;
    }
  }
  return true;
}

bool same_tree(const Document& a, NodeId ax, const Document& b, NodeId bx) {
  const Node& x = a.get(ax);
  const Node& y = b.get(bx);
  if (x.kind != y.kind || x.level != y.level || x.col_span != y.col_span ||
      x.row_span != y.row_span || x.alt != y.alt || x.src != y.src ||
      x.expanded != y.expanded || x.summary != y.summary ||
      x.float_side != y.float_side || x.display_scale != y.display_scale ||
      x.label != y.label || x.tags != y.tags || x.style != y.style ||
      x.children.size() != y.children.size() || !same_spans(x, y))
    return false;
  for (size_t i = 0; i < x.spans.size(); ++i) {
    const auto xt = x.spans[i].target;
    const auto yt = y.spans[i].target;
    if (xt != INVALID_NODE && a.get(xt).label != b.get(yt).label)
      return false;
  }
  for (size_t i = 0; i < x.children.size(); ++i)
    if (!same_tree(a, x.children[i], b, y.children[i])) return false;
  return true;
}

void test_example() {
  const std::string_view source = R"XML(
<doc title='A &amp; &quot;B&quot;' lang="de" author="Schröder">
  <front-matter>title: Rock &amp; roll
author: Schröder
</front-matter>
  <section nid="22" label="intro" style="warning" tags="a:b">
    <header nid="57">Introduction &lt;3</header>
    <p nid="68">A &amp; B <em>emphasis</em> <strong>strong</strong> <strike>strike</strike>
<a href="#intro" target="57" rel="see-also" datetime="2026-11-07" role="metadata">go</a>
<code>1 &lt; 2</code><cite href="ref1">[1]</cite><fnref href="f1">[f1]</fnref>
<math src="x^2" display="true"/><img src="a.png?x=1&amp;y=2" alt="a 'diagram'" width="64" height="32" href="#intro"/>
<text role="metadata">tag</text></p>
    <ol><li level="1">One</li><li level="2">Two</li></ol>
    <table><tr><th colspan="2">A</th><td rowspan="3" scale="1.5">B</td></tr></table>
    <footnote label="f1"><p>Footnote &#x1F680; &#13; end</p></footnote>
  </section>
</doc>
)XML";
  const Document a = parse_xml(source);
  check(a.root != INVALID_NODE, "has doc root");
  check(a.meta.title == "A & \"B\"", "attribute entities");
  check(a.meta.author == "Schröder", "UTF-8 metadata");
  check(a.meta.front_matter.find("Rock & roll") != std::string::npos,
        "front matter preserved");
  const NodeId section = a.get(a.root).children.at(0);
  const NodeId head = a.get(section).children.at(0);
  const NodeId paragraph = a.get(section).children.at(1);
  bool linked = false;
  for (const InlineSpan& span : a.get(paragraph).spans) {
    if (span.kind == InlineKind::Link) {
      check(span.target == head, "remap noncontiguous source nid");
      linked = true;
    }
  }
  check(linked, "link parsed");
  const std::string xml = serialize_xml(a);
  const Document b = parse_xml(xml);
  check(same_tree(a, a.root, b, b.root), "semantic round trip");
  check(a.meta.title == b.meta.title && a.meta.lang == b.meta.lang &&
        a.meta.author == b.meta.author && a.meta.front_matter == b.meta.front_matter,
        "metadata round trip");
  check(xml == serialize_xml(b), "canonical XML serialization is stable");
  check(xml.find("&amp;") != std::string::npos, "XML escapes ampersand");
  check(xml.find("&#13;") != std::string::npos, "XML preserves carriage return");
}

void test_all_blocks() {
  Document a;
  const NodeId root = a.create(NodeKind::Document);
  const std::vector<NodeKind> kinds = {
      NodeKind::Heading, NodeKind::Header, NodeKind::Article,
      NodeKind::Section, NodeKind::Paragraph, NodeKind::List,
      NodeKind::ListItem, NodeKind::Image, NodeKind::Stretch,
      NodeKind::Columns, NodeKind::Column, NodeKind::BlockQuote,
      NodeKind::HorizontalRule, NodeKind::Transclude, NodeKind::CodeBlock,
      NodeKind::Note, NodeKind::DefList, NodeKind::Term,
      NodeKind::Definition, NodeKind::Figure, NodeKind::Caption,
      NodeKind::Biblio, NodeKind::BiblioEntry, NodeKind::Table,
      NodeKind::TableRow, NodeKind::TableCell, NodeKind::Thread,
      NodeKind::Message, NodeKind::Footnote, NodeKind::Collection,
      NodeKind::Chrome, NodeKind::Aside};
  for (size_t i = 0; i < kinds.size(); ++i) {
    const NodeId id = a.create(kinds[i], root);
    Node& n = a.get(id);
    n.label = "label" + std::to_string(i);
    n.style = "high-contrast";
    n.tags = "foo:bar";
    n.alt = "alternate";
    n.src = "src";
    n.summary = "summary";
    n.float_side = "left";
    n.level = 3;
    n.col_span = 4;
    n.row_span = 5;
    n.display_scale = 1.125f;
    n.expanded = false;
    if (kinds[i] == NodeKind::Heading || kinds[i] == NodeKind::Header ||
        kinds[i] == NodeKind::Paragraph || kinds[i] == NodeKind::ListItem ||
        kinds[i] == NodeKind::BlockQuote || kinds[i] == NodeKind::CodeBlock ||
        kinds[i] == NodeKind::Note || kinds[i] == NodeKind::Term ||
        kinds[i] == NodeKind::Definition || kinds[i] == NodeKind::Caption ||
        kinds[i] == NodeKind::BiblioEntry || kinds[i] == NodeKind::TableCell ||
        kinds[i] == NodeKind::Message) {
      InlineSpan span;
      span.text = "text < & >";
      n.spans.push_back(std::move(span));
    }
  }
  const Document b = parse_xml(serialize_xml(a));
  check(same_tree(a, root, b, b.root), "every node kind and attribute survives");
}

void test_escapes_and_dispatch() {
  const std::string text = "&<>'\"\n\r\tüber \xF0\x9F\x9A\x80";
  Document a;
  NodeId root = a.create(NodeKind::Document);
  a.meta.title = text;
  NodeId para = a.create(NodeKind::Paragraph, root);
  a.append_text(para, text);
  const std::string xml = serialize_xml(a);
  check(xml.find("&quot;") != std::string::npos &&
        xml.find("&apos;") != std::string::npos &&
        xml.find("&#10;") != std::string::npos &&
        xml.find("&#9;") != std::string::npos, "attribute escaping");
  Document b = parse_xml(xml);
  check(b.meta.title == text && b.get(b.get(b.root).children[0]).spans[0].text == text,
        "escapes preserve original bytes");
  check(path_uses_xml("a.XML") && !path_uses_xml("a.xhtml"), "extension check");
  const Document c = parse_xml("<doc title='one\r\ntwo'><p>first\r\nsecond"
                               " &#13; <em/></p></doc>");
  check(c.meta.title == "one two", "physical attribute newline normalization");
  check(c.get(c.get(c.root).children[0]).spans[0].text ==
        "first\nsecond \r ", "text newline normalization and numeric CR");
}

void test_rejections() {
  const std::vector<std::string> bad = {
      "<?xml version=\"1.0\"?><doc/>",
      "<!DOCTYPE doc [<!ENTITY x 'oops'>]><doc/>",
      "<doc><!-- hidden --></doc>",
      "<doc><![CDATA[x]]></doc>",
      "<doc><?noop x?></doc>",
      "<a:doc/>",
      "<doc xmlns=\"urn:test\"/>",
      "<doc a:b=\"1\"/>",
      "<doc title=hi/>",
      "<doc title='a'lang='en'/>",
      "<doc title=\"a\" title=\"b\"/>",
      "<doc title=\"a &copy;\"/>",
      "<doc title=\"&#0;\"/>",
      "<doc title=\"&#xD800;\"/>",
      "<doc title=\"&#x110000;\"/>",
      "<doc title=\"&amp\"/>",
      "<doc><p><em>bad <strong>nesting</strong></em></p></doc>",
      "<doc><p>Oops</doc>",
      "<doc><p>Oops</p></doc><doc/>",
      "<doc>unexpected text</doc>",
      "<doc><unknown/></doc>",
      "<doc><front-matter/><front-matter/></doc>",
      "<doc><p target=\"1\"/></doc>",
      "<doc><p nid=\"1\"/><p nid=\"1\"/></doc>",
      "<doc><p><a target=\"300\">bad reference</a></p></doc>",
      "<doc><p>\x01</p></doc>",
      "<doc><p>\xC0\x80</p></doc>",
      "<doc><p>]]></p></doc>",
  };
  for (const std::string& input : bad)
    rejects([&] { (void)parse_xml(input); }, input.c_str());
  Document a;
  a.create(NodeKind::Document);
  a.meta.title = "bad \x01";
  try {
    (void)serialize_xml(a);
    check(false, "export rejects illegal XML code points");
  } catch (const std::invalid_argument&) {}
}

}  // namespace

int main() {
  test_example();
  test_all_blocks();
  test_escapes_and_dispatch();
  test_rejections();
  if (failures) {
    std::cerr << failures << " XML tests failed\n";
    return EXIT_FAILURE;
  }
  std::cout << "XML tests passed\n";
  return EXIT_SUCCESS;
}
