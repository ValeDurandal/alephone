#define XR_USE_PLATFORM_WIN32
#define XR_USE_GRAPHICS_API_OPENGL

#include <windows.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <cstdio>
#include <vector>

// GL internal color formats (avoid pulling in full GL headers here).
// OpenXR reports supported swapchain formats as GL enums.
#ifndef GL_RGBA8
#define GL_RGBA8       0x8058
#endif
#ifndef GL_SRGB8_ALPHA8
#define GL_SRGB8_ALPHA8 0x8C43
#endif

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

        // --- Step A: swapchain creation (no presentation yet) ---------------
        // 1) Ask the runtime how big each eye view wants to be.
        const XrViewConfigurationType viewConfigType =
            XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;

        uint32_t viewCount = 0;
        result = xrEnumerateViewConfigurationViews(
            instance, systemId, viewConfigType, 0, &viewCount, nullptr);
        std::fprintf(f, "xrEnumerateViewConfigurationViews(count): %d viewCount=%u\n",
            (int)result, viewCount);
        std::fflush(f);

        std::vector<XrViewConfigurationView> configViews(
            viewCount, { XR_TYPE_VIEW_CONFIGURATION_VIEW });
        if (XR_SUCCEEDED(result) && viewCount > 0)
        {
            result = xrEnumerateViewConfigurationViews(
                instance, systemId, viewConfigType, viewCount, &viewCount,
                configViews.data());
            std::fprintf(f, "xrEnumerateViewConfigurationViews(fill): %d\n",
                (int)result);
            for (uint32_t i = 0; i < viewCount; ++i)
            {
                std::fprintf(f,
                    "  view[%u] recommended=%ux%u max=%ux%u sampleCount=%u\n",
                    i,
                    configViews[i].recommendedImageRectWidth,
                    configViews[i].recommendedImageRectHeight,
                    configViews[i].maxImageRectWidth,
                    configViews[i].maxImageRectHeight,
                    configViews[i].recommendedSwapchainSampleCount);
            }
            std::fflush(f);
        }

        // 2) Pick a color format the runtime supports (prefer sRGB8_alpha8).
        int64_t chosenFormat = 0;
        {
            uint32_t formatCount = 0;
            xrEnumerateSwapchainFormats(session, 0, &formatCount, nullptr);
            std::vector<int64_t> formats(formatCount);
            if (formatCount > 0)
                xrEnumerateSwapchainFormats(session, formatCount, &formatCount,
                    formats.data());

            std::fprintf(f, "xrEnumerateSwapchainFormats: count=%u\n", formatCount);
            for (uint32_t i = 0; i < formatCount; ++i)
                std::fprintf(f, "  format[%u] = 0x%llx\n", i,
                    (unsigned long long)formats[i]);

            const int64_t preferred[] = { GL_SRGB8_ALPHA8, GL_RGBA8 };
            for (int64_t want : preferred)
            {
                for (uint32_t i = 0; i < formatCount && !chosenFormat; ++i)
                    if (formats[i] == want) chosenFormat = want;
                if (chosenFormat) break;
            }
            if (!chosenFormat && formatCount > 0)
                chosenFormat = formats[0];   // fall back to runtime's first choice
            std::fprintf(f, "chosen swapchain format: 0x%llx\n",
                (unsigned long long)chosenFormat);
            std::fflush(f);
        }

        // 3) Create one swapchain per eye view, enumerate its images.
        std::vector<XrSwapchain> swapchains(viewCount, XR_NULL_HANDLE);
        for (uint32_t i = 0; i < viewCount && chosenFormat; ++i)
        {
            XrSwapchainCreateInfo sci{ XR_TYPE_SWAPCHAIN_CREATE_INFO };
            sci.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT |
                             XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
            sci.format = chosenFormat;
            sci.sampleCount = 1;   // 1 for the clear test; MSAA can come later
            sci.width = configViews[i].recommendedImageRectWidth;
            sci.height = configViews[i].recommendedImageRectHeight;
            sci.faceCount = 1;
            sci.arraySize = 1;
            sci.mipCount = 1;

            result = xrCreateSwapchain(session, &sci, &swapchains[i]);
            std::fprintf(f, "xrCreateSwapchain[%u]: %d (%ux%u)\n", i, (int)result,
                sci.width, sci.height);
            std::fflush(f);
            if (!XR_SUCCEEDED(result)) { swapchains[i] = XR_NULL_HANDLE; continue; }

            uint32_t imageCount = 0;
            xrEnumerateSwapchainImages(swapchains[i], 0, &imageCount, nullptr);
            std::vector<XrSwapchainImageOpenGLKHR> images(
                imageCount, { XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR });
            if (imageCount > 0)
                xrEnumerateSwapchainImages(swapchains[i], imageCount, &imageCount,
                    (XrSwapchainImageBaseHeader*)images.data());
            std::fprintf(f, "  swapchain[%u] imageCount=%u first GL tex=%u\n", i,
                imageCount, imageCount ? images[0].image : 0u);
            std::fflush(f);
        }

        // 4) Tear the swapchains down (Step A only verifies creation).
        for (uint32_t i = 0; i < viewCount; ++i)
            if (swapchains[i] != XR_NULL_HANDLE)
                xrDestroySwapchain(swapchains[i]);
        std::fputs("swapchains: destroyed\n", f);
        // --------------------------------------------------------------------

        xrDestroySession(session);
        std::fputs("session: destroyed\n", f);
    }

    xrDestroyInstance(instance);
    std::fputs("smoke: done\n", f);
    std::fclose(f);
}