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