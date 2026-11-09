// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "xanatoo/annotations.hpp"
#include "xanatoo/revstore.hpp"

#include <cstdint>
#include <limits>
#include <unordered_set>
#include <utility>

namespace xanatoo {

namespace {

constexpr std::string_view kMagic = "xanatoo-annotation-layer 1\n";
constexpr std::string_view kKind = "annotation-layer-v1";

bool put_u32(std::string& dst, size_t n) {
  if (n > std::numeric_limits<uint32_t>::max()) return false;
  const auto v = static_cast<uint32_t>(n);
  for (int shift = 24; shift >= 0; shift -= 8)
    dst.push_back(static_cast<char>((v >> shift) & 0xff));
  return true;
}

bool put_string(std::string& dst, std::string_view text) {
  if (!put_u32(dst, text.size())) return false;
  dst.append(text);
  return true;
}

class Reader {
 public:
  explicit Reader(std::string_view s) : s_(s) {}

  bool magic() {
    if (!s_.starts_with(kMagic)) return false;
    s_.remove_prefix(kMagic.size());
    return true;
  }
  bool u32(uint32_t& value) {
    if (s_.size() < 4) return false;
    value = 0;
    for (int i = 0; i < 4; ++i)
      value = (value << 8) | static_cast<unsigned char>(s_[i]);
    s_.remove_prefix(4);
    return true;
  }
  bool str(std::string& value) {
    uint32_t n = 0;
    if (!u32(n) || n > s_.size()) return false;
    value.assign(s_.data(), n);
    s_.remove_prefix(n);
    return true;
  }
  bool done() const { return s_.empty(); }
  size_t remaining() const { return s_.size(); }

 private:
  std::string_view s_;
};

bool valid(const AnnotationLayer& layer) {
  if (!layer.id.starts_with("layer:")) return false;
  std::unordered_set<std::string> ids;
  for (const Annotation& a : layer.annotations) {
    if (a.id.empty() || a.kind.empty() || a.targets.empty() ||
        !ids.insert(a.id).second)
      return false;
    for (const AnnotationTarget& target : a.targets)
      if (target.document_key.empty()) return false;
  }
  return true;
}

}  // namespace

std::string new_annotation_id() { return "anno:" + new_doc_key().substr(4); }

std::string serialize_annotation_layer(const AnnotationLayer& layer) {
  if (!valid(layer)) return {};
  std::string data(kMagic);
  if (!put_string(data, layer.id) || !put_string(data, layer.owner) ||
      !put_string(data, layer.name) || !put_u32(data, layer.annotations.size()))
    return {};
  for (const Annotation& a : layer.annotations) {
    if (!put_string(data, a.id) || !put_string(data, a.kind) ||
        !put_string(data, a.body) || !put_string(data, a.style) ||
        !put_u32(data, a.targets.size())) return {};
    for (const AnnotationTarget& target : a.targets)
      if (!put_string(data, target.document_key) ||
          !put_string(data, target.revision) ||
          !put_string(data, target.locator)) return {};
  }
  return data;
}

std::optional<AnnotationLayer> parse_annotation_layer(std::string_view payload) {
  Reader r(payload);
  if (!r.magic()) return std::nullopt;
  AnnotationLayer layer;
  uint32_t count = 0;
  if (!r.str(layer.id) || !r.str(layer.owner) || !r.str(layer.name) ||
      !r.u32(count) || count > r.remaining() / 24) return std::nullopt;
  layer.annotations.reserve(count);
  for (uint32_t i = 0; i < count; ++i) {
    Annotation a;
    uint32_t targets = 0;
    if (!r.str(a.id) || !r.str(a.kind) || !r.str(a.body) ||
        !r.str(a.style) || !r.u32(targets) ||
        targets > r.remaining() / 12) return std::nullopt;
    a.targets.reserve(targets);
    for (uint32_t j = 0; j < targets; ++j) {
      AnnotationTarget t;
      if (!r.str(t.document_key) || !r.str(t.revision) ||
          !r.str(t.locator)) return std::nullopt;
      a.targets.push_back(std::move(t));
    }
    layer.annotations.push_back(std::move(a));
  }
  if (!r.done() || !valid(layer)) return std::nullopt;
  return layer;
}

std::optional<AnnotationLayer> create_annotation_layer(std::string_view owner,
                                                        std::string_view name) {
  AnnotationLayer layer;
  layer.id = "layer:" + new_doc_key().substr(4);
  layer.owner = owner;
  layer.name = name;
  if (!save_annotation_layer(layer)) return std::nullopt;
  return layer;
}

std::optional<AnnotationLayer> load_annotation_layer(std::string_view id,
                                                      std::string_view revision) {
  const auto object = revision.empty() ? find_object_head(id)
                                       : load_object_revision(revision);
  if (!object || object->key != id || object->kind != kKind) return std::nullopt;
  auto layer = parse_annotation_layer(object->data);
  if (!layer || layer->id != id) return std::nullopt;
  layer->revision = object->id;
  return layer;
}

std::vector<AnnotationLayer> list_annotation_layers() {
  std::vector<AnnotationLayer> layers;
  for (const ObjectRevision& object : list_object_heads(kKind)) {
    auto layer = parse_annotation_layer(object.data);
    if (!layer || layer->id != object.key) continue;
    layer->revision = object.id;
    layers.push_back(std::move(*layer));
  }
  return layers;
}

bool save_annotation_layer(AnnotationLayer& layer) {
  const std::string data = serialize_annotation_layer(layer);
  if (data.empty()) return false;
  const std::string revision = commit_object_revision(layer.id, kKind, data,
                                                       layer.revision);
  if (revision.empty()) return false;
  layer.revision = revision;
  return true;
}

}  // namespace xanatoo
