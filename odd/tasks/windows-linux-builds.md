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

- [ ] T1 Linux secret store via libsecret, wired into `platform_secret_store` and CMake.
      Route: delegated (2+ non-trivial files).
- [ ] T2 Linux preview display in `VerticalPreview::create_display()`. Route: delegated.
- [ ] T3 Linux build: `cmake/linux/`, preset, dependency handling. Route: delegated.
- [ ] T4 GitHub Actions workflow for Windows and Linux with zip/tar artifacts. Route: delegated.
- [ ] T5 Run CI, fix compile or test failures, download both packages. Route: parent operates
      CI; fixes delegated.

## Acceptance criteria

- Both CI jobs compile the plugin and pass the tests that run on that platform.
- Each job uploads a package containing the plugin binary and its data directory.
- Both packages are downloaded and their contents inspected.

## Log

- L1: "no te preocupes, dejalo asi, lo importante, saca la version de windows y linux"

## Next step

T1 through T4 through one bounded writer, then push and run CI.
