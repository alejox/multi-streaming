# Plugin auto-update (download in background, install on OBS exit)

## Objective

The plugin checks GitHub Releases for a newer version, downloads the package for the
current platform in the background, verifies it, and installs it automatically when OBS
closes. The user sees the state in the dock and can turn automatic updates off.

## Problem / Why

Users must manually watch the GitHub releases page and replace plugin files by hand.
OBS cannot replace a loaded plugin while it runs (Windows locks the DLL), so installation
has to happen after OBS exits.

## Specs

- S1 (request, verbatim): "implementa al plugin la opcion de actualizar automaticamente"
- S2 (decision): "Descargar e instalar al cerrar" — check GitHub Releases at startup,
  download in background, install when OBS closes; dock notice plus a toggle to disable.
- S3 (constraint): release source is `https://api.github.com/repos/alejox/multi-streaming/releases/latest`;
  assets are `obs-multi-stream-<ver>-macos-arm64.zip`, `-windows-x64.zip`, `-linux-x86_64.tar.gz`.
- S4 (constraint): never leave the plugin half-installed: back up the current install and
  roll back on failure. If the install location is not writable, do not stage; show a
  manual "Download" link instead.
- S5 (constraint): verify the download (SHA-256 against the asset `digest` when the API
  provides it) before staging.

## Scope

In: version check, asset selection, download + verify + stage, install-on-exit helper per
platform, dock notice + toggle, persistence of the toggle, locale strings, README.
Out: delta updates, code signing of updates, prerelease channel, Program Files elevation.

## Tasks

- [x] T1 Core (pure, tested): semver compare, GitHub latest-release JSON parse, platform
  asset selection, digest parse, update decision. Route: delegated (writer, multi-file).
  Evidence: `a567832`; RED 6/11 failing on stubs, GREEN 11/11 (`update_check_test`).
- [x] T2 Adapter: Qt update service — check, download, verify SHA-256, stage into the OBS
  config dir; writability check of the install location. Route: delegated.
  Evidence: `6e3d339`; core `staging_test` RED 6/6 on stubs, GREEN 6/6; ctest 36/36; macOS build OK.
  Transport is libcurl (as `src/oauth/https_client`), not QNetworkAccessManager.
- [x] T3 Installer: install-on-exit via detached helper (waits for OBS PID, backs up,
  extracts, replaces, rolls back) for macOS/Linux (sh) and Windows (PowerShell); core
  plan/script generation tested. Route: delegated.
  Evidence: `a11158c`; `install_plan_test` RED 11/11 on stubs, GREEN 11/11 (runs the real sh
  script: install, broken archive, bad layout, injected copy failure → rollback); ctest 37/37;
  macOS build OK. PowerShell script only string-tested (no Windows runner here).
- [ ] T4 Dock: update banner (available / downloading / ready-on-close / failed / manual
  download link), "Install updates automatically" toggle persisted, locale strings.
  Route: delegated.
- [ ] T5 Docs: README section on auto-update. Route: inline.

## Acceptance criteria

- Newer release → package downloaded, verified, staged; dock shows "ready, installs when OBS closes".
- On OBS exit with a staged update, the helper installs it; next launch reports the new version.
- Same/older release, offline, API error, or toggle off → no download, no crash.
- Failed install restores the previous plugin files.

## Checks

- Core unit tests via the existing tests/ CTest suite (RED → GREEN for T1/T3 core).
- macOS plugin build (`cmake --preset macos` + build).

## Delivery

Strategy: `ask-on-risk`. Forecast ~900 authored lines → exceeds 400; chain strategy to ask
before the first commit past the budget.

## Log

- L1 (request, verbatim): "implementa al plugin la opcion de actualizar automaticamente"
- L2 (decision): install-on-exit chosen over notify-only options.
- L3: RDD off (global) — ordinary checks only.
- L4: T1 done (`a567832`): `src/update/update_check.*` + `tests/update/update_check_test.cpp`.
- L5: T2 done (`6e3d339`): `src/update/staging.*` (manifest, install root), `https_download.*`,
  `update_service.*`; staging in `plugin_config/obs-multi-stream/update/`.
- L6: T3 done (`a11158c`): launch at frontend EXIT (Qt alive, close not delayed), not
  obs_module_unload. Outcome in `update-result.txt`; a failed version falls back to manual.
