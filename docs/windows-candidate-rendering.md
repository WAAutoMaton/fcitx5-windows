# Windows Candidate Rendering

## Decision and Scope

The nonactivating Win32 popup uses DirectWrite layouts and a Direct2D HWND
render target. Language-bar icons and document composition use separate paths.
The TSF target links Windows `d2d1` and `dwrite`; rendering alone does not require
Core, submodule, IPC or registration metadata changes.
The window and graphics resources stay on the TSF owning thread. Initialization
is lazy, outside `DllMain`. Snapshots still come from Core; rendering never
commits text. There is no render thread or continuous animation loop.

Candidate data is published independently through TSF UIElement/Behavior.
The host's BeginUIElement/Show permission gates popup display; missing HWND or
layout does not discard the host-readable snapshot. Host selection/finalize/abort
actions go to Core with the original context and stable candidate generation.
The popup itself has no mouse interaction. See
[modern UI policy](windows-modern-apps-design.md#候选-uielement-与自绘窗口).

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
restores the thread context. `show` accepts owner-space logical screen coordinates.
Per-Monitor-aware owners already use physical coordinates; other owners convert
both corners with `LogicalToPhysicalPointForPerMonitorDPI`. This avoids rejecting
valid text extents when modern hosts such as Windows Terminal use a visible,
zero-sized input-site HWND. Conversion failures hide the popup and log the failed
corner, owner awareness/DPI/client dimensions and anchor coordinates when detailed
logging is enabled. A null owner assumes physical coordinates. A clipped
composition falls back to the collapsed selection range. If that caret is also
reported as clipped, valid geometry overlapping the document display returned by
`GetScreenExt` can still anchor the popup. This accommodates hosts that report a
visible caret as clipped. Empty or non-overlapping geometry, unavailable document
display bounds, hidden/minimized owners, invalid UTF-8 and empty candidates hide
the popup. Detailed logging records the display bounds and the clipped-caret
acceptance decision as `candidate_clipped_caret`.

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

`test_candidate` is a default CTest with no Core or registration requirement.
The production draw function runs through a software WIC target at 96/120/144/
192/288 DPI. It compares color emoji with monochrome controls and checks VS15,
ellipsis and layout reuse. Window tests cover three owner awareness modes,
thread-state restoration, focus, owner lifetime, empty/invalid input, work-area
bounds, immediate paint, target-loss recovery and bounded repaint retries.
Zero-sized per-monitor input sites are accepted without logical coordinate
conversion; zero-sized owners requiring failed conversion remain hidden.

`test_candidate_tsf` uses a real TSF context backed by the shared in-memory text
store, without Core, IPC or registration. It checks repeated TS_E_NOLAYOUT,
content at retained anchors, merged read-only layout sessions, applied versus
queued snapshots, clipped-composition caret fallback, clipped carets overlapping
document bounds, invalid/empty/offscreen geometry, timeout/recovery, commit,
cancel, hidden owners and late sessions after focus loss/deactivation.
It also checks host UIElement show/hide and candidate publication without HWND
or usable popup geometry. These are explicit adapter callbacks, not OS dispatch.

For the current default suite and real engine probes, see
[TSF tests](pinyin.md#build-and-test-tsf). Generate synthetic-DPI PNGs and briefly
show a native preview after building:

```powershell
./build/all/tsf/tests/test_candidate.exe build/candidate-preview
```

The output includes five offscreen images and a native HWND capture using
PrintWindow(PW_RENDERFULLCONTENT), independent of occlusion. Preview snapshots
are synthetic, not live engine candidates. Call-duration measurements from this
fixture do not establish end-to-end input latency, registered-app behavior or a
GDI performance comparison. Interactive desktop focus and installed fonts can
affect the native preview.

## Remaining Validation

Real registered-TIP applications, physical mixed-DPI monitor transitions,
themes/high contrast and accessibility text scaling remain unverified. Font
coverage and newer color-font formats depend on the OS and installed fonts.
Physical scrolling/moving behavior still needs application testing despite the
layout-change subscription. Oversized pages are height-clamped
without scrolling. Popup mouse interaction and full accessibility remain
incomplete. UIElement/Behavior has unregistered coverage, but real host rendering,
candidate actions and screen-reader behavior still need system acceptance.
