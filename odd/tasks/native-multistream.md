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

Unverified going in: Canvas API behavior from this plugin on OBS 33 arm64; Qt licensing terms. Three VideoToolbox sessions ran concurrently in the verifier's runs; the upper limit is unknown.

Isolated-instance technique, which leaves the user's running OBS alone:

- `CFFIXED_USER_HOME="$H" /Applications/OBS.app/Contents/MacOS/OBS --multi --disable-updater`, with `HOME` left unset. Plugins and config come from `$H/Library/Application Support/obs-studio`.
- In `$H`'s `global.ini`, `MacOSPermissionsDialogLastShown=1` and the first-run keys skip blocking dialogs. In `user.ini`, `ConfirmOnExit=false` lets SIGINT close OBS while streaming.
- A `MULTI_STREAM_E2E_SECRETS` build, in a build directory of its own and copied into `$H`, reads fake keys from `plugin_config/obs-multi-stream/e2e-secrets.txt` and takes commands from `e2e-commands.txt`. So no run touches the Keychain or needs UI automation.
- Sinks: `ffmpeg -listen 1 -i rtmp://127.0.0.1:<port>/live/<key> -c copy <file>.flv`. `sample <own pid>` shows whether the UI thread is blocked.

## Tasks

- [x] T1 Scaffold: buildable plugin with pinned OBS 33.0.1, core test target, and proof it loads in OBS. Route: delegated (preparation trigger, 2+ files). Commit `4773b63`. Load proof: the real OBS log at 21:31:53 showed `[obs-multi-stream] loaded version 0.1.0`, `loaded 0 destinations`, and `unloaded`, with leaks 0.
- [x] T2 Destinations core: model, validation, config codec, reconnect policy, stats math, session state (test-first). Route: delegated (same writer). Commit `56b8ddc`.
- [x] T3 Secrets and persistence: `SecretStore` port, Keychain adapter, config file repository with no keys in JSON (test-first). Route: delegated (same writer). Commit `669e6ed`.
- [x] T4 Streaming engine: per-destination output/service/encoders from the main mix, encoder sharing, reconnect, stats, main-stream sync. Route: delegated. Commit `7c0e412`; runtime defect D1 fixed in the commit recorded under D1.
- [ ] T5 Destinations dock: list, editor, status, stats, thread-safe UI marshaling. Route: delegated. Status: runs inside OBS and the engine behind it is proven, but the dock has no UI proof. Commit `6d1461f`.
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
- Plugin: template macOS preset build. The build fails if the binary contains the end-to-end secret store.
- Runtime: the isolated-instance technique under Reference evidence, streaming only to 127.0.0.1. A real install to `~/Library/Application Support/obs-studio/plugins` is the user's step.
- Keychain writes during automated checks use a test-only service name and clean up after themselves.

## Delivery

- Strategy: `exception-ok`. This is a greenfield repository without a remote, so there is no reviewer or base to slice against yet. Forecast: about 3,000 authored lines. Revisit with `chained-pr` if a remote and PR flow are added.
- Push, PR, and merge remain the user's decisions.
- RDD: off (global), so no native review. Verification tier follows `gentle-ai review assess` over each writer diff.

## Progress

Branch `feat/native-multistream`. T1 to T4 are complete. T5 runs inside OBS without UI proof. D1 is fixed in `9f49a38`. D2 is fixed in the commit that records this line.

Build decisions:

- arm64 only: OBS 33.0.1 itself builds single-architecture macOS, and the installed OBS.app is arm64. Minimum macOS is 13.0, matching `LSMinimumSystemVersion` and the OBS preset. Building needs SDK and Xcode 26.5 or later (OBS check).
- OBS 33.0.1 declares its dependencies in `CMakePresets.json`, not `buildspec.json`: prebuilt and Qt6 `2026-10-23` (Qt 6.11.1, the same as OBS.app).
- The template's install step created `.pkg`, `package/`, and `temp/` inside the install prefix, which is the live plugins directory. It was removed. Other template changes are listed in `cmake/NOTICE`.
- `nlohmann/json` 3.12.0 comes through FetchContent, pinned by SHA256. The first test configure needs network access.
- Config URLs reject userinfo, query, and fragment, so a key cannot land in the plain JSON through the URL.

Findings for T4 to T6:

- `obs_output_set_reconnect_settings(output, retry_count, retry_sec)` is the only libobs reconnect setter. The 15-minute cap and the factor of about 1.5 are hard-coded (`obs-output.c`), so `ReconnectPolicy::max_delay_seconds` was removed in T4. Older configs still load; the field disappears on the next save.
- `group_by_encoder` groups video encoders only. Audio encoders are per track, so T4 has to share them separately using `Destination::audio_track`.
- `obs_frontend_add_canvas`, `obs_canvas_reset_video`, and `obs_canvas_get_video` are exported by the OBS.app binaries; declarations are in the 33.0.1 headers. They have not been called yet.

Engine decisions from T4 and T5, verified against the 33.0.1 sources:

- `obs_output_stop` does nothing while an RTMP output is connecting, because it is not `active` yet. The engine keeps the session in `Stopping` and issues the stop on the `start` signal. A force stop would work at once, but it joins the connect thread, which would freeze the UI.
- `obs_video_encoder_create` with an unknown id returns a placeholder instead of null, so ids are checked with `obs_get_encoder_codec` first.
- `obs_encoder_get_settings` is passed by reference into new encoders, so a Share clone copies main's settings.
- Outputs stop on `SCRIPTING_SHUTDOWN`. The event callback is removed on `EXIT`, because the frontend API is destroyed right after it and well before `obs_module_unload`.
- `PROFILE_CHANGING` force-stops and releases everything synchronously, because the new profile resets video. Nothing restarts automatically. Destinations are global, not per profile.
- A main stream that fails to start skips `STOPPING`, so sync destinations also stop on `STOPPED`.
- The `STARTING` event fires before main's output is active, so sync destinations never reuse main's encoders: they get a dedicated encoder cloned from main's settings, which costs one more VideoToolbox session. Destinations started by hand while main is live do reuse them.
- `enabled` only decides membership in Start all and sync. An explicit row start still works.
- A missing stream key fails the start with a visible reason instead of connecting with an empty key.
- Dedicated audio uses AAC at a fixed 160 kbps, since the model has no audio bitrate field. Core validation messages are English strings from the core, not `en-US.ini` entries.

D1, UI hang and crash when reconnect retries run out (cause proven, fixed):

- The sample's `obs_output_destroy` calls `obs-outputs + 0x38dc0`, which `otool` shows follows `bl _pthread_join` at `0x38dbc`, loading `[x19+0x88]`, guarded by `ldrb [x19+0x80] == 1`. That is `rtmp-stream.c:111-112`, joining `connect_thread`. `obs-output.c` joins nothing on this path.
- The connect thread fires the final `stop` signal at `rtmp-stream.c:1529`, then detaches itself at `:1533-1534`. Our queued release reached `rtmp_stream_destroy` first. On macOS a waiting join is never woken by that detach: `pthread_detach` clears `tl_join_ctx` without setting the exit gate, so `_pthread_terminate` wakes nobody (libpthread `pthread.c:1641`, `pthread_cancelable.c`). SIGTERM's EINTR then re-reads the gate in the freed thread struct: `pthread_join() state corruption`.
- A standalone C program that detaches during a join blocks at the same `_pthread_join + 620` frame.
- The same join is reachable from `rtmp_stream_stop` (`:210-211`), so any stop or destroy while a connect thread lives can hang, including `obs_output_force_stop` while reconnecting. libobs does not expose that thread's state.
- Fix: `ConnectGate` tracks when the thread can be alive from output signals, and the known minimum delay before the next attempt. A stop waits for a clear gate. Destruction never runs on the UI thread: `DeferredWork` runs each retired output on its own thread after the gate clears, 1 s after the final signal.
- Accepted limits: at a profile change or exit the UI waits up to 5 s for retired outputs. An output still connecting after that is left running and its leak reported. A peer that accepts TCP and never answers makes the OBS process itself wait for the 30 s RTMP timeout at exit (observed: exit 0 after 22 s, 2 leaks).

D2, Keychain read blocking the UI thread during `STREAMING_STARTING` (fixed):

- Main's connection was delayed 17 to 48 s behind prompts. Each key read now runs on its own thread, and its start finishes on the UI thread. `StartTickets` voids a read when a stop or teardown arrives first. A prompt or denial affects only its destination.
- The encoder plan is still made once per batch when the start is requested, so a slow read cannot change what the batch shares.
- A read still blocked at exit keeps its main-encoder references, so OBS reports 2 leaks in that case (observed).

## Verification evidence

- T1 RED: `cmake --build build_tests` failed on the missing `destinations/runtime_requirements.hpp`. GREEN after implementation.
- T2 RED: `cmake --build build_tests` failed on the missing `destinations/reconnect_policy.hpp`. These are compile-level failures, not assertion failures. GREEN after implementation.
- T3 RED: `cmake --build build_tests` failed on the missing `destinations/destination_removal.hpp` and `persistence/file_config_repository.hpp`. GREEN after implementation.
- `cmake -S tests -B <fresh dir>`, build, and `ctest --output-on-failure`: 12 of 12 passed, with no compiler warnings.
- Keychain test ran against the real login Keychain under `com.alejox.obs-multi-stream.tests`, with no prompt and no skip in this session, 4 runs. `security find-generic-password` finds no residue for the test or the production service. Behavior from a locked Keychain or an SSH session was not exercised.
- `cmake --preset macos` and `cmake --build --preset macos`: BUILD SUCCEEDED. The binary is arm64, `minos 13.0`, ad-hoc signed, links libobs 33.0.0, obs-frontend-api 33.0.0, Qt 6.11.1, and Security.
- `cmake --install build_macos --config RelWithDebInfo`: installed to the plugins directory, signature verifies, and the other three plugins are unchanged by modification time.
- Not verified: the plugin has not run inside OBS. `pgrep -x OBS` found a running OBS for the whole session, so it was neither launched nor quit. The installed plugin will load on the next OBS launch.

- T4 RED, assertion level: 2 of 12 `config_codec_test` cases failed before removing `max_delay_seconds`; with stubs, `encoder_plan_test` failed 13 of 14, `encoder_leases_test` 3 of 4, and `session_selection_test` 5 of 5. GREEN after implementation.
- T5 RED, assertion level: with a stub, `destination_editing_test` failed 7 of 8. GREEN after implementation.
- `cmake -S tests -B <fresh dir>`, build, and `ctest --output-on-failure`: 16 of 16 passed, with no compiler warnings.
- `cmake --preset macos` and `cmake --build --preset macos`: BUILD SUCCEEDED, with warnings treated as errors.
- `rg` over `src`: no log call mentions a key, secret, URL, or server. The only write of the key outside `SecretStore` is `obs_data_set_string(service_settings, "key", ...)`. `config_codec.cpp` and `file_config_repository.cpp` contain no key field.
- Exception to test-first: `src/streaming` and `src/dock` have no runnable deterministic test, because they need a running OBS and Qt. Their decision logic was extracted into tested core code.
- Not verified: no engine or dock code has run inside OBS. OBS stayed running for this session, and nothing was installed.

- Isolated OBS runs, all with a `MULTI_STREAM_E2E_SECRETS` build, no Keychain, and streaming started while the UI was idle through the command hook. Each main-thread `sample` shows no `_pthread_join` or `obs_output_destroy`:
  - Retries out, 3 runs: sync destination with `max_retries: 2`, its sink killed while live. `failed` logged after 5.7 s, main and the other destination kept growing (0.8 to 1 MB in 4 s), exit 0 in about 0.2 s, `unloaded`, 0 leaks.
  - Closed port, 3 runs: `failed` logged, others kept growing, exit 0, 0 leaks.
  - Stop while reconnecting: `idle` 0.2 s after the stop, others kept growing, exit 0, 0 leaks.
  - Exit with a destination live: `idle` logged, exit 0 in 0.23 s, 0 leaks, sink's `ffprobe` shows `h264,640,360` and `aac,48000`.
  - Sync stop through `obs_frontend_streaming_stop`: `starting, live, stopping, idle`, exit 0, 0 leaks.
  - Peer that never answers the handshake, stop then exit, and exit alone: UI not blocked, others kept growing, exit 0 after about 22 s, `left running` logged, 2 leaks.
- `cmake --preset macos` and `cmake --build --preset macos`: BUILD SUCCEEDED, and the post-build check reports no end-to-end store in the binary.
- Verifier's isolated runs before D1 and D2: checks 1, 2, 3, 5, 6, 7, and 8 passed, with three VideoToolbox sessions concurrent. The same runs reported 1407 leaks as the plugin-free baseline at live-stream exit. Runs with SIGINT and the fix report 0.
- D2 runtime, key read delayed 9 s for one destination and denied after 0.5 s for another, started while the UI was idle: main connected in 0.15 s, the other destination was live in 0.16 s, the slow one in 9.08 s, the denied one failed alone in 0.51 s, and all three streams grew. A `sample` during the read shows the main thread in its event loop. Exit 0, 0 leaks.
- D2 stop during the read: `idle` at once, no connect afterwards, exit 0, 0 leaks. Exit during a 60 s read: exit 0 in 0.23 s, 2 leaks.
- D2 RED, assertion level: with a stub, `start_tickets_test` failed 6 of 6. GREEN after implementation.
- D1 RED, assertion level: with stubs, `connect_gate_test` failed 8 of 11 and `deferred_work_test` 6 of 6. GREEN after implementation.

## Next step

T6, then T7. T5 needs a human UI check.
