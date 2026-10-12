// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
#pragma once

#include "xanatoo/types.hpp"

#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace xanatoo {

// Side SQLite store under ~/.config/xanatoo/sidestore.sqlite
// Documents stay immutable; view state (folds, …) lives here.

std::string sidestore_path();

// Stable key for a Section/Article: prefer .label, else child-index path "0.2.1".
std::string section_stable_key(const Document& doc, NodeId section);

// Load fold keys for a document (empty doc_key → no-op).
void load_section_folds(std::string_view doc_key,
                        std::unordered_set<std::string>& folded_keys);

void save_section_fold(std::string_view doc_key, std::string_view section_key,
                       bool folded);

// Map stable keys ↔ current NodeIds after a document load/layout.
void folds_keys_to_ids(const Document& doc,
                       const std::unordered_set<std::string>& keys,
                       std::unordered_set<NodeId>& out_ids);
void folds_ids_to_keys(const Document& doc,
                       const std::unordered_set<NodeId>& ids,
                       std::unordered_set<std::string>& out_keys);

}  // namespace xanatoo
