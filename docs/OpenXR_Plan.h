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