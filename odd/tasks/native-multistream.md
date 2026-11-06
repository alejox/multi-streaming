# Feature: native-multistream

Locator: `odd/tasks/native-multistream.md` | Engram topic: `odd/native-multistream/tasks`

## Objective

An OBS Studio plugin that streams one OBS session to many destinations at once, each fed by either the main (horizontal) mix or a dedicated vertical canvas.

## Problem and why

OBS natively streams to one service. Aitum Vertical and Multiple RTMP Outputs each solve part of this, but both keep stream keys in plain JSON and neither sets reconnect policy. We want our own native implementation, using current libobs APIs, that we control end to end.

## Scope

In scope:

- Dock UI listing destinations with per-destination start/stop, status, bitrate, and dropped frames.
- RTMP/RTMPS custom destinations (URL + key), each bound to the `main` or `vertical` video source.
- Vertical canvas with its own resolution, scenes, preview, and basic source composition.
- Encoder sharing when settings match; dedicated encoders otherwise.
- Optional start/stop in sync with the main OBS stream.
- Per-destination reconnect settings and failure reporting.
- Stream keys stored outside plain JSON (macOS Keychain first, behind a port).

Out of scope for this feature:

- Platform OAuth, chat, vertical recording or replay buffer, WHIP, and Windows/Linux packaging. The code stays portable where cheap, but only macOS arm64 is verified.
- Parity with Aitum's complete Scenes, Sources, and Transitions docks.

## Constraints

- OBS 33.0.1 is the build and runtime target. The Canvas API is officially unstable, so pin the build to that version and add a runtime `obs_get_version()` guard.
- License: GPL-2.0-or-later. Learn from the referenced plugins; write our own code. Any copied portion must keep its notices.
- Screaming/Hexagonal layout: a pure C++ core without OBS or Qt headers, with OBS, Qt, and Keychain as adapters.
- Artifacts are in English. Conventional Commits, without AI attribution.
- Do not start any real stream to a real service. Runtime proof uses a local RTMP sink only.

## Architecture

```text
src/
  destinations/      Pure core: Destination, validation, config codec, ports, reconnect policy, stats math, session state
  streaming/         libobs adapter: outputs, services, encoders, encoder sharing, main-stream sync
  vertical_canvas/   libobs adapter: canvas lifecycle, scenes, composition, render
  secrets/           SecretStore implementations (Keychain, in-memory, unavailable)
  persistence/       ConfigRepository implementation (atomic JSON file)
  dock/              Qt UI: destinations dock, editor dialog, vertical preview
  plugin_main.cpp    Composition root
tests/               Core tests through CTest, with no OBS or Qt dependency
```

## Reference evidence

Commits read: obs-vertical-canvas `0d63170`, obs-multi-rtmp `a26d9b0`, obs-aitum-multistream `93add98`, obs-plugintemplate `3e7d7ac`, obs-studio tag `33.0.1`.

- Canvas: `obs_frontend_add_canvas` + `obs_canvas_reset_video`, feeding encoders from `obs_canvas_get_video` (Aitum Vertical).
- Per-target stats and reconnect signals (Multiple RTMP).
- libobs reconnect defaults: 20 retries, 2 s base, exponent about 1.5, 15-minute cap (`obs-output.c`).

Unverified going in: Canvas API behavior from this plugin on OBS 33 arm64; VideoToolbox concurrent session limits; Qt licensing terms.

## Tasks

- [ ] T1 Scaffold: buildable plugin with pinned OBS 33.0.1, core test target, and proof it loads in OBS. Route: delegated (preparation trigger, 2+ files). Commit `4773b63`. Build and install done; runtime load proof pending because OBS was already running.
- [x] T2 Destinations core: model, validation, config codec, reconnect policy, stats math, session state (test-first). Route: delegated (same writer). Commit `56b8ddc`.
- [x] T3 Secrets and persistence: `SecretStore` port, Keychain adapter, config file repository with no keys in JSON (test-first). Route: delegated (same writer). Commit `669e6ed`.
- [ ] T4 Streaming engine: per-destination output/service/encoders from the main mix, encoder sharing, reconnect, stats, main-stream sync. Route: delegated.
- [ ] T5 Destinations dock: list, editor, status, stats, thread-safe UI marshaling. Route: delegated.
- [ ] T6 Vertical canvas: canvas lifecycle, resolution settings, scenes, preview, basic composition, engine binding. Route: delegated.
- [ ] T7 End-to-end proof and docs: local RTMP sink with main and vertical at once, README, install and license notes. Route: delegated.

## Acceptance criteria

- The plugin loads in OBS 33.0.1 on macOS arm64 without errors and without disturbing existing plugins.
- Two or more destinations stream at the same time to a local RTMP sink, from main and vertical, and the received resolutions match (checked with `ffprobe`).
- Stopping or failing one destination leaves the others and the main stream running.
- `rg` over the saved config finds no stream key.
- Core tests pass through CTest.
- Scene collections, the main stream, and configuration are unchanged when the plugin is removed. Any canvas residue is documented.

## Checks

- Core: `ctest` from the configured test build.
- Plugin: template macOS preset build, install to `~/Library/Application Support/obs-studio/plugins`, launch OBS, read its log.
- Keychain writes during automated checks use a test-only service name and clean up after themselves.

## Delivery

- Strategy: `exception-ok`. This is a greenfield repository without a remote, so there is no reviewer or base to slice against yet. Forecast: about 3,000 authored lines. Revisit with `chained-pr` if a remote and PR flow are added.
- Push, PR, and merge remain the user's decisions.
- RDD: off (global), so no native review. Verification tier follows `gentle-ai review assess` over each writer diff.

## Progress

Branch `feat/native-multistream`. T2 and T3 are complete. T1 is built and installed, and its load proof is still pending.

Build decisions:

- arm64 only: OBS 33.0.1 itself builds single-architecture macOS, and the installed OBS.app is arm64. Minimum macOS is 13.0, matching `LSMinimumSystemVersion` and the OBS preset. Building needs SDK and Xcode 26.5 or later (OBS check).
- OBS 33.0.1 declares its dependencies in `CMakePresets.json`, not `buildspec.json`: prebuilt and Qt6 `2026-10-23` (Qt 6.11.1, the same as OBS.app).
- The template's install step created `.pkg`, `package/`, and `temp/` inside the install prefix, which is the live plugins directory. It was removed. Other template changes are listed in `cmake/NOTICE`.
- `nlohmann/json` 3.12.0 comes through FetchContent, pinned by SHA256. The first test configure needs network access.
- Config URLs reject userinfo, query, and fragment, so a key cannot land in the plain JSON through the URL.

Findings for T4 to T6:

- `obs_output_set_reconnect_settings(output, retry_count, retry_sec)` is the only libobs reconnect setter. The 15-minute cap and the factor of about 1.5 are hard-coded (`obs-output.c`), so `ReconnectPolicy::max_delay_seconds` below 900 is not enforced by libobs. T4 must enforce it or the setting must be dropped.
- `group_by_encoder` groups video encoders only. Audio encoders are per track, so T4 has to share them separately using `Destination::audio_track`.
- `obs_frontend_add_canvas`, `obs_canvas_reset_video`, and `obs_canvas_get_video` are exported by the OBS.app binaries; declarations are in the 33.0.1 headers. They have not been called yet.

## Verification evidence

- T1 RED: `cmake --build build_tests` failed on the missing `destinations/runtime_requirements.hpp`. GREEN after implementation.
- T2 RED: `cmake --build build_tests` failed on the missing `destinations/reconnect_policy.hpp`. These are compile-level failures, not assertion failures. GREEN after implementation.
- T3 RED: `cmake --build build_tests` failed on the missing `destinations/destination_removal.hpp` and `persistence/file_config_repository.hpp`. GREEN after implementation.
- `cmake -S tests -B <fresh dir>`, build, and `ctest --output-on-failure`: 12 of 12 passed, with no compiler warnings.
- Keychain test ran against the real login Keychain under `com.alejox.obs-multi-stream.tests`, with no prompt and no skip in this session, 4 runs. `security find-generic-password` finds no residue for the test or the production service. Behavior from a locked Keychain or an SSH session was not exercised.
- `cmake --preset macos` and `cmake --build --preset macos`: BUILD SUCCEEDED. The binary is arm64, `minos 13.0`, ad-hoc signed, links libobs 33.0.0, obs-frontend-api 33.0.0, Qt 6.11.1, and Security.
- `cmake --install build_macos --config RelWithDebInfo`: installed to the plugins directory, signature verifies, and the other three plugins are unchanged by modification time.
- Not verified: the plugin has not run inside OBS. `pgrep -x OBS` found a running OBS for the whole session, so it was neither launched nor quit. The installed plugin will load on the next OBS launch.

## Next step

Prove the T1 load: with OBS closed by the user, launch it, read the newest log for `[obs-multi-stream] loaded version`, quit it cleanly, and check off T1. Then T4.
