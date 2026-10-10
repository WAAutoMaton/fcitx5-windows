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
A null owner assumes physical coordinates. Clipped or unavailable TSF geometry,
hidden owners, invalid UTF-8 and empty candidates hide the popup.

Creation and positioning temporarily use Per-Monitor V2, restoring the host
thread without changing process awareness. The popup's own DPI determines
DIP-to-pixel conversion, even for DPI-unaware owners. `WM_DPICHANGED` and display
changes recompute bounds; `WM_SIZE` updates target size. Positioning supports
negative coordinates, clamps to the anchor monitor's work area, and moves above
the anchor when space below is exhausted.

`WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW` and `MA_NOACTIVATE` preserve focus.
Owner destruction clears the HWND and device resources; a later snapshot can
recreate the popup. Drawing failure discards target resources and hides the
popup until a later snapshot retries, without resetting or replaying input.

## Resource and Latency Policy

Reuse factories, font formats, trimming sign, target and brush. Unchanged
strings retain layouts, including selection-only updates. Resize the target
only when pixel dimensions change. A single BeginDraw/EndDraw pair renders
the frame, without explicit Flush per row. `D2D1_PRESENT_OPTIONS_IMMEDIATELY`
avoids waiting for display refresh on the application's TSF thread.
See Microsoft's [performance guide](https://learn.microsoft.com/en-us/windows/win32/direct2d/improving-direct2d-performance)
and [presentation options](https://learn.microsoft.com/en-us/windows/win32/api/d2d1/ne-d2d1-d2d1_present_options).

## Verification

`test_candidate` is a default CTest with no Core or registration requirement.
The production draw function runs through a software WIC target at 96/120/144/
192/288 DPI. Emoji colored pixels are compared with a monochrome control;
VS15 heart presentation, ellipsis and layout reuse are checked. Window tests
cover three owner awareness modes, thread-state restoration, focus, owner
lifetime, empty/invalid input, resource recreation and work-area bounds.

After building, generate PNGs and briefly show a native preview:

```powershell
./win32/build/candidate-d2d/tests/test_candidate.exe build/candidate-d2d-preview
```

The directory contains five synthetic-DPI images and a native HWND capture
using `PrintWindow(PW_RENDERFULLCONTENT)`, independent of occlusion. These use
synthetic snapshots, not live engine candidates or registered-TIP OS dispatch.
The 2026-10-10 AMD64 Release run passed nine CTests and formatting checks.
The 192 DPI native window measured 360 x 432 pixels. One local run of 100 warm
selection updates measured show + UpdateWindow P50 2.086 ms, P95 3.323 ms.
These are call durations, not end-to-end input latency or a GDI comparison.

After the user stopped Core, the real engine/DLL integration also passed:

```powershell
./scripts/test-pinyin.ps1 -Prefix ./dist/pinyin -TsfBuild ./win32/build/candidate-d2d
```

`ipc_probe`, `settings_probe`, `tsf_probe` with the new DLL, and all nine CTests
passed. Coverage includes real full/Xiaohe/Ziranma Chinese commits, settings,
TSF context writes, asynchronous edits, cancellation and focus isolation.
The script used an independent settings file and cleaned up its Core process;
Core/Settings were absent afterwards and Core logs were empty. The TSF probe's
key-sink and language-bar adapters do not establish real OS dispatch or visual
candidate rendering in registered applications. No default DLL was replaced,
and no input-method registration was performed.

## Remaining Validation

Real registered-TIP applications, physical mixed-DPI monitor transitions,
themes/high contrast and accessibility text scaling remain unverified. Font
coverage and newer color-font formats depend on the OS and installed fonts.
There is no TSF layout-change subscription: scrolling/moving the host may need
another snapshot to refresh the anchor. Oversized pages are height-clamped
without scrolling. Candidate mouse interaction and TSF UIElement/accessibility
remain separate work.
