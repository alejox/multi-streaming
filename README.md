# OBS Multi Stream

Stream one OBS session to many RTMP/RTMPS destinations at once, each fed by the main mix or a dedicated vertical canvas. Stream keys live in the macOS Keychain, not in config files.

> **Status: pre-release.** Streaming is proven against local RTMP sinks on macOS arm64 with OBS 33.0.1. The dock and vertical canvas UI have not been checked by hand yet. See [Verification status](#verification-status).

## Quick path

Requires macOS 13+, OBS 33.0.1, Xcode 26.5+, and CMake.

1. Build and install, with OBS closed:

   ```bash
   cmake --preset macos
   cmake --build --preset macos
   cmake --install build_macos --config RelWithDebInfo
   ```

2. Open OBS and confirm that its log contains `[obs-multi-stream] loaded version`.
3. Open **Docks > Multi Stream** and add a destination.
4. Open **Docks > Vertical Canvas** if a destination uses the vertical source.

> **First load changes your scene collection.** The plugin adds a canvas named `obs-multi-stream-vertical` to it, and removing the plugin does not remove that canvas. See [Uninstall](#uninstall).

## Features

| Area | Behavior |
|------|----------|
| Destinations | RTMP/RTMPS URL plus key, per-row start/stop, Start all, status, bitrate, dropped frames |
| Video source | `main` mix or `vertical` canvas, per destination |
| Encoders | Shared when settings match; dedicated otherwise |
| Main stream sync | Optional start/stop with the OBS stream. Synced destinations use dedicated encoders, because OBS signals the start before the main encoders are active |
| Reconnect | Per-destination retry count and base delay. libobs fixes the growth factor and the 15-minute cap |
| Failure isolation | A failing destination does not stop the others or the main stream |
| Vertical canvas | Default 1080x1920, scenes, preview, and source composition with Fit and Fill |

## Behavior to know

- **Destinations are global**, not per OBS profile.
- **`enabled` governs Start all and sync only.** A row start still works.
- **URLs with userinfo, a query, or a fragment are rejected**, so a key cannot reach the config file. If a server needs a query string, put it in the key field.
- **A config with a wrong type or out-of-range value counts as corrupt.** It is moved aside untouched as `destinations.json.corrupt-N` and the dock reports it.
- **Dedicated audio is AAC at a fixed 160 kbps.**
- **Audio always comes from the main mix.** A source added only to the vertical canvas is silent on every destination. This keeps vertical-only audio out of the main stream.
- **Vertical resolution cannot change** while a vertical destination is live.

## Known limits

- A peer that never answers the RTMP handshake delays OBS exit by about 22 s.
- The UI waits up to 5 s for stopped outputs at a profile change, a collection change, and exit.
- Exit during a blocked Keychain read can report leaks.
- Validation messages are English-only, and no UI translation exists beyond `en-US`.
- The Canvas API is officially unstable in OBS 33.0.1. The build is pinned to that version, and other OBS versions are untested.
- Only macOS arm64 is supported. On other platforms the secret store fails loudly rather than falling back to plain text.

## Uninstall

Removing the plugin leaves data behind. OBS still loads cleanly and the main stream still works with this residue present.

| Residue | Location | Cleanup |
|---------|----------|---------|
| Plugin | `~/Library/Application Support/obs-studio/plugins/obs-multi-stream.plugin` | Delete it |
| Config | `~/Library/Application Support/obs-studio/plugin_config/obs-multi-stream/` | Delete it |
| Canvas and its scenes | Scene collection JSON: the `canvases[]` entry `obs-multi-stream-vertical` and the `sources` entries with its `canvas_uuid` | Back up the file first, then with OBS closed remove those entries |
| Stream keys | Keychain service `com.alejox.obs-multi-stream` | Delete the items in Keychain Access |

The manual collection cleanup has been tested once.

## Architecture

The core has no OBS or Qt headers, so its rules are tested without running OBS.

| Path | Role |
|------|------|
| `src/destinations/` | Core: model, validation, codec, ports, stats, session state, encoder plan |
| `src/persistence/` | Atomic config file repository |
| `src/secrets/` | Keychain adapter and test stores |
| `src/streaming/` | libobs outputs, services, encoders, main-stream sync |
| `src/vertical_canvas/` | Canvas lifecycle, scenes, composition, render |
| `src/dock/` | Qt docks and editor |
| `src/plugin_main.cpp` | Composition root |
| `tests/` | Core tests through CTest |

## Development

Run the core tests:

```bash
cmake -S tests -B build_tests
cmake --build build_tests
ctest --test-dir build_tests --output-on-failure
```

Runtime tests use the `MULTI_STREAM_E2E_SECRETS` CMake option in a separate build directory. It reads fake keys from a file and adds a test command hook. It is off by default, the code is excluded at compile time, and installing such a build fails on purpose.

## Verification status

| Check | Result |
|-------|--------|
| Core tests | 22 of 22 pass |
| Plugin loads and unloads in OBS 33.0.1 | Pass |
| Main and vertical streaming together | Pass: 640x360 and 1080x1920 on local sinks |
| Retries exhausted, closed port, exit while live | Pass, repeated runs |
| Slow or denied key read | Pass, using the fake key store with a simulated delay and denial |
| Three VideoToolbox sessions at once | Pass; higher counts untested |
| Dock and vertical canvas UI | **Not checked by hand** |
| Metal renderer | **Not tested**; all runs used OpenGL |
| Real Keychain prompt during start | **Not tested** |
| Real streaming services | **Not tested**; local sinks only |

## License

GPL-2.0-or-later. See [LICENSE](LICENSE). Build files in `cmake/` derive from [obs-plugintemplate](https://github.com/obsproject/obs-plugintemplate); see `cmake/NOTICE`. The design learned from [Aitum Vertical](https://github.com/Aitum/obs-vertical-canvas) and [Multiple RTMP Outputs](https://github.com/sorayuki/obs-multi-rtmp). This project is not affiliated with either.
