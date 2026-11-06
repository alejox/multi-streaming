# Feature: windows-support

Locator: `odd/tasks/windows-support.md` | Engram topic: `odd/windows-support/tasks`

## Objective

Build and ship obs-multi-stream for Windows, in addition to the macOS arm64 build.

## Problem and why

The plugin only runs on macOS arm64 today. Two pieces depend on macOS: the vertical
preview's display uses OpenGL/Cocoa (`#if __APPLE__` returns on other platforms), and the
stream-key store uses the Keychain (non-Apple falls back to `UnavailableSecretStore`). The
build also lacks Windows: `buildspec.json` has only macOS hashes and `cmake/windows/` was
removed from the template. The user asked for a Windows build.

## Specs

- S1 (verbatim): "puedes sacarme la version de windows y mac"
- S2 (verbatim, chosen scope): "Encargar Windows completo"

## Scope

In scope:

- Vertical preview display on Windows via `gs_init_data.window.hwnd` (D3D11/D3D12).
- Stream keys on Windows via the Credential Manager (`CredWrite`/`CredRead`/`CredDelete`).
- Windows build: `buildspec.json` hashes, `cmake/windows/`, CMake presets.

Out of scope:

- Linux (same gaps as Windows, not requested).
- Windows installer/.msi packaging polish (a zip of the plugin is enough to start).

## Tasks

- [ ] T1 Preview display on Windows: `VerticalPreview::create_display()` sets
      `init.window.hwnd` under `_WIN32`. Route: inline (one bounded change).
- [ ] T2 Credential Manager store: `windows_credential_store.cpp/hpp` and wiring in
      `platform_secret_store.cpp` + CMake. Route: inline.
- [ ] T3 Windows build: `buildspec.json` Windows hashes, `cmake/windows/` helpers, CMake
      presets. Route: inline.
- [ ] T4 Compile and verify on Windows. Blocked on a Windows runner (no git remote, no
      mingw). Route: GitHub Actions once a remote exists, or the user compiles on Windows.

## Log

- L1: "puedes sacarme la version de windows y mac"
- L2: "Encargar Windows completo" (scope choice after the macOS zip was produced).

## Next step

T1 and T2 are pure code and can be written and kept compiling on macOS behind `_WIN32`
guards. T4 needs a Windows environment; the plan is to leave the code ready and set up
GitHub Actions when the user creates a remote, or hand the user build instructions.
