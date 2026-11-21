# Windows Candidate Rendering

## Decision and Scope

The nonactivating Win32 candidate popup uses DirectWrite layouts and a Direct2D
HWND render target, with an `ID2D1DCRenderTarget` software fallback.
Language-bar icons and document composition use separate paths. The TSF target
links Windows `d2d1` and `dwrite`.

Window and graphics resources stay on the TSF owning thread, initialized lazily
outside `DllMain`, with no background render thread or animation loop. Snapshots
come from Core and rendering never commits text.

Candidate data is published independently through TSF UIElement/Behavior.
Host `BeginUIElement`/`Show` permission gates popup visibility; a missing HWND
or unavailable layout preserves the host-readable snapshot. Host
selection/finalize/abort actions are sent to Core with the original context and
stable candidate generation. The popup itself supports keyboard selection only.
See [modern UI policy](windows-modern-apps-design.md#候选-uielement-与自绘窗口).

## Typography and Layout

Labels, bodies and comments use separate DirectWrite layouts (16 DIP Segoe UI
for body, 13 DIP for secondary text, with system font fallback). Full UTF-16
strings—including surrogate pairs, ZWJ sequences, skin-tone modifiers and
combining marks—render with `D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT`.
Explicit VS15 text-presentation sequences prefer Segoe UI Symbol so monochrome
text symbols stay distinct from VS16 color emoji; glyph coverage depends on
Windows and installed fonts.

Measurement and drawing share the same layouts:

- Row height accounts for text height, vertical glyph overhang and padding
  (minimum 32 DIP).
- **Vertical layout (default)** aligns label, body and comment columns, bounded
  to 180–640 DIP and clamped to the monitor work area.
- **Horizontal layout** (**Global Settings > General**) keeps all entries on a
  single row without wrapping, sized tightly to content up to 1280 DIP (clamped
  to the monitor work area). When the page exceeds that width, a water-filling
  cap leaves shorter entries intact and trims only the widest entries to share
  the remaining width. Core sends the saved layout in candidate snapshots so the
  popup applies changes on the next update.
- Both layouts use DirectWrite ellipsis trimming for long text; when a cell
  overflows, comments receive at most 35% of remaining width (or the remaining
  space after a short body in horizontal layout). Per-item axis-aligned clipping
  prevents glyphs from bleeding across adjacent horizontal cells.
- System colors supply text, background, selection and border, invalidating on
  theme/color changes. A dedicated Windows 11 dark theme remains pending.

## DPI and Lifecycle

### Coordinate Conversion and Anchors

Edit sessions query `GetTextExt` under the owner's DPI awareness, then restore
the thread context. `show` accepts owner-space logical screen coordinates:

- Per-Monitor-aware owners already report physical coordinates (this accepts
  valid extents from modern hosts such as Windows Terminal with a visible
  zero-sized input-site HWND).
- Other owners convert both corners via `LogicalToPhysicalPointForPerMonitorDPI`.
  If an extent starts inside a child control but its end corner fails
  conversion (e.g., Everything's short search box), both corners are retried
  using the top-level window on the same monitor and DPI awareness context.
- Unresolved conversion failures hide the popup and log the failed corner,
  owner awareness/DPI/client bounds and anchor coordinates when detailed logging
  is enabled. A null owner assumes physical coordinates.
- A clipped composition falls back to the collapsed caret selection. If that
  caret is also flagged as clipped, geometry overlapping the document display
  from `GetScreenExt` can still anchor the popup (logged as
  `candidate_clipped_caret`).
- A zero-height or malformed composition extent also retries the collapsed
  selection. If the host supplies an unclipped point inside valid `GetScreenExt`
  bounds (e.g., game hosts such as Unity), it anchors the popup (logged as
  `candidate_point_anchor`). The all-zero rectangle remains
  [TSF's invisible-text sentinel](https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfcontextview-gettextext).
- Clipped geometry without a visible anchor, hidden/minimized owners, invalid
  UTF-8 and empty candidate lists hide the popup.

### Deferred Layout and `ITfTextLayoutSink`

`TS_E_NOLAYOUT` after a preedit write is treated as temporary: the visible popup
stays at its last physical anchor and updates with the applied snapshot for up
to 250 ms (never reusing anchors across context, view or owner changes, commit
or cancellation). If layout remains unavailable, the popup hides while keeping
the pending snapshot so a merged retry or the 100 ms timer can restore it once
geometry becomes valid.

The focused context advises `ITfTextLayoutSink`. Layout notifications post a
merged message requesting a read-only edit session (synchronous with async
fallback) bound to the original context, generation and last applied snapshot.
Late sessions after cancellation, focus change or deactivation are rejected.
Hosts that reject this optional sink fall back to the timer and regular
snapshots.

### Window Awareness, Device Loss and Software Fallback

Popup creation and positioning temporarily switch the thread to Per-Monitor V2
and restore it afterward without changing process awareness. The popup's own DPI
governs DIP-to-pixel scaling even for DPI-unaware owners. `WM_DPICHANGED` and
display changes recompute bounds; `WM_SIZE` resizes the target. Positioning
supports negative coordinates, clamps to the anchor monitor's work area and
flips above the anchor when space below is insufficient.

`WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW` and `MA_NOACTIVATE` preserve host focus.
Owner destruction clears the HWND and device resources for lazy recreation on
the next snapshot. On `D2DERR_RECREATE_TARGET`, device resources are discarded
and up to two repaint retries run at 16/32 ms without hiding; persistent or
other errors hide until the next snapshot without resetting or replaying input.

If HWND render-target creation fails, the window instance switches to
`ID2D1DCRenderTarget` with explicit software rendering, binding the paint DC and
full client rectangle on each paint while reusing the same DirectWrite layouts
and draw routine (`popup_render_fallback` logs the original HRESULT and
`dc_software`). Host UIElement permission and owner checks apply to both paths.

## Resource and Latency Policy

Factories, text formats, trimming signs, render targets and brushes are reused.
Unchanged candidate strings keep their DirectWrite layouts (including
selection-only updates). Render targets resize only when pixel dimensions
change. Frames draw in a single `BeginDraw`/`EndDraw` pair with
`D2D1_PRESENT_OPTIONS_IMMEDIATELY`, followed by `UpdateWindow` so updates paint
immediately without waiting for an idle `WM_PAINT`. See Microsoft's
[Direct2D performance guide](https://learn.microsoft.com/en-us/windows/win32/direct2d/improving-direct2d-performance)
and
[presentation options](https://learn.microsoft.com/en-us/windows/win32/api/d2d1/ne-d2d1-d2d1_present_options).

## Verification

- **`test_candidate`** (default CTest, no Core or registration):
  - Runs the production draw routine on a software WIC target at 96/120/144/192/
    288 DPI; verifies color emoji vs. monochrome controls, VS15, ellipsis,
    layout reuse and vertical/single-row horizontal layout.
  - Injects `DXGI_ERROR_NOT_CURRENTLY_AVAILABLE` into HWND-target creation to
    verify software DC fallback pixels, target reuse, resize, DPI changes and
    failure recovery.
  - Covers three owner awareness modes, thread-state restoration, focus, owner
    lifetime, work-area bounds, immediate paint, device-loss retries, zero-sized
    per-monitor input sites and child-edit overflow into top-level coordinates.
- **`test_candidate_tsf`** (default CTest, in-memory `ITextStoreACP`, no Core or
  registration):
  - Covers `TS_E_NOLAYOUT` anchor retention, merged read-only layout sessions,
    applied vs. queued snapshots, clipped-composition caret fallback, clipped
    carets overlapping document bounds, point anchors (including negative
    coordinates, clipped points and the all-zero sentinel), timeout/recovery,
    hidden owners, late sessions and host UIElement show/hide without an HWND.
- **`candidate_render_probe <AppContainer profile or package family name>`**:
  - Compares default/software HWND targets, software DC targets and both
    production paths across desktop and real AppContainer-token child processes.
- **Visual preview generation**:

  ```powershell
  ./build/all/tsf/tests/test_candidate.exe build/candidate-preview
  ```

  Produces five synthetic-DPI offscreen PNGs and a native HWND capture via
  `PrintWindow(PW_RENDERFULLCONTENT)`.

For the full CTest list and real engine probes, see
[TSF tests](pinyin.md#build-and-test-tsf).

## Remaining Validation

- Live registered-TIP testing across real desktop and packaged immersive hosts
  (e.g., SearchHost), mixed-DPI monitor transitions, scrolling/moving windows,
  dark/high-contrast themes and accessibility text scaling.
- Real host UIElement rendering, host candidate actions and screen-reader
  integration.
- Oversized candidate pages are height-clamped without scrolling; popup mouse
  interaction and full accessibility remain pending.
