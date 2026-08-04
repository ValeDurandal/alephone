#define XR_USE_PLATFORM_WIN32
#define XR_USE_GRAPHICS_API_OPENGL

#include <windows.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <cstdio>

void Aleph_OpenXR_SmokeTest()
{
    FILE* f = nullptr;
    fopen_s(&f, "openxr_smoke.txt", "w");
    if (!f) return;

    std::fputs("smoke: start\n", f);
    std::fflush(f);

    XrInstance instance = XR_NULL_HANDLE;
    XrSession session = XR_NULL_HANDLE;

    XrInstanceCreateInfo createInfo{ XR_TYPE_INSTANCE_CREATE_INFO };
    createInfo.applicationInfo.applicationVersion = 1;
    createInfo.applicationInfo.engineVersion = 1;
    createInfo.applicationInfo.apiVersion = XR_MAKE_VERSION(1, 0, 0);

    const char* app = "Aleph One";
    for (int i = 0; app[i] && i < XR_MAX_APPLICATION_NAME_SIZE - 1; ++i)
        createInfo.applicationInfo.applicationName[i] = app[i];
    for (int i = 0; app[i] && i < XR_MAX_ENGINE_NAME_SIZE - 1; ++i)
        createInfo.applicationInfo.engineName[i] = app[i];

    // Need OpenGL enable extension for the graphics binding
    const char* extensions[] = { XR_KHR_OPENGL_ENABLE_EXTENSION_NAME };
    createInfo.enabledExtensionCount = 1;
    createInfo.enabledExtensionNames = extensions;

    XrResult result = xrCreateInstance(&createInfo, &instance);
    std::fprintf(f, "xrCreateInstance: %d\n", (int)result);
    std::fflush(f);
    if (!XR_SUCCEEDED(result) || !instance)
    {
        std::fclose(f);
        return;
    }

    XrSystemGetInfo systemInfo{ XR_TYPE_SYSTEM_GET_INFO };
    systemInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    XrSystemId systemId = XR_NULL_SYSTEM_ID;
    result = xrGetSystem(instance, &systemInfo, &systemId);
    std::fprintf(f, "xrGetSystem: %d\n", (int)result);
    std::fflush(f);
    if (!XR_SUCCEEDED(result))
    {
        xrDestroyInstance(instance);
        std::fclose(f);
        return;
    }

    // Optional: graphics requirements (good practice)
    PFN_xrGetOpenGLGraphicsRequirementsKHR pfnGetReqs = nullptr;
    xrGetInstanceProcAddr(instance, "xrGetOpenGLGraphicsRequirementsKHR",
        (PFN_xrVoidFunction*)&pfnGetReqs);
    if (pfnGetReqs)
    {
        XrGraphicsRequirementsOpenGLKHR reqs{ XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_KHR };
        result = pfnGetReqs(instance, systemId, &reqs);
        std::fprintf(f, "xrGetOpenGLGraphicsRequirementsKHR: %d\n", (int)result);
        std::fflush(f);
    }

    // Bind current GL context (must be current — we call this after glewInit)
    XrGraphicsBindingOpenGLWin32KHR binding{ XR_TYPE_GRAPHICS_BINDING_OPENGL_WIN32_KHR };
    binding.hDC = wglGetCurrentDC();
    binding.hGLRC = wglGetCurrentContext();

    if (!binding.hDC || !binding.hGLRC)
    {
        std::fputs("ERROR: no current GL DC/context\n", f);
        xrDestroyInstance(instance);
        std::fclose(f);
        return;
    }

    XrSessionCreateInfo sessionInfo{ XR_TYPE_SESSION_CREATE_INFO };
    sessionInfo.next = &binding;
    sessionInfo.systemId = systemId;

    result = xrCreateSession(instance, &sessionInfo, &session);
    std::fprintf(f, "xrCreateSession: %d\n", (int)result);
    std::fflush(f);

    if (XR_SUCCEEDED(result) && session != XR_NULL_HANDLE)
    {
        std::fputs("session: created OK\n", f);
        xrDestroySession(session);
        std::fputs("session: destroyed\n", f);
    }

    xrDestroyInstance(instance);
    std::fputs("smoke: done\n", f);
    std::fclose(f);
}