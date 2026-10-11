# Windows Candidate Rendering

## Decision and Scope

Keep the nonactivating Win32 popup, replace GDI font measurement/drawing with
DirectWrite layouts and a Direct2D HWND render target. Language-bar icons and
document composition are unchanged. The TSF target links Windows `d2d1` and
`dwrite`; no Core, submodule, IPC or registration metadata changes are needed.
The window and graphics resources stay on the TSF owning thread. Initialization
is lazy, outside `DllMain`. Snapshots still come from Core; rendering never
commits text. There is no render thread or continuous animation loop.

## Typography and Layout

Labels, bodies and comments have separate layouts. The body uses 16 DIP Segoe
UI, secondary text 13 DIP, with DirectWrite's system font fallback. Complete
UTF-16 strings include surrogate pairs, ZWJ sequences, skin-tone modifiers and
combining marks. Drawing enables `D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT`.
Explicit VS15 text-presentation sequences prefer Segoe UI Symbol for the scalar
and selector. The tested VS15 heart is monochrome alongside a VS16 color heart;
unsupported glyphs and sequences still depend on installed fonts and Windows.

Measurement and drawing use the same layouts. Row height includes text height,
vertical glyph overhang and padding, with a 32 DIP minimum. Columns align labels
and comments; long text uses DirectWrite ellipsis. Comments keep natural width
when possible, but receive at most 35% of remaining space when the row does not
fit. Preferred width is bounded to 180-640 DIP and the monitor work area.
System colors supply text, background, selection and border. Theme/color changes
invalidate the window. This is not an independent Windows 11 dark theme.

## DPI and Lifecycle

The edit session queries `GetTextExt` under the owner's DPI awareness, then
restores the thread context. `show` accepts owner-space logical screen coordinates
and converts both corners with `LogicalToPhysicalPointForPerMonitorDPI`.
A null owner assumes physical coordinates. A clipped composition falls back to
the collapsed selection range; clipped/empty geometry, hidden/minimized owners,
invalid UTF-8 and empty candidates hide the popup.

`TS_E_NOLAYOUT` after a preedit write is temporary: keep the visible popup at its
last physical anchor and update it with the applied snapshot for up to 250 ms.
This anchor is never reused across context, view or owner changes, commit or
cancellation. If layout stays unavailable, hide without discarding the pending
snapshot so a later successful query can restore it. A merged message retries
once after the write; the existing 100 ms timer also retries pending geometry.

The focused context advises `ITfTextLayoutSink`. Layout notifications post a
merged message which requests a read-only edit session (synchronous with async
fallback); they do not write document text or make IPC requests. The session
holds its original context and generation and uses the last applied candidate
snapshot, not input still waiting in the edit queue. Unadvise on context change
and reject late sessions after cancellation, focus change or deactivation.
Hosts that reject this optional sink still use the existing timer/snapshots.

Creation and positioning temporarily use Per-Monitor V2, restoring the host
thread without changing process awareness. The popup's own DPI determines
DIP-to-pixel conversion, even for DPI-unaware owners. `WM_DPICHANGED` and display
changes recompute bounds; `WM_SIZE` updates target size. Positioning supports
negative coordinates, clamps to the anchor monitor's work area, and moves above
the anchor when space below is exhausted.

`WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW` and `MA_NOACTIVATE` preserve focus.
Owner destruction clears the HWND and device resources; a later snapshot can
recreate the popup. Device-target loss or target creation failure discards device
resources and schedules at most two repaint retries, after 16/32 ms, without
hiding. Persistent or other drawing errors hide until a later snapshot retries,
without resetting or replaying input. Hiding cancels repaint retries.

## Resource and Latency Policy

Reuse factories, font formats, trimming sign, target and brush. Unchanged
strings retain layouts, including selection-only updates. Resize the target
only when pixel dimensions change. A single BeginDraw/EndDraw pair renders
the frame, without explicit Flush per row. `D2D1_PRESENT_OPTIONS_IMMEDIATELY`
avoids waiting for display refresh on the application's TSF thread.
After positioning, `UpdateWindow` paints the invalidated surface immediately,
including after resizing; production updates do not wait for an idle `WM_PAINT`.
See Microsoft's [performance guide](https://learn.microsoft.com/en-us/windows/win32/direct2d/improving-direct2d-performance)
and [presentation options](https://learn.microsoft.com/en-us/windows/win32/api/d2d1/ne-d2d1-d2d1_present_options).

## Verification

The runs recorded below used the test set available at the time. The current
default TSF suite has thirteen tests, including the later `test_uninstall`;
see [the current test list](pinyin.md#build-and-test-tsf). This documentation
update does not rerun candidate previews or Pinyin probes.

`test_candidate` is a default CTest with no Core or registration requirement.
The production draw function runs through a software WIC target at 96/120/144/
192/288 DPI. Emoji colored pixels are compared with a monochrome control;
VS15 heart presentation, ellipsis and layout reuse are checked. Window tests
cover three owner awareness modes, thread-state restoration, focus, owner
lifetime, empty/invalid input, resource recreation and work-area bounds.
They also check immediate paint, same-anchor updates under all three awareness
modes, target-loss recovery and bounded repaint failure handling.

`test_candidate_tsf` uses a real TSF context with the shared in-memory text store
from `tsf_probe`, but no Core, IPC connection or input-method registration. It
checks repeated `TS_E_NOLAYOUT`, content updates at the retained anchor, merged
layout notifications and read-only repositioning, applied versus queued
snapshots, caret fallback, invisible geometry, timeout/recovery, commit, cancel,
hidden owners and late asynchronous position sessions after focus loss.
The 2026-10-11 AMD64 Release build and all 12 default CTests passed. An existing
Core was running, so the real Pinyin probes were not rerun and no services were
stopped. This does not establish visual behavior in registered applications.
The native 192 DPI preview and five offscreen images are in
`build/candidate-flicker-preview`. The native image passed colored-pixel and
selection-color assertions; warm show + paint measured P50 2.217 ms / P95
3.268 ms in this run. One preview attempt hit the foreground-window assertion
on the interactive desktop; the subsequent complete preview passed.

After building, generate PNGs and briefly show a native preview:

```powershell
./win32/build/candidate-d2d/tests/test_candidate.exe build/candidate-d2d-preview
```

The directory contains five synthetic-DPI images and a native HWND capture
using `PrintWindow(PW_RENDERFULLCONTENT)`, independent of occlusion. These use
synthetic snapshots, not live engine candidates or registered-TIP OS dispatch.
The 2026-10-11 AMD64 Release run passed twelve CTests and formatting checks.
The 192 DPI native window measured 360 x 432 pixels. One local run of 100 warm
selection updates measured show + UpdateWindow P50 2.086 ms, P95 3.323 ms.
These are call durations, not end-to-end input latency or a GDI comparison.

After the user stopped Core, the real engine/DLL integration also passed:

```powershell
./scripts/test-pinyin.ps1 -Prefix ./dist/pinyin -TsfBuild ./win32/build/candidate-d2d
```

The earlier 2026-10-10 run passed `ipc_probe`, `settings_probe`, `tsf_probe`
with the new DLL and nine CTests, covering real full/Xiaohe/Ziranma Chinese
commits, settings, TSF context writes, asynchronous edits, cancellation and
focus isolation. The 2026-10-11 candidate-layout change was validated by the
additional TSF-only test above; the real Core probes were not rerun because a
user Core was already running. No default DLL was replaced and no input-method
registration was performed.

## Remaining Validation

Real registered-TIP applications, physical mixed-DPI monitor transitions,
themes/high contrast and accessibility text scaling remain unverified. Font
coverage and newer color-font formats depend on the OS and installed fonts.
Physical scrolling/moving behavior still needs application testing despite the
layout-change subscription. Oversized pages are height-clamped
without scrolling. Candidate mouse interaction and TSF UIElement/accessibility
remain separate work.
