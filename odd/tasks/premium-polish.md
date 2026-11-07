# Premium polish: OBS best practices, preview fix, UX/UI

Branch: `feat/premium-polish` (from `feat/twitch-autologin` @ adec1b1)
Delivery strategy: `single-pr` (the user asked for autonomous end-to-end work; PR creation stays with the user).
Forecast: well above 400 authored changed lines across tasks.

## Objective

Make the plugin sale-ready: fix verified lifecycle/encoder bugs against OBS 33.0.1 behavior, fix the
floating vertical preview on macOS, and bring the UI to OBS-native, theme-aware, premium quality.

## Problem / why

Three read-only audits (architecture vs OBS 33.0.1 source, preview root cause vs Qt/OBS/Aitum source,
UX/UI vs OBS Yami theme and commercial tools) found verified bugs (encoder sharing never happens for
follow-main destinations; follow-main destinations stay live when main fails to start; vertical
outputs survive canvas removal; no `video_reset` handling), a Qt root cause for the floating preview
(native child NSView is not moved when an alien ancestor moves), and many UX gaps (hard-coded
colors, flat editor form, untranslated validation messages, unsafe destructive actions).

## Constraints

- OBS 33.0.1, Qt 6, C++17; macOS, Windows, Linux builds must keep compiling.
- Core (`src/destinations`) stays OBS/Qt-free and is covered by `tests/` (custom harness, ctest).
- UI strings go through `data/locale/en-US.ini` and `es-ES.ini` with key parity.
- No local `setStyleSheet` on widgets that use OBS theme `class` properties.
- RDD is off (global); checks are build + core tests + structural readback + manual OBS check by the user.

## Checks (per task)

- `cmake --build build_macos --config RelWithDebInfo` succeeds.
- `cmake -S tests -B build_tests && cmake --build build_tests && ctest --test-dir build_tests` passes.
- Locale key parity: `diff <(rg -o '^[^=]+' data/locale/en-US.ini | sort) <(rg -o '^[^=]+' data/locale/es-ES.ini | sort)`.

## Tasks

- [x] T1 Preview: push native child geometry (`windowHandle()->setGeometry`) on ancestor move/resize/screen change, then off-by-one resize; drop display recreation hacks. Route: inline (one file, cause understood).
- [ ] T2 Main-stream lifecycle: start follow-main destinations on `STREAMING_STARTED`; stop vertical outputs on `CANVAS_REMOVED`; handle core `video_reset` for the vertical canvas. Route: delegated writer (2+ files).
- [ ] T3 Encoder sharing: share main encoders only with follow-main destinations (lifetimes match); copy encoders honor main rescale. Route: delegated writer (core + tests).
- [ ] T4 Twitch OAuth robustness: free sockets, match redirect path, login timeout, store refresh token only on Save. Route: delegated writer.
- [ ] T5 Localize core/validation/persistence messages via codes + locale keys. Route: delegated writer.
- [ ] T6 Theme-aware styling: OBS `class` properties for status/notice/tool buttons, no hard-coded colors, native toolbar icons, empty state, notice banner. Route: delegated writer.
- [ ] T7 Editor dialog redesign: Service first, grouped sections, hidden custom-encoder rows, inline key eye toggle, Advanced collapsible, error styling + focus, presets/read-only URL, stream key links, clearer checkbox wording, audio track combo, localized units. Route: delegated writer.
- [ ] T8 Dock UX: delete confirm defaults to No, Stop all confirmation when live, enable states for Start/Stop all, live timer, compact stats, shorter flapping text, accessibility names, move up/down enable states. Route: delegated writer.
- [ ] T9 Vertical dock: libobs signals instead of 500 ms polling, Delete shortcut, resolution presets + live even-number validation, single-key audio note. Route: delegated writer.
- [ ] T10 Persistence: libobs `*_safe` writes with `.bak` recovery; vertical settings per scene collection. Route: delegated writer.
- [ ] T11 Secrets off the UI thread for editor/token/save/delete paths. Route: delegated writer.
- [ ] T12 Twitch HTTP via libcurl worker; remove global `addLibraryPath` and bundled TLS backend. Route: delegated writer.

## Acceptance criteria

- Moving OBS between monitors keeps the vertical preview inside its dock (user-verified).
- Follow-main destinations reuse OBS encoders when compatible and never stay live when main fails.
- All user-visible strings localized (en/es parity); UI readable on light and dark OBS themes.
- All builds and core tests pass after every task.

## Progress / evidence

(updated per task with commit ids and observed check results)

- T1: commit 355ac32. macOS build OK; installed + codesign OK. Root cause: Qt setGeometry_sys only moves a native child when its own geometry changes (qtbase qwidget.cpp L7314-7420). Pending: user check across monitors.
- Engram mirror `odd/premium-polish/tasks`: PENDING (auto-mode classifier blocked mem_save; resync when available).

## Next step

T2+T3 (delegated writer).
