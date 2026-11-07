# Twitch auto-login (PKCE + localhost)

Add a "Connect with Twitch" button to the destination editor that opens the browser,
authorizes via Twitch OAuth (Authorization Code + PKCE), fetches the stream key, and
remembers the session so later reconnects skip the browser.

## Specs

- S1 (request, verbatim): "me gustaria tener autologin, es decir, que haya un boton para que abra el navegador y realice la conexion en automatico"
- S2 (decision): OAuth flow = Authorization Code + PKCE, redirect URI `http://localhost:8090/`.
- S3 (decision): "Sí, recordar sesión" — persist the Twitch refresh token so reconnecting is one click without the browser.
- S4 (scope): fetch the stream key with scope `channel:read:stream_key`; the editor key field is auto-filled.

## Tasks

- [x] T1 Build: add Qt6::Network to the module and link `Qt6::Network`. Route: inline. Commit `b0a28b9`.
- [x] T2 Core: vendored SHA-256, base64url, and PKCE (code verifier + S256 challenge). Route: inline (writer subagent failed). Commit `b0a28b9`.
- [x] T3 Core: Twitch OAuth request builders and token-response parsing (authorize URL, redirect parse, token exchange, refresh). Route: inline (writer subagent failed). Commit `9cf21ff`.
- [x] T4 Core: Twitch Helix request builders and response parsing (users, streams/key). Route: inline (writer subagent failed). Commit `9cf21ff`.
- [x] T5 Adapter: store/load the Twitch refresh token per destination through the existing SecretStore. Route: inline (writer subagent failed). Commit `9cf21ff`.
- [ ] T6 Dock: Qt transport — local listener on 8090, browser launch, HTTP via QNetworkAccessManager. Route: delegated. Commit ``.
- [ ] T7 Dock: "Connect with Twitch" button and wiring; refresh-or-authorize flow fills the key and stores the token. Client ID constant (placeholder until provided). Route: delegated. Commit ``.
- [ ] T8 Locale and docs: en-US strings for the button and status; README note. Route: inline. Commit ``.

## Log

- L1 (request, verbatim): "puedes ayudarme con lo siguiente, y empecemos con twitch, me gustaria tener autologin, es decir, que haya un boton para que abra el navegador y realice la conexion en automatico"
- L2 (decision): flow chosen = PKCE + localhost (`http://localhost:8090/`).
- L3 (decision): "Sí, recordar sesión" — persist refresh token.
- L4 (blocker, open): the Twitch app Client ID is pending from the user (dev.twitch.tv/console, OAuth redirect URL `http://localhost:8090/`). Core work proceeds with a placeholder; end-to-end auth needs the real value.
- L5: `gentle-ai-worker` failed twice with "assistant reported an error" (0 tool calls), so T2 was implemented inline as the degradation path. `pkce_test` passes the RFC 7636 appendix B vector; full suite 28/28 green.
