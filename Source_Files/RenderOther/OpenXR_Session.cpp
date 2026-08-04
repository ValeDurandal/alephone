// Persistent OpenXR session + minimal frame loop (Step B).
// See OpenXR_Session.h for the contract. Windows + OpenGL + SteamVR.
//
// What this does, once per presented frame:
//   poll events -> (xrBeginSession when READY) -> xrWaitFrame ->
//   xrBeginFrame -> for each eye: acquire image, clear to a solid
//   color into a scratch FBO, release -> xrEndFrame with a projection
//   layer. Result: a solid color fills the headset.
//
// Everything is gated behind g_active; if any setup step fails we log it,
// tear down what we made, and leave g_active == false so Frame()/Shutdown()
// become no-ops and the normal monitor path is completely unaffected.

#define XR_USE_PLATFORM_WIN32
#define XR_USE_GRAPHICS_API_OPENGL

#include <windows.h>

#define GLEW_STATIC 1
#include <GL/glew.h>

#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include "OpenXR_Session.h"

#include <cstdio>
#include <cstdarg>
#include <vector>

// GL internal color formats reported by the runtime (avoid extra headers).
#ifndef GL_RGBA8
#define GL_RGBA8        0x8058
#endif
#ifndef GL_SRGB8_ALPHA8
#define GL_SRGB8_ALPHA8 0x8C43
#endif

namespace {

struct SwapchainInfo {
    XrSwapchain handle = XR_NULL_HANDLE;
    int32_t     width  = 0;
    int32_t     height = 0;
    std::vector<XrSwapchainImageOpenGLKHR> images;
};

XrInstance    g_instance = XR_NULL_HANDLE;
XrSystemId    g_systemId = XR_NULL_SYSTEM_ID;
XrSession     g_session  = XR_NULL_HANDLE;
XrSpace       g_space    = XR_NULL_HANDLE;

const XrViewConfigurationType   g_viewType  = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
const XrEnvironmentBlendMode    g_blendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;

std::vector<XrViewConfigurationView> g_configViews;
std::vector<SwapchainInfo>           g_swapchains;
int64_t   g_colorFormat = 0;

XrSessionState g_sessionState   = XR_SESSION_STATE_UNKNOWN;
bool           g_sessionRunning = false;   // between xrBeginSession/xrEndSession
bool           g_active         = false;   // Init() fully succeeded

GLuint g_fbo       = 0;   // scratch FBO: swapchain image bound as color, we blit into it
GLuint g_captureFbo = 0;  // holds a copy of the last monitor frame
GLuint g_captureTex = 0;
int    g_capW = 0, g_capH = 0;

FILE*  g_log       = nullptr;
int    g_cycle     = 0;    // Init count this process (detects re-init churn)
long   g_frameNum  = 0;    // frames since this cycle began rendering

void L(const char* fmt, ...)
{
    if (!g_log) return;
    va_list ap; va_start(ap, fmt);
    std::vfprintf(g_log, fmt, ap);
    va_end(ap);
    std::fputc('\n', g_log);
    std::fflush(g_log);
}

// ---------------------------------------------------------------------------
// Init helpers
// ---------------------------------------------------------------------------

bool CreateInstanceAndSystem()
{
    XrInstanceCreateInfo createInfo{ XR_TYPE_INSTANCE_CREATE_INFO };
    createInfo.applicationInfo.applicationVersion = 1;
    createInfo.applicationInfo.engineVersion      = 1;
    createInfo.applicationInfo.apiVersion         = XR_MAKE_VERSION(1, 0, 0);

    const char* app = "Aleph One";
    for (int i = 0; app[i] && i < XR_MAX_APPLICATION_NAME_SIZE - 1; ++i)
        createInfo.applicationInfo.applicationName[i] = app[i];
    for (int i = 0; app[i] && i < XR_MAX_ENGINE_NAME_SIZE - 1; ++i)
        createInfo.applicationInfo.engineName[i] = app[i];

    const char* extensions[] = { XR_KHR_OPENGL_ENABLE_EXTENSION_NAME };
    createInfo.enabledExtensionCount = 1;
    createInfo.enabledExtensionNames = extensions;

    XrResult r = xrCreateInstance(&createInfo, &g_instance);
    L("xrCreateInstance: %d", (int)r);
    if (!XR_SUCCEEDED(r) || !g_instance) return false;

    XrSystemGetInfo sys{ XR_TYPE_SYSTEM_GET_INFO };
    sys.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    r = xrGetSystem(g_instance, &sys, &g_systemId);
    L("xrGetSystem: %d", (int)r);
    return XR_SUCCEEDED(r);
}

bool CreateSession()
{
    // Spec requires calling graphics requirements before xrCreateSession.
    PFN_xrGetOpenGLGraphicsRequirementsKHR pfn = nullptr;
    xrGetInstanceProcAddr(g_instance, "xrGetOpenGLGraphicsRequirementsKHR",
        (PFN_xrVoidFunction*)&pfn);
    if (pfn) {
        XrGraphicsRequirementsOpenGLKHR reqs{ XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_KHR };
        XrResult r = pfn(g_instance, g_systemId, &reqs);
        L("xrGetOpenGLGraphicsRequirementsKHR: %d", (int)r);
    }

    XrGraphicsBindingOpenGLWin32KHR binding{ XR_TYPE_GRAPHICS_BINDING_OPENGL_WIN32_KHR };
    binding.hDC   = wglGetCurrentDC();
    binding.hGLRC = wglGetCurrentContext();
    if (!binding.hDC || !binding.hGLRC) {
        L("ERROR: no current GL DC/context at Init");
        return false;
    }

    XrSessionCreateInfo info{ XR_TYPE_SESSION_CREATE_INFO };
    info.next     = &binding;
    info.systemId = g_systemId;

    XrResult r = xrCreateSession(g_instance, &info, &g_session);
    L("xrCreateSession: %d", (int)r);
    return XR_SUCCEEDED(r) && g_session != XR_NULL_HANDLE;
}

bool CreateSwapchains()
{
    uint32_t viewCount = 0;
    XrResult r = xrEnumerateViewConfigurationViews(
        g_instance, g_systemId, g_viewType, 0, &viewCount, nullptr);
    if (!XR_SUCCEEDED(r) || viewCount == 0) { L("view enum failed: %d", (int)r); return false; }

    g_configViews.assign(viewCount, { XR_TYPE_VIEW_CONFIGURATION_VIEW });
    r = xrEnumerateViewConfigurationViews(
        g_instance, g_systemId, g_viewType, viewCount, &viewCount, g_configViews.data());
    if (!XR_SUCCEEDED(r)) { L("view enum(fill) failed: %d", (int)r); return false; }
    L("viewCount=%u recommended=%ux%u", viewCount,
        g_configViews[0].recommendedImageRectWidth,
        g_configViews[0].recommendedImageRectHeight);

    // Choose a color format (prefer sRGB8_alpha8, then RGBA8, else first).
    uint32_t formatCount = 0;
    xrEnumerateSwapchainFormats(g_session, 0, &formatCount, nullptr);
    std::vector<int64_t> formats(formatCount);
    if (formatCount) xrEnumerateSwapchainFormats(g_session, formatCount, &formatCount, formats.data());
    const int64_t preferred[] = { GL_SRGB8_ALPHA8, GL_RGBA8 };
    for (int64_t want : preferred) {
        for (uint32_t i = 0; i < formatCount && !g_colorFormat; ++i)
            if (formats[i] == want) g_colorFormat = want;
        if (g_colorFormat) break;
    }
    if (!g_colorFormat && formatCount) g_colorFormat = formats[0];
    L("chosen format: 0x%llx", (unsigned long long)g_colorFormat);
    if (!g_colorFormat) return false;

    g_swapchains.resize(viewCount);
    for (uint32_t i = 0; i < viewCount; ++i) {
        SwapchainInfo& sc = g_swapchains[i];
        sc.width  = (int32_t)g_configViews[i].recommendedImageRectWidth;
        sc.height = (int32_t)g_configViews[i].recommendedImageRectHeight;

        XrSwapchainCreateInfo ci{ XR_TYPE_SWAPCHAIN_CREATE_INFO };
        ci.usageFlags  = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
        ci.format      = g_colorFormat;
        ci.sampleCount = 1;
        ci.width       = sc.width;
        ci.height      = sc.height;
        ci.faceCount   = 1;
        ci.arraySize   = 1;
        ci.mipCount    = 1;

        r = xrCreateSwapchain(g_session, &ci, &sc.handle);
        L("xrCreateSwapchain[%u]: %d (%dx%d)", i, (int)r, sc.width, sc.height);
        if (!XR_SUCCEEDED(r)) return false;

        uint32_t imageCount = 0;
        xrEnumerateSwapchainImages(sc.handle, 0, &imageCount, nullptr);
        sc.images.assign(imageCount, { XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR });
        if (imageCount)
            xrEnumerateSwapchainImages(sc.handle, imageCount, &imageCount,
                (XrSwapchainImageBaseHeader*)sc.images.data());
        L("  swapchain[%u] imageCount=%u", i, imageCount);
        if (imageCount == 0) return false;
    }
    return true;
}

bool CreateReferenceSpace()
{
    XrReferenceSpaceCreateInfo ci{ XR_TYPE_REFERENCE_SPACE_CREATE_INFO };
    ci.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
    ci.poseInReferenceSpace.orientation.w = 1.0f;   // identity
    XrResult r = xrCreateReferenceSpace(g_session, &ci, &g_space);
    L("xrCreateReferenceSpace(LOCAL): %d", (int)r);
    return XR_SUCCEEDED(r);
}

// ---------------------------------------------------------------------------
// Per-frame helpers
// ---------------------------------------------------------------------------

void PollEvents()
{
    XrEventDataBuffer ev{ XR_TYPE_EVENT_DATA_BUFFER };
    while (xrPollEvent(g_instance, &ev) == XR_SUCCESS) {
        if (ev.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
            auto* e = reinterpret_cast<XrEventDataSessionStateChanged*>(&ev);
            g_sessionState = e->state;
            L("session state -> %d", (int)g_sessionState);

            if (g_sessionState == XR_SESSION_STATE_READY && !g_sessionRunning) {
                XrSessionBeginInfo bi{ XR_TYPE_SESSION_BEGIN_INFO };
                bi.primaryViewConfigurationType = g_viewType;
                XrResult r = xrBeginSession(g_session, &bi);
                L("xrBeginSession: %d", (int)r);
                g_sessionRunning = XR_SUCCEEDED(r);
            } else if (g_sessionState == XR_SESSION_STATE_STOPPING && g_sessionRunning) {
                XrResult r = xrEndSession(g_session);
                L("xrEndSession: %d", (int)r);
                g_sessionRunning = false;
            }
        }
        ev = { XR_TYPE_EVENT_DATA_BUFFER };
    }
}

// Fill one acquired swapchain image: clear to teal (fallback), then blit the
// captured monitor frame over it (scaled to the eye size). Returns true if the
// captured frame was blitted. Pure framebuffer ops — no engine render_view, so
// this is safe at any point in the game's lifetime.
bool RenderEyeImage(int eye, GLuint texture, int32_t w, int32_t h)
{
    GLint prevRead = 0, prevDraw = 0;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevRead);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevDraw);

    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, g_fbo);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
        GL_TEXTURE_2D, texture, 0);

    if (g_frameNum < 1) {   // one-time completeness check
        GLenum status = glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER);
        if (status != GL_FRAMEBUFFER_COMPLETE)
            L("WARN: eye FBO incomplete: 0x%x", (unsigned)status);
    }

    glViewport(0, 0, w, h);
    glClearColor(0.10f, 0.55f, 0.60f, 1.0f);   // teal fallback base
    glClear(GL_COLOR_BUFFER_BIT);

    bool blitted = false;
    if (g_captureFbo && g_capW > 0 && g_capH > 0) {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, g_captureFbo);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, g_fbo);
        glDisable(GL_FRAMEBUFFER_SRGB);
        // Straight copy (no Y flip): the OpenGL swapchain image shares GL's
        // bottom-left origin with our capture, so the orientation already
        // matches what the compositor expects.
        glBlitFramebuffer(0, 0, g_capW, g_capH, 0, 0, w, h,
            GL_COLOR_BUFFER_BIT, GL_LINEAR);
        blitted = true;
    }

    // Detach and restore the framebuffer bindings the game expects.
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, g_fbo);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
        GL_TEXTURE_2D, 0, 0);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)prevRead);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)prevDraw);
    return blitted;
}

// Copy the current default framebuffer (the just-rendered monitor frame) into
// our capture texture. Called before SDL_GL_SwapWindow, while the back buffer
// still holds the frame.
void CaptureDefaultFramebuffer(int w, int h)
{
    if (w <= 0 || h <= 0) return;

    if (!g_captureFbo) glGenFramebuffers(1, &g_captureFbo);
    if (!g_captureTex || w != g_capW || h != g_capH) {
        if (g_captureTex) glDeleteTextures(1, &g_captureTex);
        glGenTextures(1, &g_captureTex);
        glBindTexture(GL_TEXTURE_2D, g_captureTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA,
            GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glBindTexture(GL_TEXTURE_2D, 0);
        glBindFramebuffer(GL_FRAMEBUFFER, g_captureFbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
            GL_TEXTURE_2D, g_captureTex, 0);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        g_capW = w; g_capH = h;
    }

    GLint prevRead = 0, prevDraw = 0;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevRead);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevDraw);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);            // the just-drawn frame
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, g_captureFbo);
    glBlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)prevRead);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)prevDraw);
}

} // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void Aleph_OpenXR_CaptureFromDefaultFramebuffer(int w, int h)
{
    if (!g_active) return;
    CaptureDefaultFramebuffer(w, h);
}

bool Aleph_OpenXR_Init()
{
    if (g_active) return true;

    // Open once per process (truncate); keep it open across Init/Shutdown
    // cycles so re-init churn is visible instead of truncated away.
    if (!g_log) fopen_s(&g_log, "openxr_session.txt", "w");
    ++g_cycle;
    g_frameNum = 0;
    L("init: start (cycle %d)", g_cycle);

    if (!CreateInstanceAndSystem() ||
        !CreateSession() ||
        !CreateSwapchains() ||
        !CreateReferenceSpace()) {
        L("init: FAILED — tearing down");
        Aleph_OpenXR_Shutdown();
        return false;
    }

    glGenFramebuffers(1, &g_fbo);
    L("scratch FBO=%u", g_fbo);

    g_active = true;
    L("init: OK (session created; waiting for READY)");
    return true;
}

void Aleph_OpenXR_Frame()
{
    if (!g_active) return;

    PollEvents();
    if (!g_sessionRunning) return;   // not begun yet, or stopping

    XrFrameWaitInfo waitInfo{ XR_TYPE_FRAME_WAIT_INFO };
    XrFrameState    frameState{ XR_TYPE_FRAME_STATE };
    if (!XR_SUCCEEDED(xrWaitFrame(g_session, &waitInfo, &frameState))) return;

    XrFrameBeginInfo beginInfo{ XR_TYPE_FRAME_BEGIN_INFO };
    if (!XR_SUCCEEDED(xrBeginFrame(g_session, &beginInfo))) return;

    std::vector<XrCompositionLayerProjectionView> projViews;
    XrCompositionLayerProjection layer{ XR_TYPE_COMPOSITION_LAYER_PROJECTION };
    const XrCompositionLayerBaseHeader* layers[1] = { nullptr };
    uint32_t layerCount = 0;

    // Diagnostics for this frame.
    XrResult locateResult = XR_SUCCESS;
    XrViewStateFlags viewFlags = 0;
    bool  posesValid   = false;
    int   eyesRendered = 0;   // eyes whose image we acquired + cleared
    int   mirrorEyes   = 0;   // eyes that got the captured monitor frame

    if (frameState.shouldRender) {
        // Locate the eye views for this frame's predicted display time.
        const uint32_t viewCapacity = (uint32_t)g_configViews.size();
        std::vector<XrView> views(viewCapacity, { XR_TYPE_VIEW });
        XrViewState viewState{ XR_TYPE_VIEW_STATE };
        XrViewLocateInfo locate{ XR_TYPE_VIEW_LOCATE_INFO };
        locate.viewConfigurationType = g_viewType;
        locate.displayTime           = frameState.predictedDisplayTime;
        locate.space                 = g_space;

        uint32_t viewCountOut = 0;
        locateResult = xrLocateViews(g_session, &locate, &viewState,
            viewCapacity, &viewCountOut, views.data());
        viewFlags = viewState.viewStateFlags;

        const bool orientationValid = (viewFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT) != 0;
        const bool positionValid    = (viewFlags & XR_VIEW_STATE_POSITION_VALID_BIT) != 0;
        posesValid = orientationValid && positionValid;

        // Render whenever the locate call itself succeeded. A projection layer
        // only needs a *structurally* valid pose, not a good tracking lock, so
        // we sanitize instead of skipping — the solid color then shows even
        // before/without full tracking (headset still establishing, on a desk).
        if (XR_SUCCEEDED(locateResult) && viewCountOut > 0) {
            projViews.resize(viewCountOut);
            for (uint32_t i = 0; i < viewCountOut; ++i) {
                SwapchainInfo& sc = g_swapchains[i];

                uint32_t imgIndex = 0;
                XrSwapchainImageAcquireInfo acq{ XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO };
                if (!XR_SUCCEEDED(xrAcquireSwapchainImage(sc.handle, &acq, &imgIndex)))
                    continue;

                XrSwapchainImageWaitInfo wait{ XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
                wait.timeout = XR_INFINITE_DURATION;
                xrWaitSwapchainImage(sc.handle, &wait);

                if (RenderEyeImage((int)i, sc.images[imgIndex].image,
                                   sc.width, sc.height))
                    ++mirrorEyes;

                XrSwapchainImageReleaseInfo rel{ XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
                xrReleaseSwapchainImage(sc.handle, &rel);
                ++eyesRendered;

                // Sanitize the pose so xrEndFrame always gets a valid quaternion.
                XrPosef pose = views[i].pose;
                if (!orientationValid)
                    pose.orientation = { 0.0f, 0.0f, 0.0f, 1.0f };
                if (!positionValid)
                    pose.position = { 0.0f, 0.0f, 0.0f };

                XrCompositionLayerProjectionView& pv = projViews[i];
                pv = { XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW };
                pv.pose = pose;
                pv.fov  = views[i].fov;
                pv.subImage.swapchain               = sc.handle;
                pv.subImage.imageArrayIndex         = 0;
                pv.subImage.imageRect.offset        = { 0, 0 };
                pv.subImage.imageRect.extent.width  = sc.width;
                pv.subImage.imageRect.extent.height = sc.height;
            }

            layer.space     = g_space;
            layer.viewCount = (uint32_t)projViews.size();
            layer.views     = projViews.data();
            layers[0]       = reinterpret_cast<XrCompositionLayerBaseHeader*>(&layer);
            layerCount      = 1;
        }
    }

    XrFrameEndInfo endInfo{ XR_TYPE_FRAME_END_INFO };
    endInfo.displayTime          = frameState.predictedDisplayTime;
    endInfo.environmentBlendMode = g_blendMode;
    endInfo.layerCount           = layerCount;
    endInfo.layers               = layers;
    XrResult endResult = xrEndFrame(g_session, &endInfo);

    // Quiet diagnostics: log the first few frames of each cycle (enough to
    // confirm the loop is submitting real layers), plus any frame where
    // xrEndFrame reports an error. No steady-state per-frame spam.
    ++g_frameNum;
    if (g_frameNum <= 3 || !XR_SUCCEEDED(endResult)) {
        L("frame %ld: shouldRender=%d locate=%d viewFlags=0x%llx posesValid=%d "
          "eyes=%d mirrorEyes=%d cap=%dx%d layerCount=%u endFrame=%d",
          g_frameNum, (int)frameState.shouldRender, (int)locateResult,
          (unsigned long long)viewFlags, (int)posesValid, eyesRendered,
          mirrorEyes, g_capW, g_capH, layerCount, (int)endResult);
    }
}

void Aleph_OpenXR_Shutdown()
{
    if (g_fbo)        { glDeleteFramebuffers(1, &g_fbo);        g_fbo = 0; }
    if (g_captureFbo) { glDeleteFramebuffers(1, &g_captureFbo); g_captureFbo = 0; }
    if (g_captureTex) { glDeleteTextures(1, &g_captureTex);     g_captureTex = 0; }
    g_capW = g_capH = 0;

    if (g_sessionRunning) { xrEndSession(g_session); g_sessionRunning = false; }

    for (auto& sc : g_swapchains)
        if (sc.handle != XR_NULL_HANDLE) xrDestroySwapchain(sc.handle);
    g_swapchains.clear();
    g_configViews.clear();

    if (g_space   != XR_NULL_HANDLE) { xrDestroySpace(g_space);      g_space   = XR_NULL_HANDLE; }
    if (g_session != XR_NULL_HANDLE) { xrDestroySession(g_session);  g_session = XR_NULL_HANDLE; }
    if (g_instance!= XR_NULL_HANDLE) { xrDestroyInstance(g_instance);g_instance= XR_NULL_HANDLE; }

    g_systemId       = XR_NULL_SYSTEM_ID;
    g_colorFormat    = 0;
    g_sessionState   = XR_SESSION_STATE_UNKNOWN;
    g_active         = false;

    // Keep the log file open for the life of the process (see Init).
    L("shutdown: done (cycle %d)", g_cycle);
}

bool Aleph_OpenXR_IsActive()
{
    return g_active;
}
