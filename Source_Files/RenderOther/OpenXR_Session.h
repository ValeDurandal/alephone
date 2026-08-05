#pragma once

// Persistent OpenXR session + minimal frame loop (Step B).
//
// This is a self-contained helper: it owns its own OpenXR instance,
// session, swapchains and a scratch FBO. It never touches the existing
// monitor / dual-FBO render path. Every entry point is a safe no-op if
// initialization failed, so wiring it in cannot break normal rendering.
//
// Usage (Windows + OpenGL only):
//   Aleph_OpenXR_Init()      once, right after the GL context + glewInit()
//   Aleph_OpenXR_CaptureFromDefaultFramebuffer(w,h)  before the buffer swap
//   Aleph_OpenXR_Frame()     once per presented frame (after the buffer swap)
//   Aleph_OpenXR_Shutdown()  once at teardown

bool Aleph_OpenXR_Init();

// C0: grab the just-rendered monitor frame from the default framebuffer (call
// BEFORE SDL_GL_SwapWindow, while the back buffer still holds the frame) into
// an internal capture texture. Aleph_OpenXR_Frame() then mirrors it into the
// headset. w/h are the drawable pixel size. No-op unless the session is active.
void Aleph_OpenXR_CaptureFromDefaultFramebuffer(int w, int h);

void Aleph_OpenXR_Frame();
void Aleph_OpenXR_Shutdown();
bool Aleph_OpenXR_IsActive();

// --- C1: per-eye real render driven by head pose --------------------------

// Recommended swapchain image size for an eye (0 = left, 1 = right).
void Aleph_OpenXR_GetEyeImageSize(int eye, int* w, int* h);

// Last pose + FOV that xrLocateViews returned for an eye (LOCAL space). Filled
// as plain floats so this header needs no OpenXR types: orientation quaternion
// (x,y,z,w), position (x,y,z) in meters, FOV angles (left,right,up,down) in
// radians. Any pointer may be null. Returns true if a pose has been located
// (may lag the current head pose by one frame).
bool Aleph_OpenXR_GetEyePose(int eye, float outQuatXYZW[4],
                             float outPosXYZ[3], float outFovLRUD[4]);

// Hand the session a GL framebuffer whose color attachment holds a freshly
// rendered eye image (w x h). Aleph_OpenXR_Frame() blits it into that eye's
// swapchain image instead of the monitor mirror. Pass glFbo = 0 to clear the
// source for this eye (falls back to the mirror). Set each frame; the session
// consumes and clears it after use.
void Aleph_OpenXR_SetEyeSourceFbo(int eye, unsigned int glFbo, int w, int h);

// Append a line to the OpenXR log (openxr_session.txt). For host-side debug.
void Aleph_OpenXR_LogLine(const char* msg);
