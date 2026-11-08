# OBS Multi Stream

Stream one OBS session to many RTMP/RTMPS destinations at once, each fed by the main mix or a dedicated vertical canvas. Stream keys live in the system's secret store (macOS Keychain, Windows Credential Manager, or Linux Secret Service), not in config files.

> **Status: v0.3.0 released.** Windows, Linux and macOS packages are published on the [releases page](https://github.com/alejox/multi-streaming/releases). Streaming is proven against local RTMP sinks on macOS arm64 with OBS 33.0.1. The dock and vertical canvas UI have not been checked by hand yet, and neither the Windows nor the Linux package has been run inside OBS. See [Verification status](#verification-status).

## Quick path

This builds from source on macOS. For Windows and Linux, see [Packages](#packages).

Requires macOS 13+, OBS 33.0.1, Xcode 26.5+, and CMake.

1. Build and install, with OBS closed:

   ```bash
   cmake --preset macos
   cmake --build --preset macos
   cmake --install build_macos --config RelWithDebInfo
   ```

2. Open OBS and confirm that its log contains `[obs-multi-stream] loaded version`.
3. Open **Docks > Multi Stream** and add a destination.
4. Open **Docks > Vertical canvas** if a destination uses the vertical source.

> **First load changes your scene collection.** The plugin adds a canvas named `obs-multi-stream-vertical` to it, and removing the plugin does not remove that canvas. See [Uninstall](#uninstall).

## Packages

Download the latest packages from the [releases page](https://github.com/alejox/multi-streaming/releases). Each package also holds its install steps as `INSTALL.txt`.

| Platform | Built against | Install, with OBS closed |
|----------|---------------|--------------------------|
| macOS arm64 | OBS 33.0.1 | Copy the `obs-multi-stream.plugin` bundle into `~/Library/Application Support/obs-studio/plugins/` |
| Windows x64 | OBS 33.0.1 | Extract the `obs-multi-stream` folder into `C:\ProgramData\obs-studio\plugins\` |
| Linux x86_64 | The OBS Project PPA for Ubuntu 24.04, 32.2.0 when this was written | `mkdir -p ~/.config/obs-studio/plugins`, then `tar -xzf <package> -C ~/.config/obs-studio/plugins` |

- **The macOS package is ad-hoc signed and not notarized.** Gatekeeper may quarantine it after download. If macOS refuses to load it, clear the flag with `xattr -dr com.apple.quarantine obs-multi-stream.plugin`.
- **Windows needs OBS 33 or later.** OBS refuses a plugin built against a newer libobs than its own.
- **Linux needs glibc 2.39 and Qt 6.4 or later,** as on Ubuntu 24.04, plus libsecret. Without a running Secret Service such as GNOME Keyring or KWallet, stream keys cannot be saved.
- **The Linux package is not built against OBS 33.0.1,** because the PPA has no OBS 33 for Ubuntu 24.04. See [Known limits](#known-limits).

## Features

| Area | Behavior |
|------|----------|
| Destinations | RTMP/RTMPS URL plus key, per-row start/stop, Start all, status, bitrate, dropped frames |
| Twitch login | "Connect with Twitch" opens the browser for OAuth, fills the stream key, and remembers the session |
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
- **Updates install when OBS closes.** About 8 s after OBS starts, the plugin checks the latest GitHub release. If it is newer, the package for your platform is downloaded in the background and verified with SHA-256. The dock then shows that the update is ready. When OBS closes, a separate script waits for OBS to exit, moves the current version aside, installs the new one, and puts the old one back if anything fails. Turn this off with **Install updates automatically** in the dock settings menu.
- **Some installs only get a download link.** This happens when the plugin is not in a per-user `obs-studio/plugins/<name>` folder, or that folder cannot be written (for example a system-wide or Program Files install). It also happens when a version failed to install. The dock then shows a **Download** link to the release page.

## Known limits

- **Twitch login needs the redirect URIs registered.** The plugin tries `http://localhost:8090/` through `http://localhost:8094/` in order, so every one of those must be added as an OAuth Redirect URL in the Twitch app at dev.twitch.tv/console. The Client ID lives in `src/oauth/twitch_client_id.hpp`.
- A peer that never answers the RTMP handshake delays OBS exit by about 22 s.
- The UI waits up to 5 s for stopped outputs at a profile change, a collection change, and exit.
- Exit during a blocked Keychain read can report leaks.
- Validation messages are English-only, and no UI translation exists beyond `en-US`.
- The Canvas API is officially unstable in OBS 33.0.1. The build is pinned to that version, and other OBS versions are untested.
- Only macOS arm64 has been run inside OBS. Linux x86_64 and Windows x64 are compile-tested and unit-tested only.
- The Linux package is built against OBS 32.2.0, an untested version for an unstable API. The functions the plugin calls from `obs.h` and `obs-frontend-api.h` have the same declarations in both versions, which says nothing about their behavior.
- Where no secure store is available, saving a key fails loudly rather than falling back to plain text.

## Uninstall

Removing the plugin leaves data behind. OBS still loads cleanly and the main stream still works with this residue present.

| Residue | Location | Cleanup |
|---------|----------|---------|
| Plugin | macOS: `~/Library/Application Support/obs-studio/plugins/obs-multi-stream.plugin`<br>Windows: `C:\ProgramData\obs-studio\plugins\obs-multi-stream\`<br>Linux: `~/.config/obs-studio/plugins/obs-multi-stream/` | Delete it |
| Config | macOS: `~/Library/Application Support/obs-studio/plugin_config/obs-multi-stream/`<br>Windows: `%APPDATA%\obs-studio\plugin_config\obs-multi-stream\`<br>Linux: `~/.config/obs-studio/plugin_config/obs-multi-stream/` | Delete it |
| Canvas and its scenes | Scene collection JSON: the `canvases[]` entry `obs-multi-stream-vertical` and the `sources` entries with its `canvas_uuid` | Back up the file first, then with OBS closed remove those entries |
| Stream keys | macOS: Keychain service `com.alejox.obs-multi-stream`<br>Windows: Credential Manager targets starting `obs-multi-stream/`<br>Linux: Secret Service items with `service` = `com.alejox.obs-multi-stream` | Delete them in Keychain Access, Credential Manager, or Seahorse |

The manual collection cleanup has been tested once.

## Architecture

The core has no OBS or Qt headers, so its rules are tested without running OBS.

| Path | Role |
|------|------|
| `src/destinations/` | Core: model, validation, codec, ports, stats, session state, encoder plan |
| `src/persistence/` | Atomic config file repository |
| `src/secrets/` | Keychain, Credential Manager and Secret Service adapters, and test stores |
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

### Build on Linux

Linux builds against system packages rather than `buildspec.json`, so that the plugin links the libobs that OBS itself loads. On Ubuntu 24.04:

```bash
sudo add-apt-repository ppa:obsproject/obs-studio
sudo apt-get update
sudo apt-get install --no-install-recommends \
  libsecret-1-dev libsimde-dev ninja-build obs-studio pkg-config qt6-base-dev
cmake --preset linux-x86_64
cmake --build --preset linux-x86_64
cmake --install build_linux
```

The Secret Service test is skipped when no service is reachable. Set `MULTI_STREAM_REQUIRE_SECRET_SERVICE=1` to make that a failure, as CI does.

### Runtime tests

Runtime tests use the `MULTI_STREAM_E2E_SECRETS` CMake option in a separate build directory. It reads fake keys from a file and adds a test command hook. It is off by default, the code is excluded at compile time, and installing such a build fails on purpose.

## Verification status

| Check | Result |
|-------|--------|
| Core tests | 27 of 27 pass |
| Plugin loads and unloads in OBS 33.0.1 | Pass |
| Main and vertical streaming together | Pass: 640x360 and 1080x1920 on local sinks |
| Retries exhausted, closed port, exit while live | Pass, repeated runs |
| Slow or denied key read | Pass, using the fake key store with a simulated delay and denial |
| Three VideoToolbox sessions at once | Pass; higher counts untested |
| Dock and vertical canvas UI | **Not checked by hand** |
| Metal renderer | **Not tested**; all runs used OpenGL |
| Real Keychain prompt during start | **Not tested** |
| Real streaming services | **Not tested**; local sinks only |
| Linux in CI: compile, core tests, Secret Service test, package | Pass, against OBS 32.2.0 |
| Windows in CI: compile, core tests, package | Pass, against OBS 33.0.1 |
| macOS package: build, core tests, ad-hoc code signature | Pass, locally |
| Windows and Linux inside OBS, including previews on X11 and Wayland | **Not tested** |

## License

GPL-2.0-or-later. See [LICENSE](LICENSE). Build files in `cmake/` derive from [obs-plugintemplate](https://github.com/obsproject/obs-plugintemplate); see `cmake/NOTICE`. The design learned from [Aitum Vertical](https://github.com/Aitum/obs-vertical-canvas) and [Multiple RTMP Outputs](https://github.com/sorayuki/obs-multi-rtmp). This project is not affiliated with either.
