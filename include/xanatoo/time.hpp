// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace xanatoo {

// ISO 8601 instant, including Z or an explicit +/-HH:MM offset.
// Never assumes a local timezone for an unqualified date or time.
std::optional<int64_t> parse_timestamp(std::string_view iso);

// Independent of the display text / hyperlink of an InlineSpan. 'now' is
// injected for deterministic tests; the GUI supplies current Unix seconds.
// Three lines: UTC, local time (system timezone), relative age or future.
std::string timestamp_hover_text(std::string_view iso, int64_t now);

}  // namespace xanatoo
