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

- [ ] T1 Scaffold: buildable plugin with pinned OBS 33.0.1, core test target, and proof it loads in OBS. Route: delegated (preparation trigger, 2+ files).
- [ ] T2 Destinations core: model, validation, config codec, reconnect policy, stats math, session state (test-first). Route: delegated (same writer).
- [ ] T3 Secrets and persistence: `SecretStore` port, Keychain adapter, config file repository with no keys in JSON (test-first). Route: delegated (same writer).
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

Branch `feat/native-multistream`, root commit `b633496`. Research complete.

## Verification evidence

None yet.

## Next step

T1.
