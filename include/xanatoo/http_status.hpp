// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
#pragma once

// Short, user-facing HTTP errors. These are explanations of the status code,
// not diagnoses of a particular server's reason for returning it.
// Header-only so every optional network client can use them independently.

#include <string>
#include <string_view>

namespace xanatoo {

inline constexpr std::string_view http_status_reason(int status) {
  switch (status) {
    case 300: return "Multiple Choices";
    case 301: return "Moved Permanently";
    case 302: return "Found";
    case 303: return "See Other";
    case 304: return "Not Modified";
    case 307: return "Temporary Redirect";
    case 308: return "Permanent Redirect";
    case 400: return "Bad Request";
    case 401: return "Unauthorized";
    case 402: return "Payment Required";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 406: return "Not Acceptable";
    case 407: return "Proxy Authentication Required";
    case 408: return "Request Timeout";
    case 409: return "Conflict";
    case 410: return "Gone";
    case 411: return "Length Required";
    case 412: return "Precondition Failed";
    case 413: return "Content Too Large";
    case 414: return "URI Too Long";
    case 415: return "Unsupported Media Type";
    case 416: return "Range Not Satisfiable";
    case 417: return "Expectation Failed";
    case 418: return "I'm a teapot";
    case 421: return "Misdirected Request";
    case 422: return "Unprocessable Content";
    case 423: return "Locked";
    case 424: return "Failed Dependency";
    case 425: return "Too Early";
    case 426: return "Upgrade Required";
    case 428: return "Precondition Required";
    case 429: return "Too Many Requests";
    case 431: return "Request Header Fields Too Large";
    case 451: return "Unavailable For Legal Reasons";
    case 500: return "Internal Server Error";
    case 501: return "Not Implemented";
    case 502: return "Bad Gateway";
    case 503: return "Service Unavailable";
    case 504: return "Gateway Timeout";
    case 505: return "HTTP Version Not Supported";
    case 506: return "Variant Also Negotiates";
    case 507: return "Insufficient Storage";
    case 508: return "Loop Detected";
    case 510: return "Not Extended";
    case 511: return "Network Authentication Required";
    default: return {};
  }
}

inline constexpr std::string_view http_status_explanation(int status) {
  switch (status) {
    case 300: return "The server offers several possible responses and cannot choose one automatically.";
    case 301: case 302: case 303: case 307: case 308:
      return "The server directed the request to another address, but the redirect was not completed.";
    case 304: return "The server says the cached copy has not changed.";
    case 400: return "The server could not understand the request. The URL or its parameters may be invalid.";
    case 401: return "This resource requires authentication, or the supplied credentials were not accepted.";
    case 402: return "The server says payment is required for this resource.";
    case 403: return "The server refused access. The site may require permission or block automated requests.";
    case 404: return "The requested page or record was not found at this address.";
    case 405: return "The server does not allow this kind of request for this resource.";
    case 406: return "The server cannot provide a response in a format the client requested.";
    case 407: return "A network proxy requires authentication before forwarding this request.";
    case 408: return "The server timed out while waiting for the request.";
    case 409: return "The request conflicts with the current state of the resource.";
    case 410: return "The resource has been removed and is no longer available at this address.";
    case 411: return "The server requires a request length that was not supplied.";
    case 412: return "A condition attached to the request was not met.";
    case 413: return "The request contains more data than the server will accept.";
    case 414: return "The requested address is longer than the server will accept.";
    case 415: return "The server cannot process the format of the submitted data.";
    case 416: return "The requested part of the resource is outside its available range.";
    case 417: return "The server cannot satisfy an expectation included in the request.";
    case 418: return "The server is returning the deliberately humorous teapot status.";
    case 421: return "The request reached a server that cannot answer for this address.";
    case 422: return "The server understood the request but cannot process its contents.";
    case 423: return "The resource is locked and cannot be changed right now.";
    case 424: return "The request failed because an earlier required action failed.";
    case 425: return "The server will not process this request yet because doing so may be unsafe.";
    case 426: return "The server requires a different protocol before it can respond.";
    case 428: return "The server requires a condition to prevent accidental conflicting changes.";
    case 429: return "Too many requests were sent. Wait before trying again.";
    case 431: return "The request headers are too large for the server to accept.";
    case 451: return "The resource is unavailable because of a legal restriction.";
    case 500: return "The server encountered an unexpected problem while handling the request.";
    case 501: return "The server does not support the requested operation.";
    case 502: return "A gateway or proxy received an invalid response from another server.";
    case 503: return "The service is temporarily unavailable, possibly due to maintenance or overload.";
    case 504: return "A gateway or proxy did not receive a response from another server in time.";
    case 505: return "The server does not support the HTTP protocol version used by the client.";
    case 506: return "The server encountered a configuration problem while selecting a response.";
    case 507: return "The server does not have enough storage to complete the request.";
    case 508: return "The server detected an endless loop while processing the request.";
    case 510: return "The server needs additional request extensions to complete this operation.";
    case 511: return "The network requires authentication, such as signing in to a Wi-Fi portal.";
    default:
      if (status >= 400 && status < 500)
        return "The server rejected the request or cannot provide this resource.";
      if (status >= 500 && status < 600)
        return "The server could not complete the request.";
      if (status >= 300 && status < 400)
        return "The server requested a different action or redirect.";
      return {};
  }
}

// Retain the numeric code for diagnostics and the official reason phrase for
// recognition, then explain the result in ordinary language.
inline std::string http_status_line(int status) {
  std::string result = "HTTP " + std::to_string(status);
  const std::string_view reason = http_status_reason(status);
  if (!reason.empty()) {
    result += ' ';
    result += reason;
  }
  return result;
}

inline std::string http_status_error(int status, std::string_view host = {}) {
  if (status == 0) return "No HTTP response was received.";
  std::string result = http_status_line(status);
  if (!host.empty()) {
    result += " from ";
    result += host;
  }
  const std::string_view explanation = http_status_explanation(status);
  if (!explanation.empty()) {
    result += " — ";
    result += explanation;
  }
  return result;
}

}  // namespace xanatoo
