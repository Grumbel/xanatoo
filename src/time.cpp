// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>

#include "xanatoo/time.hpp"

#include <array>
#include <ctime>
#include <limits>

namespace xanatoo {

namespace {

// Days since 1970-01-01 (Gregorian), independent of the host's timezone.
int64_t days_from_civil(int y, unsigned m, unsigned d) {
  y -= m <= 2;
  const int era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(y - era * 400);
  const int month = static_cast<int>(m) + (m > 2 ? -3 : 9);
  const unsigned doy = (153 * static_cast<unsigned>(month) + 2) / 5 + d - 1;
  const unsigned yoe_days = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return static_cast<int64_t>(era) * 146097 + yoe_days - 719468;
}

bool digits(std::string_view s, size_t pos, size_t count, int& n) {
  if (pos + count > s.size()) return false;
  n = 0;
  for (size_t i = pos; i < pos + count; ++i) {
    if (s[i] < '0' || s[i] > '9') return false;
    n = n * 10 + s[i] - '0';
  }
  return true;
}

bool leap(int y) { return y % 4 == 0 && (y % 100 != 0 || y % 400 == 0); }

std::string relative_time(int64_t time, int64_t now) {
  if (time == now) return "just now";
  const bool future = time > now;
  uint64_t remaining = future ? static_cast<uint64_t>(time - now)
                              : static_cast<uint64_t>(now - time);
  struct Unit { uint64_t seconds; const char* name; };
  constexpr std::array<Unit, 4> units = {{{86400, "day"}, {3600, "hour"},
                                           {60, "minute"}, {1, "second"}}};
  std::string out;
  int parts = 0;
  for (const Unit& u : units) {
    if (parts == 2) break;
    const uint64_t amount = remaining / u.seconds;
    if (!amount) continue;
    if (!out.empty()) out += ", ";
    out += std::to_string(amount) + " " + u.name;
    if (amount != 1) out += "s";
    remaining %= u.seconds;
    ++parts;
  }
  return future ? "in " + out : out + " ago";
}

bool utc_time(std::time_t raw, std::tm& out) {
#if defined(_WIN32)
  return gmtime_s(&out, &raw) == 0;
#else
  return gmtime_r(&raw, &out) != nullptr;
#endif
}

bool local_time(std::time_t raw, std::tm& out) {
#if defined(_WIN32)
  return localtime_s(&out, &raw) == 0;
#else
  return localtime_r(&raw, &out) != nullptr;
#endif
}

}  // namespace

std::optional<int64_t> parse_timestamp(std::string_view iso) {
  // YYYY-MM-DD[T ]HH:MM[:SS][.fraction](Z|+HH:MM|-HH:MM)
  if (iso.size() < 17 || iso[4] != '-' || iso[7] != '-' ||
      (iso[10] != 'T' && iso[10] != 't' && iso[10] != ' ') ||
      iso[13] != ':') return std::nullopt;
  int year, month, day, hour, minute;
  if (!digits(iso, 0, 4, year) || !digits(iso, 5, 2, month) ||
      !digits(iso, 8, 2, day) || !digits(iso, 11, 2, hour) ||
      !digits(iso, 14, 2, minute)) return std::nullopt;
  if (month < 1 || month > 12 || hour > 23 || minute > 59) return std::nullopt;
  constexpr std::array<int, 12> mdays =
      {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (day < 1 || day > mdays[month - 1] + (month == 2 && leap(year)))
    return std::nullopt;
  size_t pos = 16;
  int seconds = 0;
  if (pos < iso.size() && iso[pos] == ':') {
    if (!digits(iso, pos + 1, 2, seconds) || seconds > 59)
      return std::nullopt;
    pos += 3;
  }
  if (pos < iso.size() && iso[pos] == '.') {
    ++pos;
    const size_t start = pos;
    while (pos < iso.size() && iso[pos] >= '0' && iso[pos] <= '9') ++pos;
    if (start == pos) return std::nullopt;
  }
  int offset_seconds = 0;
  if (pos < iso.size() && (iso[pos] == 'Z' || iso[pos] == 'z')) {
    ++pos;
  } else if (pos < iso.size() && (iso[pos] == '+' || iso[pos] == '-')) {
    int oh, om;
    if (!digits(iso, pos + 1, 2, oh) || pos + 3 >= iso.size() ||
        iso[pos + 3] != ':' || !digits(iso, pos + 4, 2, om) ||
        oh > 23 || om > 59) return std::nullopt;
    offset_seconds = (oh * 60 + om) * 60 * (iso[pos] == '+' ? 1 : -1);
    pos += 6;
  } else {
    return std::nullopt;  // an instant needs an explicit timezone
  }
  if (pos != iso.size()) return std::nullopt;
  return days_from_civil(year, static_cast<unsigned>(month),
                         static_cast<unsigned>(day)) * 86400 +
         hour * 3600 + minute * 60 + seconds - offset_seconds;
}

std::string timestamp_hover_text(std::string_view iso, int64_t now) {
  auto seconds = parse_timestamp(iso);
  if (!seconds || *seconds < std::numeric_limits<std::time_t>::min() ||
      *seconds > std::numeric_limits<std::time_t>::max()) return {};
  const std::time_t raw = static_cast<std::time_t>(*seconds);
  std::tm utc{}, local{};
  if (!utc_time(raw, utc) || !local_time(raw, local)) return {};
  char ubuf[80], lbuf[80];
  if (!std::strftime(ubuf, sizeof(ubuf), "%Y-%m-%d %H:%M:%S UTC", &utc) ||
      !std::strftime(lbuf, sizeof(lbuf), "%Y-%m-%d %H:%M:%S %Z", &local))
    return {};
  return std::string("UTC    ") + ubuf + "\nLocal  " + lbuf +
         "\n" + relative_time(*seconds, now);
}

}  // namespace xanatoo
