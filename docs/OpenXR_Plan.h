// OpenXR integration plan for Aleph One VR mod
// Status: planning / not implemented
//
// Prerequisite already done:
//   - Dual-FBO side-by-side stereo path (g_enable_stereo_prototype)
//   - Correct eye-position offsets, separation 72
//   - Weapons forced off in stereo; HUD remains mono
//
// Goal of first real OpenXR milestone:
//   Create an OpenXR instance + session that binds to Aleph One's
//   existing OpenGL context, then shut down cleanly.
//   No headset rendering yet.
//
// Natural insertion point:
//   AFTER normal SDL window + OpenGL context exist
//   BEFORE / alongside the main game render loop
//
// Windows OpenGL binding needed for xrCreateSession:
//   XrGraphicsBindingOpenGLWin32KHR {
//     type  = XR_TYPE_GRAPHICS_BINDING_OPENGL_WIN32_KHR
//     hDC   = device context of the game window
//     hGLRC = current OpenGL rendering context
//   }
//
// Startup sequence (first milestone):
//   1. Load OpenXR loader
//   2. xrCreateInstance   (app name "Aleph One", request OpenGL enable extension)
//   3. xrGetSystem        (XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY)
//   4. xrGetOpenGLGraphicsRequirementsKHR
//   5. Fill graphics binding with current HDC + HGLRC
//   6. xrCreateSession
//   7. Log success/failure; keep normal monitor rendering working
//
// Shutdown:
//   xrDestroySession, xrDestroyInstance
//
// Later milestones (not now):
//   - Swapchains sized to runtime recommendation
//   - Frame loop: xrWaitFrame / xrBeginFrame / xrLocateViews / render each eye / xrEndFrame
//   - Replace invented eye offsets with OpenXR poses + FOVs
//   - Feed swapchain images instead of (or in addition to) our FBOs
//
// Mapping to current code:
//   Current:  player camera → left/right origin offsets → render_view into FBOs → composite to window
//   Future:   OpenXR views  → set view_data from pose+FOV → render_view into swapchain image → submit
// 
// Notes:
// 
//	 OpenGL context is created in screen.cpp around the
//	 SDL_CreateWindow + SDL_GL_CreateContext(main_screen) block
//	 (when acceleration == _opengl_acceleration).
//	 OpenXR session must be created AFTER that succeeds.
//
// After successful SDL_GL_CreateContext(main_screen)[and glewInit on Win32]:
//
// Pseudocode only — not implemented
//	 SDL_SysWMinfo wmInfo;
//	 SDL_VERSION(&wmInfo.version);
//	 SDL_GetWindowWMInfo(main_screen, &wmInfo);
//	 HDC   hDC = GetDC(wmInfo.info.win.window);
//	 HGLRC hGLRC = wglGetCurrentContext();
//
// Then eventually:
// XrGraphicsBindingOpenGLWin32KHR binding = { ... hDC, hGLRC ... };
// xrCreateSession(..., &binding, ...);
//
// ## Environment & insertion point (filled in)
//
// -**Active OpenXR runtime : **SteamVR
// `C:\Program Files(x86)\Steam\steamapps\common\SteamVR\steamxr_win64.json`
// - **Also available : **Meta Quest Link / Oculus
// - **OpenGL context created in : **`Source_Files/RenderOther / screen.cpp`
// Sequence: `SDL_CreateWindow` (with `SDL_WINDOW_OPENGL`) → `SDL_GL_CreateContext(main_screen)` → `glewInit()` (Win32)
// -**OpenXR session must start after : **that context creation succeeds(and only when using OpenGL acceleration)
// - **Graphics binding(Windows) will need : **
// -`HDC`  via `SDL_GetWindowWMInfo` → `GetDC(hwnd)`
// - `HGLRC` via `wglGetCurrentContext()` after the context is current
// - **First implementation milestone(unchanged) :**
// Create OpenXR instance + session bound to the existing GL context, then shut down cleanly.No headset rendering yet.
//
//
// ## Progress summary (as of 2026-08-03)
//
// ### Dual - FBO stereo(monitor side - by - side)
// - Optional path controlled by `g_enable_stereo_prototype` in `screen.cpp`
// - Left / right eye offsets(right - vector), separation 72
// - Half - width FBOs + per - eye `initialize_view_data` for improved aspect
// - Weapons - in - hand forced off while stereo is on; HUD remains mono on top
// - Rare distant visual glitches still possible but much reduced
// - Free - view(cross - eyed) only for now — no headset presentation yet
//
// ### OpenXR foundation
// - **Runtime:**SteamVR active(`steamxr_win64.json`)
//	- **Loader:**`openxr-loader` via vcpkg; `openxr_loader.dll` (+deps) beside the exe for debug runs
//	- **GL context landmark : **`screen.cpp` — after `SDL_GL_CreateContext` + `glewInit()`
//	- **Graphics binding(Windows, later) :**`HDC` via `SDL_GetWindowWMInfo` + `GetDC`; `HGLRC` via `wglGetCurrentContext`
//	- **Smoke test : **`OpenXR_Smoke.cpp` / `.h`, called once after `glewInit`
//	- `xrCreateInstance` succeeds with `XR_MAKE_VERSION(1, 0, 0)`
//	- `xrGetSystem(HMD)` returns `-35` (`XR_ERROR_FORM_FACTOR_UNAVAILABLE`) when no headset — expected
//	- Instance destroyed cleanly
//		- **Not done yet : **`xrCreateSession` with OpenGL binding, swapchains, frame loop, or feeding poses into `view_data`
//
//		### Next milestones
//		1. Connect HMD(SteamVR + Link / Air Link) and confirm `xrGetSystem` returns 0
//		2. Create / destroy session with `XrGraphicsBindingOpenGLWin32KHR`
//		3. Swapchains + minimal frame loop(clear only)
//		4. Render existing stereo path into OpenXR swapchain images using runtime poses / FOVs
//
//
// ## Progress summary (as of 2026-08-04)
//
// ### Dual - FBO stereo(monitor side - by - side)
//  - Optional path : `g_enable_stereo_prototype` in `screen.cpp`
//  - Eye offsets(right - vector), separation 72
//  - Half - width FBOs + per - eye `initialize_view_data`
//  - Weapons off in stereo; HUD mono on top
//  - Free - view only on monitor for now
//
// ### OpenXR foundation — COMPLETE through session
//  - **Runtime:**SteamVR(`steamxr_win64.json`), tested with Quest via Air Link
//    - **Loader:**vcpkg `openxr-loader`; DLLs beside exe for debug
//    - **GL context landmark : **`screen.cpp` after `SDL_GL_CreateContext` + `glewInit()`
//    - **Smoke test : **`OpenXR_Smoke.cpp` / `.h`
//    - `xrCreateInstance` OK(API 1.0.0)
//    - `xrGetSystem` OK when HMD present(−35 without headset is expected)
//    - `xrGetOpenGLGraphicsRequirementsKHR` OK
//    - `xrCreateSession` OK with `XrGraphicsBindingOpenGLWin32KHR` (current `hDC` / `hGLRC`)
//      - Session + instance destroyed cleanly
//      - **Environment notes : **
//      -Run Steam / SteamVR / Aleph One at the same elevation(not as Admin)
//      - Extra OpenXR API layers can be disabled under
//      `HKLM\SOFTWARE\Khronos\OpenXR\1\ApiLayers\Implicit` (DWORD 1 = disabled)
//
// ### Next milestones
//     1. Create OpenXR swapchains(runtime - recommended size / format)
//     2. Minimal frame loop : wait / begin → acquire → clear color → release → end
//     (solid color in the headset)
//     3. Render existing stereo path into swapchain images
//     4. Later : use `xrLocateViews` poses / FOVs instead of invented eye offsets
//
//
// ## Progress summary (as of 2026-08-04) — Steps A & B COMPLETE
//
// ### Step A — swapchain creation (verified)
//  - Extended the smoke test: enumerate view-config views, choose color
//    format, create per-eye swapchains, enumerate images, then destroy.
//  - Confirmed on Quest via SteamVR: 2 views @ 2064x2272,
//    format 0x8c43 (GL_SRGB8_ALPHA8), 3 images per swapchain.
//
// ### Step B — persistent session + minimal frame loop (verified: SOLID TEAL)
//  - **New helper:** `OpenXR_Session.cpp` / `.h`
//    (`Aleph_OpenXR_Init` / `Aleph_OpenXR_Frame` / `Aleph_OpenXR_Shutdown`).
//    Self-contained: owns its instance/session/space/swapchains + scratch FBO.
//    Every entry point is a no-op if init failed → cannot break monitor path.
//  - **Init** (after `glewInit`, replacing the one-shot smoke call):
//    instance → system → GL reqs → session → view-config → format →
//    swapchains → `xrCreateReferenceSpace(LOCAL)` → scratch FBO.
//    Does NOT begin the session; `xrBeginSession` happens on state READY.
//  - **Frame** (driven from `MainScreenSwap()` after `SDL_GL_SwapWindow`):
//    `xrPollEvent` (BeginSession@READY / EndSession@STOPPING) →
//    `xrWaitFrame` → `xrBeginFrame` → `xrLocateViews` → per eye
//    acquire / wait / clear-to-teal into scratch FBO / release →
//    `xrEndFrame` with a projection layer. Blend = OPAQUE, space = LOCAL.
//  - **Teardown wired twice, idempotent:**
//    - `screen.cpp` before `SDL_DestroyWindow` (window/GL-context recreation
//      on video-mode changes; also lets the following Init re-bind cleanly).
//    - `shell.cpp` `shutdown_application()` before `SDL_Quit()` (clean exit).
//  - **Gotcha fixed:** originally gated rendering on `posesValid` (position +
//    orientation valid bits). Before tracking locks, those bits are clear, so
//    we submitted empty frames (layerCount 0) → blank headset even though the
//    session still reached FOCUSED. Now we render whenever `xrLocateViews`
//    succeeds and *sanitize* the pose (identity orientation / zero position)
//    when bits are off — a projection layer only needs a structurally valid
//    pose, not a good lock. Teal now shows the moment the session submits.
//  - **Known cost (expected):** `xrWaitFrame` blocks the main thread to the
//    headset cadence, and Frame() runs off every `MainScreenSwap()`, so the
//    game paces to ~HMD refresh and feels heavier. Fine for this milestone;
//    decouple XR frame timing from the monitor present in a later step.
//  - **Monitor dual-FBO stereo path: untouched.**
//
// ### Next milestones
//     1. (done) swapchains
//     2. (done) minimal frame loop — solid color in headset
//     3. Render the existing per-eye game view into the swapchain images
//        instead of a flat clear.
//     4. Drive `view_data` from `xrLocateViews` pose/FOV, replacing the
//        invented eye offsets from the dual-FBO prototype.
//
//
// ## Progress summary (as of 2026-08-04) — Step C0 COMPLETE
//
// Goal of C0: get real, recognizable game pixels into the headset (any camera)
// to prove the pixel path end-to-end, before wiring head pose -> view_data.
//
// ### What shipped
//  - The teal clear is replaced by a **capture-and-mirror** of the monitor
//    frame into both eyes. Confirmed on Quest: right-side-up game imagery in
//    the headset (menu + in-game + HUD), with stereo depth, clean quit.
//  - `screen.cpp` `MainScreenSwap()`:
//      * BEFORE `SDL_GL_SwapWindow` — `Aleph_OpenXR_CaptureFromDefaultFramebuffer`
//        copies the finished back buffer (FB0) into a capture texture.
//      * AFTER the swap — `Aleph_OpenXR_Frame()` blits that capture into each
//        eye's swapchain image (scaled to the eye; teal remains the fallback).
//      * Both the capture and Frame calls are `#if __WIN32__ && HAVE_OPENGL`
//        guarded (also fixed the previously-unguarded Frame call).
//  - `OpenXR_Session.cpp`: added capture texture/FBO + `RenderEyeImage` blit;
//    frame log now reports `mirrorEyes` and capture size.
//
// ### Why NOT a second render_view (important for C1/C2)
//  First attempt called `render_view()` from the eye callback into a raw FBO.
//  Two hard blocks, both diagnosed:
//   1. **Target integration:** the world renderer composites through the
//      engine's `FBO`/`FBOSwapper` `active_chain` stack and restores to FB0 on
//      deactivate. A raw `glBindFramebuffer` is not on that stack, so
//      render_view's pixels landed in FB0 (already swapped, invisible) and the
//      eye kept its teal clear ("teal in game"). The dual-FBO monitor path
//      works only because it uses the engine `FBO` class (on the chain).
//   2. **Lifetime/timing:** calling render_view from `MainScreenSwap` (outside
//      the normal render pipeline) touched texture/collection state that is
//      invalid during the level-exit transition -> read AV in the texture
//      manager (`CTState`). render_view must run inside the engine's own
//      render pass, when textures are valid.
//  => For C1/C2, per-eye rendering must go into an engine `FBO` (so it joins
//     the active_chain) during a valid render pass, then be submitted to the
//     swapchain — not a second render_view bolted onto the present.
//
// ### Orientation note
//  OpenGL swapchain images share GL's bottom-left origin, so the capture->eye
//  blit is a STRAIGHT copy (no Y flip). An added flip made everything
//  upside-down; removing it fixed menu + both eyes + HUD uniformly.
//
// ### Known / expected (out of scope for C0)
//  - Headset mirrors the monitor, so with `g_enable_stereo_prototype = true`
//    each eye shows the full side-by-side composite; the stereo depth seen is
//    the prototype's fixed separation, NOT head-tracked per-eye views.
//  - Aspect is stretched to the ~square eye; colors/gamma approximate.
//  - No head tracking yet (camera == monitor camera). In-VR cursor is absent,
//    so quitting is easiest from the monitor.
//
// ### Next: C1
//  - Drive ONE eye's `view_data` from the real `xrLocateViews` pose/FOV
//    (head-tracked camera), rendered via an engine FBO into that eye's
//    swapchain image. Then C2: both eyes from runtime views; retire the
//    invented dual-FBO offsets when XR is active.