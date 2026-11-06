# Feature: windows-linux-builds

Locator: `odd/tasks/windows-linux-builds.md` | Engram topic: `odd/windows-linux-builds/tasks`

## Objective

Produce downloadable Windows x64 and Linux x86_64 packages of obs-multi-stream, compiled and
tested on GitHub Actions.

## Problem and why

Only a macOS arm64 zip exists. Windows code landed (`odd/tasks/windows-support.md`) but was
never compiled because no Windows runner was available. Linux has no CMake preset, no build
helpers, no preview window wiring and no secret store. The repository now has a remote
(`alejox/multi-streaming`), so CI can compile what this Mac cannot.

## Specs

- S1 (verbatim): "lo importante, saca la version de windows y linux"

## Scope

In scope:

- Linux stream-key storage through libsecret.
- Linux vertical preview display.
- Linux build configuration and preset.
- GitHub Actions workflow that builds, tests and packages Windows and Linux.
- Fixing whatever the first real compile on each platform reveals.

Out of scope:

- Publishing a GitHub Release (a human delivery decision).
- Installers, code signing, macOS in CI, and non-x86_64 Linux.
- Runtime verification inside OBS on Windows or Linux: no such machine is available here, so
  packages are verified as compiled and unit-tested only.

## Constraints

- Conventional Commits with no AI attribution (user rule).
- Delivery strategy: `single-pr`; forecast about 400 authored changed lines.
- RED cannot be observed locally for Linux or Windows code on this Mac. CI is the runner.

## Tasks

Written but not observed on the target stays unchecked until it is.

- [x] T1 Linux secret store via libsecret, wired into `platform_secret_store` and CMake.
      Route: delegated (2+ non-trivial files). Commit `f1b1be7`. CI run 42256716693: compiled
      and passed, including the Secret Service test.
- [ ] T2 Linux preview display in `VerticalPreview::create_display()`. Route: delegated.
      Commit `1877b1e`. Compiled in CI run 42256716693. The preview on X11 and Wayland has not
      been observed at runtime.
- [x] T3 Linux build: `cmake/linux/`, preset, dependency handling. Route: delegated. Commit
      `71e90ad`. CI run 42256716693 built against OBS 32.2.0 and packaged `bin/64bit/obs-multi-stream.so`,
      `data/` and `INSTALL.txt` (46 tar entries, inspected by the parent).
- [ ] T4 GitHub Actions workflow for Windows and Linux with zip/tar artifacts. Route:
      delegated. Commit `3ce0002`. The Linux job passed; the Windows job failed at Configure in
      run 42256716693 and its fixes have not been through CI.
- [ ] T5 Run CI, fix compile or test failures, download both packages. Route: parent operates
      CI; fixes delegated. Windows fixes: commits `6b84999`, `973ddb0`, `1631940`, none of them
      compiled on Windows yet. The Linux package is downloaded and inspected.

## Verification evidence

- macOS after the Windows fixes: `cmake --build --preset macos` succeeded and 27 of 27 core
  tests passed. These prove only that macOS did not regress.
- The Windows save path passed a syntax check against a stub of `windows.h`. No MSVC has seen it.
- Both Windows dependency zips were downloaded: their SHA-256 values equal `buildspec.json`,
  `file(ARCHIVE_EXTRACT)` handles them, and they extract with `share/obs-deps/VERSION` and
  `lib/cmake/Qt6/Qt6Config.cmake` at the root that the dependency check expects.

## Findings

- The PPA has no OBS 33 for Ubuntu 24.04: noble has 32.2.0, resolute (26.04) has 33.0.0.
  OBS 33.0.1 itself needs FFmpeg 8, so it cannot be built on 24.04 from distro packages.
- Linux builds on 24.04 against 32.2.0. Every OBS function the plugin calls from `obs.h` and
  `obs-frontend-api.h` has the same declaration in both versions. OBS rejects only plugins
  built against a newer libobs, so this binary should load on 32.2 and 33. Runner choice is
  still undecided.
- Run 42256716693 failed on Windows because `cmake/windows/buildspec.cmake` asked for `.tar.xz`;
  obs-deps publishes `.zip` for Windows.
- Visual Studio 2022 stays. OBS 33.0.1's preset and CI use Visual Studio 2026, but its CMake
  only requires Windows SDK 10.0.20348, and obs-deps 2026-10-23 was built with MSVC 19.44
  (`QT_MSVC_*` in `qconfig.pri`, `@comp.id` build 35229 in the static libraries), the toolset
  of Visual Studio 2022 17.14 on `windows-2022`. If linking fails over toolset versions, moving
  to `windows-2025-vs2026` and `Visual Studio 18 2026` is the fallback.
- The nested OBS configure downloads its own dependencies on Windows, including the x86 zip for
  its Win32 sub-configure, so CI time and network use are higher than the outer build suggests.
- MSVC warning level stays at its default, with warnings as errors from `compiler_common.cmake`.
  A compile failure from a warning is a T5 fix, not a reason to lower the bar blindly.

## Acceptance criteria

- Both CI jobs compile the plugin and pass the tests that run on that platform.
- Each job uploads a package containing the plugin binary and its data directory.
- Both packages are downloaded and their contents inspected.

## Log

- L1: "no te preocupes, dejalo asi, lo importante, saca la version de windows y linux"

## Next step

Push and run CI, then fix what the first Windows compile reveals (T5).
