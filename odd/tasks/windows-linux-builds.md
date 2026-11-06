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

Written but not compiled on the target stays unchecked until CI proves it.

- [ ] T1 Linux secret store via libsecret, wired into `platform_secret_store` and CMake.
      Route: delegated (2+ non-trivial files). Commit `f1b1be7`. Only a syntax check against
      a stub of libsecret was observed; CI must compile it and run both tests.
- [ ] T2 Linux preview display in `VerticalPreview::create_display()`. Route: delegated.
      Commit `1877b1e`. Not compiled for Linux. Follows `OBSQTDisplay.cpp` in OBS 33.0.1.
- [ ] T3 Linux build: `cmake/linux/`, preset, dependency handling. Route: delegated. Commit
      `71e90ad`. Install layout observed with a throwaway project on macOS; not run on Linux.
- [ ] T4 GitHub Actions workflow for Windows and Linux with zip/tar artifacts. Route:
      delegated. Commit `3ce0002`. YAML parses; never run. `.github` is ignored by the root
      `/*` rule, so the file was added with `git add -f`.
- [ ] T5 Run CI, fix compile or test failures, download both packages. Route: parent operates
      CI; fixes delegated. Known failures to fix first are listed below.

## Verification evidence

On macOS after these changes: `cmake --build --preset macos` succeeded and 27 of 27 core
tests passed.

## Findings

- The PPA has no OBS 33 for Ubuntu 24.04: noble has 32.2.0, resolute (26.04) has 33.0.0.
  OBS 33.0.1 itself needs FFmpeg 8, so it cannot be built on 24.04 from distro packages.
- Linux builds on 24.04 against 32.2.0. Every OBS function the plugin calls from `obs.h` and
  `obs-frontend-api.h` has the same declaration in both versions. OBS rejects only plugins
  built against a newer libobs, so this binary should load on 32.2 and 33.
- Blockers outside the allowed edit surface, certain to fail the Windows job:
  - `cmake/windows/helpers.cmake`: `install(TARGETS ... RUNTIME DESTINATION .)` on a MODULE
    target fails configure with "given no LIBRARY DESTINATION" (reproduced on macOS).
  - `src/persistence/file_config_repository.cpp` and
    `tests/persistence/file_config_repository_test.cpp` use POSIX (`unistd.h`, `fsync`,
    `mkstemp`, `mkdtemp`).

## Acceptance criteria

- Both CI jobs compile the plugin and pass the tests that run on that platform.
- Each job uploads a package containing the plugin binary and its data directory.
- Both packages are downloaded and their contents inspected.

## Log

- L1: "no te preocupes, dejalo asi, lo importante, saca la version de windows y linux"

## Next step

Fix the Windows blockers above, then push and run CI (T5).
