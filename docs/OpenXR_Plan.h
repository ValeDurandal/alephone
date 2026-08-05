// OpenXR integration plan for Aleph One VR mod
//
// =====================================================================
// CURRENT STATE / RESUME HERE   (update at the end of each milestone)
// ---------------------------------------------------------------------
// Last updated: 2026-08-05, Step C2 in progress (BOTH eyes head-tracked).
// Branch: vr-mod
//
// Verified working:
//   - Step A: OpenXR swapchains created (2 views @ 2064x2272, sRGB8_alpha8).
//   - Step B: persistent session + minimal frame loop; solid color in Quest;
//     clean teardown on video-mode change and on app exit.
//   - Step C0: the monitor frame captured and MIRRORED into both eyes.
//   - Step C1: LEFT eye head-tracked engine render (committed checkpoint).
//   - Step C2 (playable, this checkpoint): BOTH eyes head-tracked; correct
//     per-eye FOV read from the runtime (the engine inflates field_of_view 1.3x,
//     so we set world_to_screen_* directly -> fixed the "zoomed" look);
//     render==submit fov so the eyes FUSE with real stereo depth; fixed IPD
//     (~64 WU, no per-frame spikes); monitor forced mono while XR active.
//     Aiming: VIEW FOLLOWS AIM - view pitch tracks the player's mouse elevation
//     (+/- small capped head assist) so the gun fires at view center. Eye offset
//     guarded by polygon walk (find_new_object_polygon) + floor/ceiling height
//     check to avoid rendering from a bad polygon near walls/stairs.
//   - PITCH-DOWN GLITCH: FIXED. Root cause was a normalized-vs-signed angle bug:
//     world_view->pitch is stored NORMALIZED [0,512) (looking down = ~469), but
//     the clamp treated it as signed and slammed it to +PITCH_LIMIT (straight
//     up). Now converted to signed before clamping (screen.cpp).
//   - TERMINALS / overhead map in headset: mirror the monitor as a fused mono
//     PANEL - clear the per-eye world source, give BOTH eyes the SAME symmetric
//     fov (else the mono image doubles), and blit only the terminal/map REGION
//     cropped + centered + scaled (PANEL ~0.65). Readable and fused.
//
// OPEN ISSUES (C2 polish / C3), next up:
//   1. TERMINAL TEARING: the flat 2D panel shimmers/tears through the headset's
//      reprojection (projection layers assume 3D depth). Proper fix: submit UI
//      via an XrCompositionLayerQuad (dedicated flat-panel layer) instead.
//   2. AIMING (head-yaw): view = facing + head_yaw but the gun uses facing, so
//      turning your head puts the gun off view-center horizontally ("sometimes
//      off"). Fix: cap head-yaw's influence, or "head drives the gun" (C3).
//   3. HEAD TRACKING positional: rotation-only + FIXED IPD, NO translational
//      tracking (leaning/moving your head doesn't move the camera). Wanted 6DoF.
//   4. NEAR-object DOUBLE VISION: inherent VR near-fusion limit; later IPD tune.
//   (Plus from C1: perf DEGRADES after ~5 min -> C3 pacing, single-threaded
//    3x world render + blocking xrWaitFrame.)
//
// Key files:
//   - Source_Files/RenderOther/OpenXR_Session.cpp / .h
//       Init / CaptureFromDefaultFramebuffer / Frame / Shutdown, all state.
//   - Source_Files/RenderOther/screen.cpp
//       * Aleph_OpenXR_Init() after glewInit() (~line 986).
//       * MainScreenSwap(): capture BEFORE SDL_GL_SwapWindow, Frame() AFTER.
//       * Aleph_OpenXR_Shutdown() before SDL_DestroyWindow (mode-change).
//       * dual-FBO monitor stereo block (g_enable_stereo_prototype) — leave
//         it working; it is untouched by the OpenXR path.
//   - Source_Files/shell.cpp  shutdown_application(): Shutdown() before SDL_Quit.
//
// Build (Windows / VS 2026):
//   MSBuild VisualStudio/LibAlephOne/LibAlephOne.vcxproj /p:Configuration=Debug
//     /p:Platform=x64 /t:Build   (LibAlephOne is a static lib — this only
//   relinks the LIB; the IDE must rebuild the EXE to actually run.)
//   Gotcha: a killed build can leave orphaned cl.exe / mspdbsrv.exe holding
//   LibAlephOne.pdb -> error C1041. Fix: kill those processes, rebuild.
//
// Runtime log: openxr_session.txt next to the exe (VisualStudio/AlephOne/,
//   gitignored). Healthy in-game line looks like:
//     frame N: shouldRender=1 locate=0 viewFlags=0xf posesValid=1 eyes=2
//              mirrorEyes=2 cap=WxH layerCount=1 endFrame=0
//
// HARD CONSTRAINTS (learned the hard way — see the C0/C1 sections below):
//   1. Per-eye engine rendering must target an ENGINE `FBO` (so it joins the
//      renderer's FBO active_chain), NOT a raw glBindFramebuffer — otherwise
//      render_view's pixels land in FB0, not the swapchain.
//   2. Never call render_view() from MainScreenSwap / the present path: outside
//      the engine's render pass the texture manager state is invalid during
//      transitions -> read AV in the texture manager (CTState). C1 renders the
//      eye inside render_screen (valid pass) into an engine FBO, then blits that
//      FBO into the swapchain in Frame() (a safe texture copy).
//   3. Capture->eye blit is a STRAIGHT copy, no Y flip (GL bottom-left origin
//      matches the GL swapchain image).
//   4. Keep the monitor dual-FBO stereo path working; every OpenXR entry point
//      is a no-op when the session is inactive.
//   5. Render the XR eye at the MONITOR viewport size, not the full 2064x2272
//      swapchain size: a huge render FBO crashed on level load, and mismatched
//      sizes churn the renderer's FBOs. Blit scales up to the swapchain.
//   6. view_data.pitch is a SIGNED angle used to index the 512-entry trig
//      tables. render.cpp now NORMALIZE_ANGLEs that index (was OOB/divide-by-
//      zero for negative or large pitch). Still keep |pitch| < QUARTER_CIRCLE
//      (cos=0 at 90 deg). Angle units: 512 = full circle (rad * 512/2pi).
//
// NEXT: Step C2 — proper per-eye projection from the runtime XrFovf (asymmetric,
//   off-center; the engine supports off-center via view->half_screen_width) at
//   the correct eye aspect, for BOTH eyes from their xrLocateViews poses. This
//   is what fixes the C1 environment warping. Retire the invented dual-FBO
//   offsets when XR is active. Then C3: pacing (decouple xrWaitFrame from the
//   main thread) to fix the perf degradation; HUD/weapons policy; mirror policy.
// =====================================================================
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
//
//
// ## Progress summary (as of 2026-08-04) — Step C1 COMPLETE (with caveats)
//
// Goal of C1: one eye (left) follows the headset — camera from the real
// xrLocateViews pose, real engine render for that eye. ACHIEVED: looking around
// moves the world in the left eye; played for minutes.
//
// ### How it works (the pattern that finally stuck)
//  - `OpenXR_Session` stores the last-located views and exposes the pose:
//    `Aleph_OpenXR_GetEyePose(eye, XrPosef*, XrFovf*)`,
//    `Aleph_OpenXR_GetEyeImageSize()`, and
//    `Aleph_OpenXR_SetEyeSourceFbo(eye, glFbo, w, h)` — a GL FBO the session
//    blits into that eye's swapchain instead of the mirror (0 => fall back).
//  - `screen.cpp` render_screen, AFTER the dual-FBO block (inside the valid
//    render pass, textures live): if XR active + in game + pose available,
//    render the LEFT eye into a persistent ENGINE `FBO` (so it joins the
//    renderer active_chain) at the MONITOR VIEWPORT SIZE, then hand that FBO to
//    the session. `Frame()` blits it into the left swapchain (straight copy).
//    Right eye stays the C0 mirror. Pose lags by ~1 frame (uses last frame's
//    locate); fine for C1.
//  - Added a 1-line getter `FBO::fbo()` (OGL_FBO.h) so the session can blit
//    from the engine FBO.
//
// ### Pose -> Marathon camera (rotation only for C1)
//  - Angles: 512 = full circle, so angle_units = radians * 512/(2*pi).
//  - Rotate forward (0,0,-1) by the pose quaternion (LOCAL space, +Y up,-Z fwd):
//      fx=-2(xz+wy)  fy=-2(yz-wx)  fz=-(1-2(x^2+y^2))
//      head_yaw   = atan2(fx,-fz) * 512/2pi   (YAW_SIGN tunable)
//      head_pitch = asin(fy)      * 512/2pi   (PITCH_SIGN tunable)
//  - world_view->yaw = NORMALIZE_ANGLE(player_facing + head_yaw); pitch =
//    clamp(player_elevation + head_pitch, +/-PITCH_LIMIT ~78 deg). Set
//    virtual_yaw/pitch = angle * FIXED_ONE. FOV from XrFovf horizontal (approx,
//    symmetric — the real fix is C2). Origin unchanged (no head translation yet).
//  - Head angles are RATE-LIMITED (~22 deg/frame) to bound tracking spikes.
//
// ### Bugs found & fixed IN SHARED ENGINE CODE (both latent, pre-existing)
//   1. Divide-by-zero: render.cpp:621 `dtanpitch = .../cosine_table[view->pitch]`
//      indexed the 512-entry table with the SIGNED pitch and no NORMALIZE.
//      Negative pitch read out of bounds (could hit 0); exactly +/-90 deg is 0.
//      Fixed by NORMALIZE_ANGLE-ing the index. Callers still keep |pitch| < 90.
//   2. Texture leak: FBO::~FBO() freed the framebuffer + depth buffer but NOT
//      its color texture (texID). Any FBO recreation leaked a texture. Fixed by
//      adding glDeleteTextures in the destructor.
//
// ### Crash trail (for future reference — all resolved)
//   - render_view into the full 2064x2272 swapchain-size FBO crashed on level
//     load. Fix: render at the monitor viewport size, blit-scale to the eye.
//   - Non-ASCII in a comment / a stray "*/" in render.cpp broke the build.
//   - "Snap straight up" was invalid-orientation poses driving the camera; now
//     the pose only latches when the orientation-valid bit is set.
//
// ### Known limitations (NOT bugs — these define C2 / C3)
//  - WARPING / wrong perspective: symmetric FOV + stretch from viewport aspect
//    to the ~square eye. C2 fixes this with true asymmetric per-eye projection.
//  - Only the LEFT eye is head-tracked; right eye is the mirror. C2 does both.
//  - PERFORMANCE degrades to unplayable after ~5 min: 3x world render/frame +
//    2 big blits + blocking xrWaitFrame, all single-threaded, so normal scene
//    growth tips past the VR frame budget (reprojection). C3 = pacing/decouple.
//    (Could also hide a residual leak; needs profiling.)
//  - Occasional pose glitch remains; revisit with C2 projection + C3 pacing.
//
// ### Next: C2
//  - Build the projection from XrFovf per eye (asymmetric/off-center via
//    view->half_screen_width) at the correct eye aspect; render BOTH eyes from
//    their xrLocateViews poses; stop using the invented dual-FBO offsets when
//    XR is active. Then C3 for pacing + HUD/weapons + mirror policy.