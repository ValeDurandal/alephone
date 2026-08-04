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
//   Aleph_OpenXR_Frame()     once per presented frame (after the buffer swap)
//   Aleph_OpenXR_Shutdown()  once at teardown

bool Aleph_OpenXR_Init();
void Aleph_OpenXR_Frame();
void Aleph_OpenXR_Shutdown();
bool Aleph_OpenXR_IsActive();
