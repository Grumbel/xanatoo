// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
#pragma once

#include <string>

namespace xanatoo {

// App-only OAuth credentials for Reddit's read-only Data API client.
// Persisted separately from view.conf; never print or include in documents.
struct RedditCredentials {
  std::string client_id;
  std::string client_secret;

  bool complete() const { return !client_id.empty() && !client_secret.empty(); }
};

std::string reddit_credentials_path();
RedditCredentials load_reddit_credentials();
bool save_reddit_credentials(const RedditCredentials& credentials);
// If both environment variables are provided, they take precedence.
bool reddit_credentials_from_environment();
RedditCredentials effective_reddit_credentials();

}  // namespace xanatoo
