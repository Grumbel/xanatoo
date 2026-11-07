# Reddit Data API credentials

Xanatoo's native `reddit:` reader is read-only. Reddit may reject requests
without OAuth with HTTP 403, especially from hosted/cloud IP addresses.
Xanatoo supports **app-only OAuth** (the `client_credentials` grant), which
does **not** sign in to your Reddit account or access private messages.

## Obtain a client ID and secret

1. Sign in to Reddit with your personal account.
2. [Request Data API access](https://support.reddithelp.com/hc/en-us/requests/new?ticket_form_id=14868593862164).
   Explain that Xanatoo is an external, non-commercial, read-only desktop
   reader of public subreddit listings and comment threads. Include a link
   to its source code, `https://github.com/Grumbel/xanatoo`, and explain
   why Reddit's hosted Devvit platform cannot run a local desktop reader.
   **Approval is not guaranteed.**
3. If your account has permission to create or use a traditional OAuth app,
   open [Reddit app settings](https://www.reddit.com/prefs/apps), scroll to
   **are you a developer? create an app...**, and choose a **script** app for
   private use on your own computer (or select an existing confidential app).
   Give it a recognizable name, such as `xanatoo-personal`, and a short
   description. The form may require a redirect URI even though Xanatoo's
   app-only flow does not use one; `http://localhost:8080` is a placeholder.
   After creating it, the **client ID** appears just beneath its name and
   the **client secret** is labeled `secret`. Do not choose **installed app**
   for this workflow: it has no secret and uses a different OAuth grant.
4. In Xanatoo, open **Edit → Preferences → Reddit API**, enter the ID and
   secret, and click **Save credentials**. Reload the Reddit page. If the
   environment variables below are set, they override the saved pair.

The settings page links to the official access-request and app-management
pages. A Devvit account or project **does not** give you a traditional
client ID and secret for an external program.
The instructions above describe Reddit's
[legacy OAuth app flow](https://github.com/reddit-archive/reddit/wiki/OAuth2),
which Reddit may no longer allow for new registrations.

You can still use environment variables instead of the Preferences UI:

```sh
export XANATOO_REDDIT_CLIENT_ID='your-client-id'
export XANATOO_REDDIT_CLIENT_SECRET='your-client-secret'
```

The UI stores its credentials in `config_dir()/reddit.conf` (normally
`~/.config/xanatoo/reddit.conf`), separately from `view.conf`. On Unix,
the file is created or replaced atomically with owner-only permissions
(`0600`). It is plain text, **not encrypted**, so protect backups and do
not commit it. **Clear saved credentials** removes this file.
`--no-save` disables writing credentials. Environment variables are not
written to disk by Xanatoo. Credential changes take effect on the next
Reddit request.

## Limits and future availability

- A client ID/secret may not fix HTTP 403 when Reddit has not approved the
  application or blocks the IP. If a token request fails, Xanatoo reports
  the OAuth error along with the retrieval error.
- As of November 2026, Reddit's [Data API guidance](https://support.reddithelp.com/hc/en-us/articles/14945211791892-Developer-Platform-Accessing-Reddit-Data)
  requires approval for external API access. The
  [Devvit FAQ](https://developers.reddit.com/docs/guides/faq) states that
  Devvit credentials are not interchangeable with external API credentials.
- Reddit's [app registration announcement](https://developers.reddit.com/app-registration)
  says remaining public API access ends in **March 2027**, with restrictions
  beginning January 12, 2027. This configuration supports existing and
  approved legacy clients; it is not a long-term guarantee of native access.
- When the native reader fails with 403, Xanatoo also offers a link to the
  corresponding `.rss` feed. Reddit is retiring its RSS service as well.

Xanatoo cannot automatically create an approved API application: that
requires a Reddit account, Reddit's review/registration process, and
Reddit-issued credentials. Automating sign-up would also conflict with
Reddit's [rules on account automation](https://support.reddithelp.com/hc/en-us/articles/360043512931-Don-t-break-the-site).
