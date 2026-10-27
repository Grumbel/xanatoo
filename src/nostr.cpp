// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
//
// Read-only Nostr client: NIP-19 decode + WebSocket REQ for kind 1 notes,
// mapped into Thread / Message document nodes.

#include "xanatoo/nostr.hpp"
#include "xanatoo/util.hpp"

#include "xanatoo/https.hpp"
#include "xanatoo/tls.hpp"
#include "xanatoo/json_min.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <ctime>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <vector>

#if defined(XANATOO_NOSTR) && XANATOO_NOSTR
#include <arpa/inet.h>
#include <fcntl.h>
#include <poll.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#endif

namespace xanatoo {
namespace {


bool is_hex64(std::string_view s) {
  if (s.size() != 64) return false;
  for (char c : s) {
    if (!std::isxdigit(static_cast<unsigned char>(c))) return false;
  }
  return true;
}

std::string strip_nostr_prefix(std::string_view s) {
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())))
    s.remove_prefix(1);
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))
    s.remove_suffix(1);
  if (s.size() >= 6 && ascii_lower(s.substr(0, 6)) == "nostr:")
    s.remove_prefix(6);
  return std::string(s);
}

// ---- Bech32 (BIP-0173) ----------------------------------------------------

constexpr char kBech32Charset[] = "qpzry9x8gf2tvdw0s3jn54khce6mua7l";

int bech32_charset_rev(char c) {
  if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  const char* p = std::strchr(kBech32Charset, c);
  return p ? static_cast<int>(p - kBech32Charset) : -1;
}

uint32_t bech32_polymod(const std::vector<uint8_t>& values) {
  static const uint32_t GEN[] = {0x3b6a57b2, 0x26508e6d, 0x1ea119fa,
                                 0x3d4233dd, 0x2a1462b3};
  uint32_t chk = 1;
  for (uint8_t v : values) {
    uint8_t b = static_cast<uint8_t>(chk >> 25);
    chk = ((chk & 0x1ffffff) << 5) ^ v;
    for (int i = 0; i < 5; ++i)
      if ((b >> i) & 1) chk ^= GEN[i];
  }
  return chk;
}

std::vector<uint8_t> bech32_hrp_expand(std::string_view hrp) {
  std::vector<uint8_t> out;
  out.reserve(hrp.size() * 2 + 1);
  for (char c : hrp) out.push_back(static_cast<uint8_t>(c >> 5));
  out.push_back(0);
  for (char c : hrp) out.push_back(static_cast<uint8_t>(c & 31));
  return out;
}

bool bech32_verify(std::string_view hrp, const std::vector<uint8_t>& data) {
  auto values = bech32_hrp_expand(hrp);
  values.insert(values.end(), data.begin(), data.end());
  // NIP-19 uses bech32 (not bech32m): polymod == 1
  return bech32_polymod(values) == 1;
}

bool bech32_decode(std::string_view str, std::string& hrp,
                   std::vector<uint8_t>& data5) {
  if (str.size() < 8 || str.size() > 200) return false;
  for (char c : str)
    if (c < 33 || c > 126) return false;
  bool lower = false, upper = false;
  for (char c : str) {
    if (c >= 'a' && c <= 'z') lower = true;
    if (c >= 'A' && c <= 'Z') upper = true;
  }
  if (lower && upper) return false;

  std::string s = ascii_lower(str);
  auto pos = s.rfind('1');
  if (pos == std::string::npos || pos < 1 || pos + 7 > s.size()) return false;
  hrp = s.substr(0, pos);
  data5.clear();
  for (size_t i = pos + 1; i < s.size(); ++i) {
    int v = bech32_charset_rev(s[i]);
    if (v < 0) return false;
    data5.push_back(static_cast<uint8_t>(v));
  }
  if (data5.size() < 6) return false;
  if (!bech32_verify(hrp, data5)) return false;
  data5.resize(data5.size() - 6);  // drop checksum
  return true;
}

bool convert_bits(const std::vector<uint8_t>& in, int frombits, int tobits,
                  bool pad, std::vector<uint8_t>& out) {
  int acc = 0;
  int bits = 0;
  const int maxv = (1 << tobits) - 1;
  out.clear();
  for (uint8_t value : in) {
    if (value >> frombits) return false;
    acc = (acc << frombits) | value;
    bits += frombits;
    while (bits >= tobits) {
      bits -= tobits;
      out.push_back(static_cast<uint8_t>((acc >> bits) & maxv));
    }
  }
  if (pad) {
    if (bits) out.push_back(static_cast<uint8_t>((acc << (tobits - bits)) & maxv));
  } else if (bits >= frombits || ((acc << (tobits - bits)) & maxv)) {
    return false;
  }
  return true;
}

std::string bytes_to_hex(const uint8_t* p, size_t n) {
  static const char* hexd = "0123456789abcdef";
  std::string o;
  o.resize(n * 2);
  for (size_t i = 0; i < n; ++i) {
    o[i * 2] = hexd[p[i] >> 4];
    o[i * 2 + 1] = hexd[p[i] & 0xf];
  }
  return o;
}

// NIP-19 TLV: type (1) + length (1) + value
bool parse_nevent_tlvs(const std::vector<uint8_t>& bytes, std::string& event_hex,
                       std::vector<std::string>* relays) {
  size_t i = 0;
  event_hex.clear();
  while (i + 2 <= bytes.size()) {
    uint8_t t = bytes[i++];
    uint8_t len = bytes[i++];
    if (i + len > bytes.size()) return false;
    if (t == 0 && len == 32) {
      event_hex = bytes_to_hex(bytes.data() + i, 32);
    } else if (t == 1 && relays) {
      relays->emplace_back(reinterpret_cast<const char*>(bytes.data() + i), len);
    }
    i += len;
  }
  return !event_hex.empty();
}

}  // namespace

bool is_nostr_ref(std::string_view s) {
  std::string t = strip_nostr_prefix(s);
  if (t.empty()) return false;
  if (is_hex64(t)) return true;
  std::string low = ascii_lower(t);
  if (low == "recent" || low == "firehose" || low == "latest") return true;
  return low.rfind("note1", 0) == 0 || low.rfind("nevent1", 0) == 0;
}

std::optional<std::string> nostr_decode_event_id(
    std::string_view ref, std::vector<std::string>* relays) {
  std::string t = strip_nostr_prefix(ref);
  if (is_hex64(t)) return ascii_lower(t);

  std::string hrp;
  std::vector<uint8_t> data5;
  if (!bech32_decode(t, hrp, data5)) return std::nullopt;
  std::vector<uint8_t> data8;
  if (!convert_bits(data5, 5, 8, false, data8)) return std::nullopt;

  if (hrp == "note") {
    if (data8.size() != 32) return std::nullopt;
    return bytes_to_hex(data8.data(), 32);
  }
  if (hrp == "nevent") {
    std::string hex;
    if (!parse_nevent_tlvs(data8, hex, relays)) return std::nullopt;
    return hex;
  }
  return std::nullopt;
}

#if defined(XANATOO_NOSTR) && XANATOO_NOSTR

namespace {

struct NostrEvent {
  std::string id;
  std::string pubkey;
  int64_t created_at = 0;
  int kind = 0;
  std::string content;
  std::string reply_to;  // parent event id (hex), empty if root
  std::vector<std::string> e_tags;
};


// Pull "e" tag values from tags array (NIP-10 aware).
void parse_e_tags(std::string_view json, NostrEvent& ev) {
  size_t tags = json.find("\"tags\"");
  if (tags == std::string_view::npos) return;
  size_t arr = json.find('[', tags);
  if (arr == std::string_view::npos) return;
  size_t p = arr;
  std::string marked_reply;
  while ((p = json.find("\"e\"", p)) != std::string_view::npos) {
    size_t q = json.find('"', p + 3);
    if (q == std::string_view::npos) break;
    size_t s0 = json.find('"', q + 1);
    if (s0 == std::string_view::npos) break;
    size_t s1 = json.find('"', s0 + 1);
    if (s1 == std::string_view::npos) break;
    std::string_view id = json.substr(s0 + 1, s1 - s0 - 1);
    size_t tag_end = json.find(']', s1);
    bool is_reply_marker = false;
    if (tag_end != std::string_view::npos) {
      std::string_view tag_body = json.substr(s1, tag_end - s1);
      if (tag_body.find("\"reply\"") != std::string_view::npos)
        is_reply_marker = true;
    }
    if (is_hex64(id)) {
      std::string hex = ascii_lower(id);
      ev.e_tags.push_back(hex);
      if (is_reply_marker) marked_reply = hex;
      ev.reply_to = hex;
    }
    p = s1 + 1;
  }
  if (!marked_reply.empty()) ev.reply_to = marked_reply;
}

bool parse_event_json(std::string_view json, NostrEvent& ev) {
  ev.id = ascii_lower(json_str_field(json, "id"));
  ev.pubkey = ascii_lower(json_str_field(json, "pubkey"));
  ev.content = json_str_field(json, "content");
  ev.created_at = json_int_field(json, "created_at");
  ev.kind = static_cast<int>(json_int_field(json, "kind"));
  if (!is_hex64(ev.id)) return false;
  parse_e_tags(json, ev);
  return true;
}

struct WsConn {
  TlsStream tls;      // owns the socket once connected
  std::string carry;  // bytes after HTTP upgrade + partial WS frames

  void close() { tls.close(); }

  bool write_all(const void* data, size_t n) { return tls.write_all(data, n); }

  bool read_some(std::string& buf, size_t max_add) {
    char tmp[4096];
    size_t got = 0;
    while (got < max_add) {
      int r = tls.read(tmp, std::min(sizeof(tmp), max_add - got));
      if (r <= 0) return got > 0;
      buf.append(tmp, static_cast<size_t>(r));
      got += static_cast<size_t>(r);
      if (!tls.pending()) break;
    }
    return true;
  }
};

bool parse_wss_url(std::string_view url, std::string& host, std::string& path,
                   int& port) {
  host.clear();
  path = "/";
  port = 443;
  std::string u(url);
  if (u.rfind("wss://", 0) == 0)
    u = u.substr(6);
  else
    return false;
  size_t slash = u.find('/');
  std::string authority = slash == std::string::npos ? u : u.substr(0, slash);
  if (slash != std::string::npos) path = u.substr(slash);
  size_t colon = authority.rfind(':');
  if (colon != std::string::npos && authority.find(']') == std::string::npos) {
    host = authority.substr(0, colon);
    port = std::atoi(authority.substr(colon + 1).c_str());
    if (port <= 0) port = 443;
  } else {
    host = authority;
  }
  return !host.empty();
}

bool tcp_connect_timeout(int fd, const sockaddr* addr, socklen_t len,
                         int timeout_ms) {
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags < 0) return false;
  if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) return false;
  int rc = ::connect(fd, addr, len);
  if (rc == 0) {
    fcntl(fd, F_SETFL, flags);
    return true;
  }
  if (errno != EINPROGRESS) {
    fcntl(fd, F_SETFL, flags);
    return false;
  }
  pollfd pfd{};
  pfd.fd = fd;
  pfd.events = POLLOUT;
  rc = ::poll(&pfd, 1, timeout_ms);
  if (rc <= 0) {
    fcntl(fd, F_SETFL, flags);
    return false;
  }
  int soerr = 0;
  socklen_t sl = sizeof(soerr);
  if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &soerr, &sl) < 0 || soerr != 0) {
    fcntl(fd, F_SETFL, flags);
    return false;
  }
  fcntl(fd, F_SETFL, flags);
  return true;
}

std::string normalize_relay_url(std::string r) {
  while (!r.empty() && std::isspace(static_cast<unsigned char>(r.front())))
    r.erase(r.begin());
  while (!r.empty() && std::isspace(static_cast<unsigned char>(r.back())))
    r.pop_back();
  if (r.empty()) return {};
  // Plain host → wss
  if (r.rfind("ws://", 0) == 0) return {};  // refuse cleartext
  if (r.rfind("wss://", 0) != 0) r = "wss://" + r;
  while (r.size() > 8 && r.back() == '/') r.pop_back();
  return r;
}

bool ws_connect(WsConn& c, const std::string& url, std::string* err) {
  std::string host, path;
  int port = 443;
  if (!parse_wss_url(url, host, path, port)) {
    if (err) *err = "bad relay URL (need wss://)";
    return false;
  }
  if (path.empty()) path = "/";

  addrinfo hints{};
  hints.ai_socktype = SOCK_STREAM;
  // Prefer IPv4 first: many handheld networks mishandle AAAA for relays.
  hints.ai_family = AF_INET;
  addrinfo* res = nullptr;
  std::string port_s = std::to_string(port);
  int gai = getaddrinfo(host.c_str(), port_s.c_str(), &hints, &res);
  if (gai != 0) {
    hints.ai_family = AF_UNSPEC;
    gai = getaddrinfo(host.c_str(), port_s.c_str(), &hints, &res);
  }
  if (gai != 0) {
    if (err) *err = "DNS failed for " + host;
    return false;
  }

  int fd = -1;
  for (addrinfo* p = res; p; p = p->ai_next) {
    fd = ::socket(p->ai_family, p->ai_socktype, p->ai_protocol);
    if (fd < 0) continue;
    timeval tv{};
    tv.tv_sec = 5;
    tv.tv_usec = 0;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    if (tcp_connect_timeout(fd, p->ai_addr, p->ai_addrlen, 4000)) break;
    ::close(fd);
    fd = -1;
  }
  freeaddrinfo(res);
  if (fd < 0) {
    if (err) *err = "connect failed " + host;
    return false;
  }
  if (!c.tls.connect(fd, host, err)) return false;

  std::string key = "dGhlIHNhbXBsZSBub25jZQ==";
  std::ostringstream req;
  req << "GET " << path << " HTTP/1.1\r\n"
      << "Host: " << host;
  if (port != 443) req << ":" << port;
  req << "\r\n"
      << "Upgrade: websocket\r\n"
      << "Connection: Upgrade\r\n"
      << "Sec-WebSocket-Key: " << key << "\r\n"
      << "Sec-WebSocket-Version: 13\r\n"
      << "User-Agent: xanatoo/0.1\r\n"
      << "\r\n";
  std::string reqs = req.str();
  if (!c.write_all(reqs.data(), reqs.size())) {
    if (err) *err = "WS handshake write failed";
    return false;
  }
  std::string resp;
  for (int i = 0; i < 40 && resp.find("\r\n\r\n") == std::string::npos; ++i) {
    if (!c.read_some(resp, 8192)) break;
  }
  if (resp.find("101") == std::string::npos) {
    if (err) {
      std::string line = resp.substr(0, resp.find('\n'));
      if (line.size() > 80) line.resize(80);
      *err = "WS upgrade rejected by " + host + (line.empty() ? "" : (": " + line));
    }
    return false;
  }
  // Keep any bytes after the HTTP headers — first WS frames may already be here.
  size_t hdr_end = resp.find("\r\n\r\n");
  if (hdr_end != std::string::npos && hdr_end + 4 < resp.size())
    c.carry.assign(resp, hdr_end + 4, std::string::npos);
  return true;
}

bool ws_send_text(WsConn& c, std::string_view text) {
  std::vector<uint8_t> frame;
  frame.push_back(0x81);
  size_t n = text.size();
  if (n < 126) {
    frame.push_back(static_cast<uint8_t>(0x80 | n));
  } else if (n <= 0xffff) {
    frame.push_back(0x80 | 126);
    frame.push_back(static_cast<uint8_t>((n >> 8) & 0xff));
    frame.push_back(static_cast<uint8_t>(n & 0xff));
  } else {
    return false;
  }
  uint8_t mask[4] = {0x12, 0x34, 0x56, 0x78};
  frame.insert(frame.end(), mask, mask + 4);
  for (size_t i = 0; i < n; ++i)
    frame.push_back(static_cast<uint8_t>(text[i] ^ mask[i % 4]));
  return c.write_all(frame.data(), frame.size());
}

bool ws_read_text(WsConn& c, std::string& out, std::string& carry) {
  out.clear();
  for (int attempts = 0; attempts < 64; ++attempts) {
    if (carry.size() < 2) {
      if (!c.read_some(carry, 65536)) return false;
      if (carry.size() < 2) continue;
    }
    uint8_t b0 = static_cast<uint8_t>(carry[0]);
    uint8_t b1 = static_cast<uint8_t>(carry[1]);
    bool fin = (b0 & 0x80) != 0;
    int opcode = b0 & 0x0f;
    bool masked = (b1 & 0x80) != 0;
    uint64_t plen = b1 & 0x7f;
    size_t hdr = 2;
    if (plen == 126) {
      if (carry.size() < 4) {
        if (!c.read_some(carry, 65536)) return false;
        continue;
      }
      plen = (static_cast<uint8_t>(carry[2]) << 8) |
             static_cast<uint8_t>(carry[3]);
      hdr = 4;
    } else if (plen == 127) {
      return false;
    }
    size_t need = hdr + (masked ? 4 : 0) + static_cast<size_t>(plen);
    while (carry.size() < need) {
      if (!c.read_some(carry, 65536)) return false;
    }
    size_t payload_off = hdr;
    uint8_t mask[4] = {};
    if (masked) {
      for (int i = 0; i < 4; ++i)
        mask[i] = static_cast<uint8_t>(carry[hdr + i]);
      payload_off = hdr + 4;
    }
    std::string payload = carry.substr(payload_off, static_cast<size_t>(plen));
    if (masked) {
      for (size_t i = 0; i < payload.size(); ++i)
        payload[i] = static_cast<char>(payload[i] ^ mask[i % 4]);
    }
    carry.erase(0, need);

    if (opcode == 0x8) return false;
    if (opcode == 0x9) continue;
    if (opcode == 0x1 || opcode == 0x0) {
      out += payload;
      if (fin) return true;
      continue;
    }
  }
  return false;
}

std::string bech32_encode(std::string_view hrp, const std::vector<uint8_t>& data5) {
  auto values = bech32_hrp_expand(hrp);
  values.insert(values.end(), data5.begin(), data5.end());
  for (int i = 0; i < 6; ++i) values.push_back(0);
  uint32_t mod = bech32_polymod(values) ^ 1;
  std::string out(hrp);
  out.push_back('1');
  for (uint8_t d : data5) out.push_back(kBech32Charset[d]);
  for (int i = 0; i < 6; ++i)
    out.push_back(kBech32Charset[(mod >> (5 * (5 - i))) & 31]);
  return out;
}

std::string pubkey_to_npub(const std::string& pubkey_hex) {
  if (!is_hex64(pubkey_hex)) return pubkey_hex;
  std::vector<uint8_t> raw(32);
  auto hexv = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
  };
  for (size_t i = 0; i < 32; ++i)
    raw[i] = static_cast<uint8_t>((hexv(pubkey_hex[i * 2]) << 4) |
                                  hexv(pubkey_hex[i * 2 + 1]));
  std::vector<uint8_t> data5;
  if (!convert_bits(raw, 8, 5, true, data5))
    return pubkey_hex.substr(0, 8) + "…";
  return bech32_encode("npub", data5);
}

std::string short_npub(const std::string& pubkey_hex) {
  std::string npub = pubkey_to_npub(pubkey_hex);
  if (npub.size() > 16) return npub.substr(0, 12) + "…";
  return npub;
}

std::string profile_display_name(std::string_view content,
                                 const std::string& pubkey_hex) {
  std::string name = json_str_field(content, "display_name");
  if (name.empty()) name = json_str_field(content, "displayName");
  if (name.empty()) name = json_str_field(content, "name");
  if (!name.empty()) return name;
  return short_npub(pubkey_hex);
}

std::string format_time(int64_t ts) {
  if (ts <= 0) return {};
  time_t t = static_cast<time_t>(ts);
  char buf[64];
  struct tm tm {};
#if defined(_WIN32)
  gmtime_s(&tm, &t);
#else
  gmtime_r(&t, &tm);
#endif
  if (std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M UTC", &tm) == 0)
    return {};
  return buf;
}

Document events_to_document(const std::string& root_id,
                            std::vector<NostrEvent>& events,
                            const std::map<std::string, std::string>& profiles) {
  Document doc;
  NodeId root = doc.create(NodeKind::Document);
  NodeId thread = doc.create(NodeKind::Thread, root);
  doc.get(thread).label = root_id.substr(0, 12);
  doc.get(thread).summary = "Nostr discussion";

  for (const auto& e : events) {
    if (e.id == root_id && e.kind == 1) {
      std::string sub = e.content;
      if (sub.size() > 72) {
        sub.resize(72);
        sub += "…";
      }
      for (char& c : sub)
        if (c == '\n' || c == '\r') c = ' ';
      doc.get(thread).summary = sub.empty() ? "Nostr note" : sub;
      break;
    }
  }

  std::sort(events.begin(), events.end(),
            [](const NostrEvent& a, const NostrEvent& b) {
              if (a.created_at != b.created_at)
                return a.created_at < b.created_at;
              return a.id < b.id;
            });

  for (const auto& e : events) {
    if (e.kind != 1) continue;
    NodeId msg = doc.create(NodeKind::Message, thread);
    Node& n = doc.get(msg);
    n.label = e.id;
    auto pit = profiles.find(e.pubkey);
    n.src = (pit != profiles.end()) ? pit->second : short_npub(e.pubkey);
    n.summary = format_time(e.created_at);
    // Only mark real in-reply-to chains; do not invent a parent for
    // independent firehose notes (that made every line say "reply to #root").
    if (!e.reply_to.empty())
      n.alt = e.reply_to;
    // Body lives on a child Paragraph — Message layout/serialize only walk
    // children, not spans on the Message node itself.
    NodeId body = doc.create(NodeKind::Paragraph, msg);
    doc.append_text(body, e.content.empty() ? "(empty)" : e.content);
  }

  if (doc.get(thread).children.empty()) {
    NodeId msg = doc.create(NodeKind::Message, thread);
    doc.get(msg).src = "xanatoo";
    NodeId body = doc.create(NodeKind::Paragraph, msg);
    doc.append_text(body,
                    "No events returned from relays for this id.\n"
                    "The note may be rare, or relays may be unreachable.");
  }

  NodeId foot = doc.create(NodeKind::Paragraph, root);
  doc.append_text(foot,
                  "Nostr · read-only · Ctrl+L to open another note1/nevent1");
  return doc;
}

std::vector<std::string> default_relays() {
  // Ordered by general reliability; flaky hosts last (or omitted).
  return {
      "wss://nos.lol",
      "wss://relay.damus.io",
      "wss://relay.primal.net",
      "wss://nostr.bitcoiner.social",
      "wss://relay.snort.social",
      "wss://nostr.wine",
      // nostr.band often refuses or times out from some networks — try late
      "wss://relay.nostr.band",
  };
}

void collect_ws_events(WsConn& c, std::map<std::string, NostrEvent>& by_id,
                       int timeout_sec) {
  // Seed from any bytes left after the HTTP upgrade (first WS frames).
  std::string carry = std::move(c.carry);
  auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(timeout_sec);
  while (std::chrono::steady_clock::now() < deadline) {
    std::string msg;
    if (!ws_read_text(c, msg, carry)) break;
    if (msg.find("\"EOSE\"") != std::string::npos) break;
    if (msg.find("\"EVENT\"") == std::string::npos) continue;
    size_t brace = msg.find('{');
    if (brace == std::string::npos) continue;
    size_t endb = msg.rfind('}');
    if (endb == std::string::npos || endb <= brace) continue;
    NostrEvent ev;
    if (!parse_event_json(msg.substr(brace, endb - brace + 1), ev)) continue;
    by_id.insert_or_assign(ev.id, std::move(ev));
  }
}

void query_relay(const std::string& relay, const std::string& event_id,
                 std::map<std::string, NostrEvent>& by_id, std::string* last_err) {
  WsConn c;
  std::string err;
  if (!ws_connect(c, relay, &err)) {
    if (last_err) *last_err = err;
    return;
  }
  std::string sub = "xanatoo";
  std::string req =
      std::string("[\"REQ\",\"") + sub + "\"," +
      "{\"ids\":[\"" + event_id + "\"]}," +
      "{\"kinds\":[1],\"#e\":[\"" + event_id + "\"],\"limit\":80}]";
  if (!ws_send_text(c, req)) {
    if (last_err) *last_err = "WS send failed";
    return;
  }
  collect_ws_events(c, by_id, 5);
  ws_send_text(c, std::string("[\"CLOSE\",\"") + sub + "\"]");
}

void query_recent(const std::string& relay,
                  std::map<std::string, NostrEvent>& by_id, std::string* last_err) {
  WsConn c;
  std::string err;
  if (!ws_connect(c, relay, &err)) {
    if (last_err) *last_err = err;
    return;
  }
  std::string sub = "recent";
  // Newest kind-1 notes from this relay (firehose sample for nostr:recent).
  std::string req = std::string("[\"REQ\",\"") + sub +
                    "\",{\"kinds\":[1],\"limit\":25}]";
  if (!ws_send_text(c, req)) {
    if (last_err) *last_err = "WS send failed";
    return;
  }
  collect_ws_events(c, by_id, 6);
  ws_send_text(c, std::string("[\"CLOSE\",\"") + sub + "\"]");
}

void query_profiles(const std::string& relay,
                    const std::vector<std::string>& pubkeys,
                    std::map<std::string, std::string>& profiles,
                    std::string* last_err) {
  if (pubkeys.empty()) return;
  WsConn c;
  std::string err;
  if (!ws_connect(c, relay, &err)) {
    if (last_err) *last_err = err;
    return;
  }
  std::string authors;
  for (size_t i = 0; i < pubkeys.size() && i < 40; ++i) {
    if (i) authors += ",";
    authors += "\"" + pubkeys[i] + "\"";
  }
  std::string sub = "prof";
  std::string req = std::string("[\"REQ\",\"") + sub +
                    "\",{\"kinds\":[0],\"authors\":[" + authors +
                    "],\"limit\":40}]";
  if (!ws_send_text(c, req)) return;
  std::map<std::string, NostrEvent> tmp;
  collect_ws_events(c, tmp, 4);
  ws_send_text(c, std::string("[\"CLOSE\",\"") + sub + "\"]");
  for (auto& kv : tmp) {
    if (kv.second.kind != 0) continue;
    profiles[kv.second.pubkey] =
        profile_display_name(kv.second.content, kv.second.pubkey);
  }
}

}  // namespace

std::optional<Document> nostr_fetch_thread(std::string_view ref,
                                           std::string* err) {
  std::string stripped = strip_nostr_prefix(ref);
  std::string low = ascii_lower(stripped);
  const bool recent_mode =
      (low == "recent" || low == "firehose" || low == "latest");

  std::vector<std::string> hint_relays;
  std::optional<std::string> id;
  if (!recent_mode) {
    id = nostr_decode_event_id(ref, &hint_relays);
    if (!id) {
      if (err) *err = "not a valid note1/nevent1/hex event id (or nostr:recent)";
      return std::nullopt;
    }
  }

  std::vector<std::string> relays;
  auto add_relay = [&](std::string r) {
    r = normalize_relay_url(std::move(r));
    if (r.empty()) return;
    if (std::find(relays.begin(), relays.end(), r) == relays.end())
      relays.push_back(std::move(r));
  };
  for (const auto& r : hint_relays) add_relay(r);
  for (const auto& r : default_relays()) add_relay(r);

  std::map<std::string, NostrEvent> by_id;
  std::vector<std::string> failures;
  int tried = 0;
  for (const auto& r : relays) {
    if (tried >= 5) break;
    ++tried;
    std::string one_err;
    size_t before = by_id.size();
    if (recent_mode)
      query_recent(r, by_id, &one_err);
    else
      query_relay(r, *id, by_id, &one_err);
    if (by_id.size() > before) {
      std::cerr << "Nostr: got " << (by_id.size() - before)
                << " event(s) from " << r << "\n";
    } else if (!one_err.empty()) {
      failures.push_back(one_err);
      std::cerr << "Nostr: " << one_err << "\n";
    }
    if (recent_mode && by_id.size() >= 15) break;
    if (!recent_mode && id && by_id.count(*id) && by_id.size() >= 3) break;
  }

  if (by_id.empty()) {
    if (err) {
      if (recent_mode) {
        *err = "no recent notes from " + std::to_string(tried) + " relay(s)";
      } else {
        *err = "note not found on " + std::to_string(tried) +
               " relay(s) (id may be old or rare — try nostr:recent)";
      }
      if (!failures.empty()) {
        *err += " — ";
        *err += failures.front();
        if (failures.size() > 1)
          *err += " (+" + std::to_string(failures.size() - 1) + " more)";
      }
    }
    return std::nullopt;
  }

  std::string root_id = recent_mode ? by_id.begin()->second.id : *id;
  if (recent_mode) {
    // Prefer the newest event as the "thread" root for the firehose view.
    int64_t best_t = -1;
    for (const auto& kv : by_id) {
      if (kv.second.kind == 1 && kv.second.created_at >= best_t) {
        best_t = kv.second.created_at;
        root_id = kv.second.id;
      }
    }
  }

  std::vector<std::string> pubkeys;
  std::set<std::string> seen_pk;
  for (const auto& kv : by_id) {
    if (seen_pk.insert(kv.second.pubkey).second)
      pubkeys.push_back(kv.second.pubkey);
  }
  std::map<std::string, std::string> profiles;
  std::string prof_err;
  for (const auto& r : relays) {
    if (r.rfind("wss://", 0) != 0) continue;
    query_profiles(r, pubkeys, profiles, &prof_err);
    if (profiles.size() >= pubkeys.size()) break;
  }

  std::vector<NostrEvent> events;
  events.reserve(by_id.size());
  for (auto& kv : by_id) events.push_back(std::move(kv.second));

  Document doc = events_to_document(root_id, events, profiles);
  if (recent_mode) {
    // Clarify this is a sample firehose, not a single conversation.
    NodeId root = doc.root;
    if (root != INVALID_NODE) {
      for (NodeId c : doc.get(root).children) {
        if (doc.get(c).kind == NodeKind::Thread) {
          doc.get(c).summary = "Recent public notes (firehose sample)";
          doc.get(c).label = "recent";
          break;
        }
      }
    }
  }
  return doc;
}

#endif  // XANATOO_NOSTR

}  // namespace xanatoo
