// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
#pragma once

// Credentials for catalog adapters. Never embed these in a search:/media:
// address or a document. Values are read on demand so changes take effect
// without restarting the application.

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace xanatoo {

enum class CatalogKey : size_t {
  IgdbClientId,
  IgdbAccessToken,
  RawgApiKey,
  TmdbReadToken,
  TmdbApiKey,
  DiscogsToken,
  Count
};

constexpr size_t catalog_key_count = static_cast<size_t>(CatalogKey::Count);

struct CatalogKeyInfo {
  const char* setting;
  const char* environment;
};

inline constexpr std::array<CatalogKeyInfo, catalog_key_count> catalog_key_info = {{
    {"igdb_client_id", "XANATOO_IGDB_CLIENT_ID"},
    {"igdb_access_token", "XANATOO_IGDB_ACCESS_TOKEN"},
    {"rawg_api_key", "XANATOO_RAWG_API_KEY"},
    {"tmdb_read_token", "XANATOO_TMDB_READ_TOKEN"},
    {"tmdb_api_key", "XANATOO_TMDB_API_KEY"},
    {"discogs_token", "XANATOO_DISCOGS_TOKEN"},
}};

constexpr size_t catalog_key_index(CatalogKey key) {
  return static_cast<size_t>(key);
}

struct CatalogCredentials {
  std::array<std::string, catalog_key_count> values;
  std::string& operator[](CatalogKey key) { return values[catalog_key_index(key)]; }
  const std::string& operator[](CatalogKey key) const {
    return values[catalog_key_index(key)];
  }
  bool empty() const;
};

// ASCII header/query-safe tokens (whitespace and control characters rejected).
bool valid_catalog_credential(std::string_view value);
std::string catalog_credentials_path();
CatalogCredentials load_catalog_credentials();
bool save_catalog_credentials(const CatalogCredentials& credentials);
bool catalog_credential_from_environment(CatalogKey key);
std::string effective_catalog_credential(CatalogKey key);

// Transient UI command payload, never persisted to session/history.
std::string catalog_credentials_command(const CatalogCredentials& credentials);
std::optional<CatalogCredentials> parse_catalog_credentials_command(std::string_view text);

}  // namespace xanatoo
