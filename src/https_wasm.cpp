// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>

// Browser build of https.hpp: requests go through the browser's fetch (via
// emscripten_fetch), so TLS, DNS and redirects belong to the browser and the
// page's CORS rules apply. Calls block, so they must run on a worker thread
// (all network clients already do; the build needs pthreads).
//
// Differences from the socket client (docs/PORTS.md "Network in the browser"):
//  * The agent string, Host, Connection … are forbidden request headers: ignored.
//  * Redirects are followed by the browser; `final_url` stays the requested
//    URL (relative links of a redirected page resolve against it).
//  * Only CORS-safelisted response headers are visible (content-type yes,
//    content-disposition no).
//  * Streaming reads the whole body first, then hands it to on_body.
//  * Raw TCP (Gemini, Nostr) does not exist: tcp_connect_host fails.

#include "xanatoo/https.hpp"
#include "xanatoo/http_url.hpp"
#include "xanatoo/tls.hpp"
#include "xanatoo/util.hpp"

#include <emscripten/fetch.h>

#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace xanatoo {

const char* TlsStream::backend() { return "browser fetch"; }
std::string TlsStream::version() { return "browser fetch (TLS by the browser)"; }

int tcp_connect_host(const std::string&, int, std::string* err) {
  if (err) *err = "raw TCP sockets are not available in the browser";
  return -1;
}

namespace {

std::string trimmed(std::string_view s) {
  trim_ascii(s);
  return std::string(s);
}

// Value of header `name` (lower case) in a "Name: value\r\n…" block.
std::string header_from_block(const std::string& block, const char* name) {
  size_t pos = 0;
  while (pos < block.size()) {
    size_t eol = block.find("\r\n", pos);
    if (eol == std::string::npos) eol = block.size();
    const std::string line = block.substr(pos, eol - pos);
    const size_t colon = line.find(':');
    if (colon != std::string::npos && ascii_lower(line.substr(0, colon)) == name)
      return trimmed(std::string_view(line).substr(colon + 1));
    pos = eol + 2;
  }
  return {};
}

// Request headers a page may set; everything else is forbidden or would
// only trigger a CORS preflight for no benefit.
bool allowed_request_header(const std::string& name) {
  const std::string n = ascii_lower(name);
  return n == "accept" || n == "accept-language" || n == "content-type" ||
         n == "authorization";
}

}  // namespace

std::optional<HttpResult> https_request(std::string_view method,
                                        std::string_view url,
                                        std::string_view /*agent*/,
                                        std::string_view extra_headers,
                                        std::string_view body_in,
                                        std::string* err,
                                        HttpRequestSecurity security) {
  const auto parsed = parse_http_url(url);
  if (!parsed) {
    if (err) *err = "invalid http(s) URL";
    return std::nullopt;
  }
  if (security.require_https && !parsed->secure) {
    if (err) *err = "HTTPS required";
    return std::nullopt;
  }

  std::vector<std::string> hdr_storage;  // name, value, name, value …
  size_t pos = 0;
  const std::string extra(extra_headers);
  while (pos < extra.size()) {
    size_t eol = extra.find("\r\n", pos);
    if (eol == std::string::npos) eol = extra.size();
    const std::string line = extra.substr(pos, eol - pos);
    const size_t colon = line.find(':');
    if (colon != std::string::npos) {
      std::string name = trimmed(std::string_view(line).substr(0, colon));
      if (allowed_request_header(name)) {
        hdr_storage.push_back(name);
        hdr_storage.push_back(trimmed(std::string_view(line).substr(colon + 1)));
      }
    }
    pos = eol + 2;
  }
  bool has_accept = false;
  for (size_t i = 0; i < hdr_storage.size(); i += 2)
    if (ascii_lower(hdr_storage[i]) == "accept") has_accept = true;
  if (!has_accept) {
    hdr_storage.push_back("Accept");
    hdr_storage.push_back("application/json, text/html, text/plain, */*");
  }
  std::vector<const char*> hdrs;
  for (const std::string& s : hdr_storage) hdrs.push_back(s.c_str());
  hdrs.push_back(nullptr);

  emscripten_fetch_attr_t attr;
  emscripten_fetch_attr_init(&attr);
  const std::string m(method);
  std::strncpy(attr.requestMethod, m.c_str(), sizeof(attr.requestMethod) - 1);
  attr.attributes = EMSCRIPTEN_FETCH_LOAD_TO_MEMORY | EMSCRIPTEN_FETCH_SYNCHRONOUS;
  attr.requestHeaders = hdrs.data();
  const std::string body(body_in);
  if (!body.empty()) {
    attr.requestData = body.data();
    attr.requestDataSize = body.size();
  }
  const std::string u(url);
  emscripten_fetch_t* f = emscripten_fetch(&attr, u.c_str());
  if (!f) {
    if (err) *err = "fetch could not start";
    return std::nullopt;
  }

  HttpResult out;
  out.status = f->status;
  std::optional<HttpResult> result;
  if (f->status == 0) {
    // The browser hides why: a network error and a CORS refusal look alike.
    if (err)
      *err = "request failed or blocked by the browser (the site probably "
             "does not allow cross-origin requests): " + u;
  } else if (f->numBytes > security.max_response_bytes) {
    if (err) *err = "HTTP response exceeds size limit";
  } else {
    out.body.assign(f->data, static_cast<size_t>(f->numBytes));
    const size_t hl = emscripten_fetch_get_response_headers_length(f);
    std::string block(hl + 1, '\0');
    emscripten_fetch_get_response_headers(f, block.data(), block.size());
    block.resize(std::strlen(block.c_str()));
    out.content_type = header_from_block(block, "content-type");
    out.location = header_from_block(block, "location");
    result = std::move(out);
  }
  emscripten_fetch_close(f);
  return result;
}

std::optional<HttpResult> https_get(std::string_view url,
                                    std::string_view agent,
                                    std::string* err) {
  return https_request("GET", url, agent, {}, {}, err);
}

std::optional<HttpResult> https_get_follow(std::string_view url,
                                           std::string_view agent,
                                           std::string* err,
                                           std::string* final_url) {
  if (final_url) *final_url = std::string(url);
  return https_get(url, agent, err);
}

bool https_get_follow_stream(std::string_view url, std::string_view agent,
                             const HttpHeaderCallback& on_headers,
                             const HttpBodyCallback& on_body,
                             std::string* err, std::string* final_url) {
  auto r = https_get(url, agent, err);
  if (!r) return false;
  if (final_url) *final_url = std::string(url);
  HttpStreamHeaders h;
  h.status = r->status;
  h.content_type = r->content_type;
  h.content_length = static_cast<int64_t>(r->body.size());
  if (!on_headers(h)) return true;
  constexpr size_t kChunk = 64 * 1024;
  for (size_t i = 0; i < r->body.size(); i += kChunk)
    if (!on_body(std::string_view(r->body).substr(i, kChunk))) break;
  return true;
}

}  // namespace xanatoo
