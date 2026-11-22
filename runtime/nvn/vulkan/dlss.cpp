#include "../../platform.h"  // [XCDE] T275: timestamped diagnostics log (rt_logf)

#ifdef _WIN32  // [XCDE] T-LINUX: the Streamline integration below is Windows-only
#define VK_USE_PLATFORM_WIN32_KHR
// [XCDE] NVIDIA DLSS Super Resolution through Streamline 2.x (third_party/streamline, MIT; sl.interposer.dll,
// sl.common.dll, sl.dlss.dll and nvngx_dlss.dll next to the exe), for temporal.c. Streamline's manual hooking mode:
// the Vulkan API is called directly; before the instance / device are created the DLSS plugin's extensions,
// features and queues are added (device.c), the device is then handed to Streamline (slSetVulkanInfo), and the
// swapchain calls it intercepts go through its proxies (dlss_proxy). Loaded at run time: without the DLLs, without an
// RTX GPU or with another upscaler chosen nothing of this runs (temporal.c falls back to FSR 3).
// Per frame (dlss_evaluate): constants (camera, jitter), DLSS options (quality mode, output size), the tagged inputs
// (colour, depth, motion vectors at render size; output at window size) and the evaluation into the command buffer.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <vulkan/vulkan.h>

#include <sl.h>
#include <sl_consts.h>
#include <sl_dlss.h>
#include <sl_hooks.h>
#include <sl_helpers_vk.h>
#include "../../host/nvidia_signature.h"
#include "gpu_scan.h"
#include "../../dlss_policy.h"

extern "C" {
#include "temporal.h"
#include "../../settings.h"

}
#define DLOG(...) rt_logf(__VA_ARGS__)

static HMODULE sl_mod;
static bool sl_ok, dlss_ok, sl_initialized, sl_surface_ready;
static char dlss_note[256] = "No inicializado; se comprueba al iniciar";
static VkDevice sl_device;
static PFN_vkGetInstanceProcAddr p_proxyInstanceProcAddr;
static PFun_slShutdown *p_slShutdown;
extern "C" const char *dlss_status(void) { return dlss_note; }
static void dlss_note_set(const char *text) { snprintf(dlss_note, sizeof dlss_note, "%s", text); DLOG("[dlss] %s\n", text); }
// [XCDE] GitHub #216: NOT registered with atexit (see the note at the sl_initialized assignment in dlss_init_early).
// atexit handlers run in reverse order of registration, and DLSS initialises after the window handler in device.c, so
// an atexit shutdown ran BEFORE the renderer's quick exit: slShutdown unloaded the Streamline plugins while the render
// thread was still inside dlss_evaluate, whose next Streamline call jumped into the unloaded plugin (the report's
// 0xc0000005 at an address inside no loaded module). The window close path ends the process through quick_exit_handler
// and plat_terminate(), which skips the DLL teardown, so the SDK can stay loaded. Kept for a controlled teardown once
// every renderer thread has been stopped.
static void dlss_shutdown(void) {
    if (sl_initialized && p_slShutdown) { if (sl_device) vkDeviceWaitIdle(sl_device); p_slShutdown(); }
    sl_initialized = sl_ok = dlss_ok = sl_surface_ready = false;
}
static PFun_slInit *p_slInit;
static PFun_slGetFeatureRequirements *p_slGetFeatureRequirements;
static PFun_slSetVulkanInfo *p_slSetVulkanInfo;
static PFun_slIsFeatureSupported *p_slIsFeatureSupported;
static PFun_slGetFeatureFunction *p_slGetFeatureFunction;
static PFun_slGetNewFrameToken *p_slGetNewFrameToken;
static PFun_slSetConstants *p_slSetConstants;
static PFun_slSetTagForFrame *p_slSetTagForFrame;
static PFun_slEvaluateFeature *p_slEvaluateFeature;
static PFun_slDLSSSetOptions *p_slDLSSSetOptions;
static PFN_vkGetDeviceProcAddr p_proxyDeviceProcAddr;
static sl::FeatureRequirements reqs;

static void sl_log(sl::LogType type, const char *msg) {
    if (type != sl::LogType::eInfo) DLOG("[dlss] %s", msg);
}

// Before the Vulkan instance: load Streamline with the DLSS plugin (only when DLSS is the chosen upscaler).
extern "C" bool dlss_init_early(void) {
    if ((!g_settings.upscaler_auto && g_settings.upscaler != UPSCALE_DLSS) || getenv("XCDE_TMP_OFF")) {
        dlss_note_set("DLSS no solicitado (elige Automático o DLSS y reinicia)"); return false;
    }
    uint32_t count = 0; char error[512];
    XcdeGpu *adapters = xcde_gpu_scan(&count, error, sizeof error);
    XcdeGpuInfo *infos = count ? (XcdeGpuInfo *)calloc(count, sizeof *infos) : nullptr;
    if (!adapters || !infos) { free(adapters); free(infos); dlss_note_set("No se pudo comprobar el adaptador Vulkan"); return false; }
    for (uint32_t i = 0; i < count; ++i) infos[i] = adapters[i].info;
    bool fallback;
    int selected = xcde_gpu_choose(infos, count, getenv("LARECOMP_GPU"), g_settings.gpu_uuid, g_settings.gpu_index, &fallback);
    int route = selected >= 0 ? xcde_vulkan_route(infos[selected].vulkan_routes,
        10 + (int)VK_API_VERSION_MINOR(xcde_vk_loader_version()), g_settings.vulkan_api, &fallback) : 0;
    bool nvidia = selected >= 0 && infos[selected].vendor == 0x10de;
    free(adapters); free(infos);
    if (!nvidia) { dlss_note_set("La GPU seleccionada no es NVIDIA RTX; DLSS desactivado"); return false; }
    if (route < 13) { dlss_note_set("DLSS requiere la ruta Vulkan 1.3 o 1.4"); return false; }
    wchar_t dir[MAX_PATH];
    DWORD n = GetModuleFileNameW(NULL, dir, MAX_PATH);
    if (!n || n >= MAX_PATH) { dlss_note_set("Ruta del ejecutable no disponible"); return false; }
    while (n && dir[n - 1] != L'\\' && dir[n - 1] != L'/') n--;
    dir[n] = 0;
    wchar_t path[MAX_PATH + 32];
    swprintf(path, MAX_PATH + 32, L"%lssl.interposer.dll", dir);
    if (!xcde_nvidia_signature(path)) { dlss_note_set("Falta el runtime oficial o su firma NVIDIA no es válida"); return false; }
    sl_mod = LoadLibraryExW(path, NULL, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!sl_mod) { snprintf(dlss_note, sizeof dlss_note, "No se pudo cargar DLSS (Win32 %lu). Comprueba DLL y Visual C++ x64", GetLastError()); DLOG("[dlss] %s\n", dlss_note); return false; }
#define GET(f) p_##f = (PFun_##f *)(void *)GetProcAddress(sl_mod, #f)
    GET(slShutdown); GET(slInit); GET(slGetFeatureRequirements); GET(slSetVulkanInfo); GET(slIsFeatureSupported); GET(slGetFeatureFunction);
    GET(slGetNewFrameToken); GET(slSetConstants); GET(slSetTagForFrame); GET(slEvaluateFeature);
#undef GET
    p_proxyDeviceProcAddr = (PFN_vkGetDeviceProcAddr)(void *)GetProcAddress(sl_mod, "vkGetDeviceProcAddr");
    p_proxyInstanceProcAddr = (PFN_vkGetInstanceProcAddr)(void *)GetProcAddress(sl_mod, "vkGetInstanceProcAddr");
    if (!p_slInit || !p_slShutdown || !p_slGetFeatureRequirements || !p_slSetVulkanInfo || !p_slEvaluateFeature ||
        !p_slIsFeatureSupported || !p_slGetFeatureFunction || !p_slGetNewFrameToken || !p_slSetConstants ||
        !p_slSetTagForFrame || !p_proxyDeviceProcAddr || !p_proxyInstanceProcAddr) {
        dlss_note_set("El runtime Streamline no contiene todas las funciones requeridas");
        return false;
    }
    static const wchar_t *paths[1];
    paths[0] = dir;
    static sl::Feature features[] = {sl::kFeatureDLSS};
    sl::Preferences pref{};
    pref.showConsole = false;
    pref.logLevel = getenv("XCDE_TMP_LOG") ? sl::LogLevel::eVerbose : sl::LogLevel::eDefault;
    pref.logMessageCallback = sl_log;
    pref.pathsToPlugins = paths;
    pref.numPathsToPlugins = 1;
    pref.pathToLogsAndData = nullptr;
    pref.flags = sl::PreferenceFlags::eDisableCLStateTracking | sl::PreferenceFlags::eUseManualHooking |
                 sl::PreferenceFlags::eUseFrameBasedResourceTagging;
    pref.featuresToLoad = features;
    pref.numFeaturesToLoad = 1;
    pref.engine = sl::EngineType::eCustom;
    pref.engineVersion = "xcde";
    pref.projectId = "5b7c1d8e-6a4f-4e1d-9c3a-2f8e7d6b5a41";
    pref.renderAPI = sl::RenderAPI::eVulkan;
    sl::Result r = p_slInit(pref, sl::kSDKVersion);
    if (r != sl::Result::eOk) { snprintf(dlss_note, sizeof dlss_note, "Streamline no pudo iniciarse (SDK %d)", (int)r); DLOG("[dlss] %s\n", dlss_note); return false; }
    sl_initialized = true;  // [XCDE] GitHub #216: no atexit(dlss_shutdown) here (the note at dlss_shutdown has the reason)
    r = p_slGetFeatureRequirements(sl::kFeatureDLSS, reqs);
    if (r != sl::Result::eOk) { snprintf(dlss_note, sizeof dlss_note, "No se pudo cargar el plugin DLSS (SDK %d)", (int)r); DLOG("[dlss] %s\n", dlss_note); return false; }
    sl_ok = true;
    dlss_note_set("Runtime oficial cargado; comprobando GPU/controlador RTX");
    DLOG("[dlss] Streamline loaded: %u instance / %u device extensions, %u + %u extra queues\n", reqs.vkNumInstanceExtensions,
         reqs.vkNumDeviceExtensions, reqs.vkNumGraphicsQueuesRequired, reqs.vkNumComputeQueuesRequired);
    return true;
}

extern "C" int dlss_instance_exts(const char **out, int max) {
    int n = 0;
    for (uint32_t i = 0; sl_ok && i < reqs.vkNumInstanceExtensions && n < max; i++) out[n++] = reqs.vkInstanceExtensions[i];
    return n;
}

// [XCDE] T250: optional DLSS requirements must never make an otherwise usable GPU fail.
extern "C" void dlss_validate_adapter(VkPhysicalDevice phys, uint32_t api, uint32_t qfam) {
    if (!sl_ok) return;
    VkPhysicalDeviceProperties props; vkGetPhysicalDeviceProperties(phys, &props);
    bool supported = api >= VK_API_VERSION_1_3 && props.vendorID == 0x10de;
    uint32_t n = 0;
    VkResult r = vkEnumerateDeviceExtensionProperties(phys, nullptr, &n, nullptr);
    auto *ext = n ? (VkExtensionProperties *)calloc(n, sizeof(VkExtensionProperties)) : nullptr;
    if (r != VK_SUCCESS || !ext || vkEnumerateDeviceExtensionProperties(phys, nullptr, &n, ext) != VK_SUCCESS) supported = false;
    for (uint32_t i = 0; supported && i < reqs.vkNumDeviceExtensions; ++i) {
        bool found = false;
        for (uint32_t j = 0; j < n; ++j) found |= !strcmp(reqs.vkDeviceExtensions[i], ext[j].extensionName);
        if (!found) supported = false;
    }
    free(ext);
    if (supported) {
        VkPhysicalDeviceVulkan13Features h13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
        VkPhysicalDeviceVulkan12Features h12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES}; h12.pNext = &h13;
        VkPhysicalDeviceFeatures2 have{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2}; have.pNext = &h12;
        vkGetPhysicalDeviceFeatures2(phys, &have);
        auto need12 = sl::getVkPhysicalDeviceVulkan12Features(reqs.vkNumFeatures12, reqs.vkFeatures12);
        auto need13 = sl::getVkPhysicalDeviceVulkan13Features(reqs.vkNumFeatures13, reqs.vkFeatures13);
        const VkBool32 *h = &h12.samplerMirrorClampToEdge, *need = &need12.samplerMirrorClampToEdge;
        for (size_t i = 0; i < (offsetof(VkPhysicalDeviceVulkan12Features, subgroupBroadcastDynamicId) + sizeof(VkBool32) - offsetof(VkPhysicalDeviceVulkan12Features, samplerMirrorClampToEdge)) / sizeof(VkBool32); ++i)
            if (need[i] && !h[i]) supported = false;
        h = &h13.robustImageAccess; need = &need13.robustImageAccess;
        for (size_t i = 0; i < (offsetof(VkPhysicalDeviceVulkan13Features, maintenance4) + sizeof(VkBool32) - offsetof(VkPhysicalDeviceVulkan13Features, robustImageAccess)) / sizeof(VkBool32); ++i)
            if (need[i] && !h[i]) supported = false;
        uint32_t nq = 0; vkGetPhysicalDeviceQueueFamilyProperties(phys, &nq, nullptr);
        auto *queues = nq ? (VkQueueFamilyProperties *)calloc(nq, sizeof(VkQueueFamilyProperties)) : nullptr;
        if (queues) vkGetPhysicalDeviceQueueFamilyProperties(phys, &nq, queues);
        uint64_t required = 1ull + reqs.vkNumGraphicsQueuesRequired + reqs.vkNumComputeQueuesRequired;
        if (!queues || qfam >= nq || required > queues[qfam].queueCount || required > 16) supported = false;
        free(queues);
    }
    if (!supported) { sl_ok = dlss_ok = false; dlss_note_set("GPU/controlador sin los requisitos DLSS de Vulkan; se usa escalado alternativo"); }
}

// The device's extensions (appended to ext[*n]) and features (OR-ed into f12 / f13); extra queues it asks for.
extern "C" void dlss_device_requirements(VkInstance inst, VkPhysicalDevice phys, const char **ext, uint32_t *n, uint32_t max,
                                         VkPhysicalDeviceVulkan12Features *f12, VkPhysicalDeviceVulkan13Features *f13,
                                         uint32_t *extra_gfx, uint32_t *extra_compute) {
    (void)inst; (void)phys;
    *extra_gfx = *extra_compute = 0;
    if (!sl_ok) return;
    uint32_t additional = 0;
    for (uint32_t i = 0; i < reqs.vkNumDeviceExtensions; ++i) {
        bool present = false;
        for (uint32_t j = 0; j < *n; ++j) present |= !strcmp(ext[j], reqs.vkDeviceExtensions[i]);
        if (!present) ++additional;
    }
    if (*n > max || additional > max - *n) { sl_ok = false; dlss_note_set("Demasiadas extensiones opcionales DLSS; se usa escalado alternativo"); return; }
    for (uint32_t i = 0; i < reqs.vkNumDeviceExtensions && *n < max; i++) {
        bool dup = false;
        for (uint32_t j = 0; j < *n; j++) dup |= !strcmp(ext[j], reqs.vkDeviceExtensions[i]);
        if (!dup) ext[(*n)++] = reqs.vkDeviceExtensions[i];
    }
    VkPhysicalDeviceVulkan12Features a = sl::getVkPhysicalDeviceVulkan12Features(reqs.vkNumFeatures12, reqs.vkFeatures12);
    VkPhysicalDeviceVulkan13Features b = sl::getVkPhysicalDeviceVulkan13Features(reqs.vkNumFeatures13, reqs.vkFeatures13);
    // (OR the VkBool32 fields: everything between pNext and the end of each structure)
    VkBool32 *d = &f12->samplerMirrorClampToEdge, *s = &a.samplerMirrorClampToEdge;
    for (size_t i = 0; i < (offsetof(VkPhysicalDeviceVulkan12Features, subgroupBroadcastDynamicId) + sizeof(VkBool32) - offsetof(VkPhysicalDeviceVulkan12Features, samplerMirrorClampToEdge)) / sizeof(VkBool32); i++) d[i] |= s[i];
    d = &f13->robustImageAccess; s = &b.robustImageAccess;
    for (size_t i = 0; i < (offsetof(VkPhysicalDeviceVulkan13Features, maintenance4) + sizeof(VkBool32) - offsetof(VkPhysicalDeviceVulkan13Features, robustImageAccess)) / sizeof(VkBool32); i++) d[i] |= s[i];
    *extra_gfx = reqs.vkNumGraphicsQueuesRequired;
    *extra_compute = reqs.vkNumComputeQueuesRequired;
}

// After vkCreateDevice: the device for Streamline (its queues start at index 1 of the host's family).
extern "C" void dlss_device_created(VkInstance inst, VkPhysicalDevice phys, VkDevice dev, uint32_t qfam) {
    if (!sl_ok) return;
    sl::VulkanInfo info{};
    info.device = dev;
    info.instance = inst;
    info.physicalDevice = phys;
    info.graphicsQueueFamily = qfam;
    uint32_t graphics_index, compute_index;
    xcde_dlss_queue_indices(reqs.vkNumGraphicsQueuesRequired, reqs.vkNumComputeQueuesRequired, &graphics_index, &compute_index);
    info.graphicsQueueIndex = graphics_index;
    info.computeQueueFamily = qfam;
    info.computeQueueIndex = compute_index;
    sl_device = dev;
    sl::Result r = p_slSetVulkanInfo(info);
    if (r != sl::Result::eOk) { snprintf(dlss_note, sizeof dlss_note, "No se pudo preparar DLSS en Vulkan (SDK %d)", (int)r); DLOG("[dlss] %s\n", dlss_note); sl_ok = false; return; }
    sl::AdapterInfo ai{};
    VkPhysicalDevice pd = phys;
    ai.vkPhysicalDevice = pd;
    r = p_slIsFeatureSupported(sl::kFeatureDLSS, ai);
    if (r != sl::Result::eOk) { sl_ok = false; snprintf(dlss_note, sizeof dlss_note, "DLSS no compatible con esta GPU/controlador (SDK %d)", (int)r); DLOG("[dlss] %s\n", dlss_note); return; }
    void *f = nullptr;
    if (p_slGetFeatureFunction(sl::kFeatureDLSS, "slDLSSSetOptions", f) != sl::Result::eOk || !f) {
        DLOG("[dlss] slDLSSSetOptions missing\n");
        return;
    }
    p_slDLSSSetOptions = (PFun_slDLSSSetOptions *)f;
    dlss_ok = true;
}

// T263: Streamline's surface wrapper uses the instance dispatch set up by slSetVulkanInfo.
extern "C" bool dlss_create_surface(void *sdl_window, VkInstance instance, VkPhysicalDevice phys,
                                    uint32_t qfam, VkSurfaceKHR *surface) {
    if (!dlss_ok || !p_proxyInstanceProcAddr) return false;
    auto create = (PFN_vkCreateWin32SurfaceKHR)p_proxyInstanceProcAddr(instance, "vkCreateWin32SurfaceKHR");
    auto destroy = (PFN_vkDestroySurfaceKHR)p_proxyInstanceProcAddr(instance, "vkDestroySurfaceKHR");
    HWND hwnd = (HWND)SDL_GetPointerProperty(SDL_GetWindowProperties((SDL_Window *)sdl_window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
    if (!create || !destroy || !hwnd) {
        dlss_note_set("Falta el hook o la ventana para preparar la superficie DLSS");
        return false;
    }
    VkWin32SurfaceCreateInfoKHR info{VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR};
    info.hinstance = (HINSTANCE)GetWindowLongPtrW(hwnd, GWLP_HINSTANCE);
    if (!info.hinstance) info.hinstance = GetModuleHandleW(nullptr);
    info.hwnd = hwnd;
    VkSurfaceKHR hooked = VK_NULL_HANDLE;
    VkResult r = create(instance, &info, nullptr, &hooked);
    if (r != VK_SUCCESS || !hooked) {
        snprintf(dlss_note, sizeof dlss_note, "Falló la superficie Streamline (Vulkan %d)", (int)r);
        DLOG("[dlss] %s\n", dlss_note);
        return false;
    }
    VkBool32 present = VK_FALSE;
    r = vkGetPhysicalDeviceSurfaceSupportKHR(phys, qfam, hooked, &present);
    if (r != VK_SUCCESS || !present) {
        destroy(instance, hooked, nullptr);
        snprintf(dlss_note, sizeof dlss_note, "La cola no puede presentar la superficie Streamline (Vulkan %d)", (int)r);
        DLOG("[dlss] %s\n", dlss_note);
        return false;
    }
    *surface = hooked;
    sl_surface_ready = true;
    dlss_note_set("DLSS Super Resolution disponible en la GPU activa");
    return true;
}

// Streamline's proxies for the swapchain calls it intercepts (NULL: call Vulkan directly).
extern "C" PFN_vkVoidFunction dlss_proxy(VkDevice dev, const char *name) {
    if (!sl_surface_ready || !p_proxyDeviceProcAddr) return nullptr;
    return p_proxyDeviceProcAddr(dev, name);
}

extern "C" bool dlss_available(void) { return dlss_ok && sl_surface_ready; }

static sl::Resource tex(const TexBackend *b, VkImageUsageFlags usage) {
    static const VkComponentMapping id = {};
    VkImageView v = vk_image_view((TexBackend *)b, VK_IMAGE_VIEW_TYPE_2D, b->format, 0, 1, 0, 1, id, VK_IMAGE_ASPECT_COLOR_BIT);
    sl::Resource r(sl::ResourceType::eTex2d, (void *)b->image, nullptr, (void *)v, (uint32_t)VK_IMAGE_LAYOUT_GENERAL);
    r.width = b->width;
    r.height = b->height;
    r.nativeFormat = (uint32_t)b->format;
    r.mipLevels = 1;
    r.arrayLayers = 1;
    r.flags = 0;
    r.usage = usage;
    return r;
}

static void rows_to_sl(const double m[16], sl::float4x4 &o) {  // column-vector matrix -> Streamline's row-vector one
    for (int r = 0; r < 4; r++) o.row[r] = sl::float4((float)m[0 * 4 + r], (float)m[1 * 4 + r], (float)m[2 * 4 + r], (float)m[3 * 4 + r]);
}
static bool inv4(const double m[16], double o[16]) {
    double a[4][8];
    for (int i = 0; i < 4; i++) for (int j = 0; j < 8; j++) a[i][j] = j < 4 ? m[i * 4 + j] : (j - 4 == i);
    for (int c = 0; c < 4; c++) {
        int p = c;
        for (int r = c + 1; r < 4; r++) if (fabs(a[r][c]) > fabs(a[p][c])) p = r;
        if (fabs(a[p][c]) < 1e-12) return false;
        if (p != c) for (int j = 0; j < 8; j++) { double t = a[c][j]; a[c][j] = a[p][j]; a[p][j] = t; }
        double d = a[c][c];
        for (int j = 0; j < 8; j++) a[c][j] /= d;
        for (int r = 0; r < 4; r++) if (r != c) { double f = a[r][c]; for (int j = 0; j < 8; j++) a[r][j] -= f * a[c][j]; }
    }
    for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) o[i * 4 + j] = a[i][j + 4];
    return true;
}

// reproj: the previous clip position from this frame's (column vectors), as temporal.c computed it.
extern "C" bool dlss_evaluate(VkCommandBuffer cb, const TexBackend *color, const TexBackend *mv, const TexBackend *depth,
                              const TexBackend *out, const TexBackend *hud, const float jitter[2], bool reset, const TmpCamera *cam,
                              const double reproj[16]) {
    if (!dlss_ok) return false;
    static uint32_t frame;
    sl::FrameToken *token = nullptr;
    uint32_t fi = frame++;
    if (p_slGetNewFrameToken(token, &fi) != sl::Result::eOk || !token) return false;
    sl::ViewportHandle vp(0);
    sl::Constants c{};
    double proj[16], iproj[16];
    for (int i = 0; i < 16; i++) proj[i] = cam->proj[i];
    if (!inv4(proj, iproj)) return false;
    rows_to_sl(proj, c.cameraViewToClip);
    rows_to_sl(iproj, c.clipToCameraView);
    double idm[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}, back[16];
    rows_to_sl(idm, c.clipToLensClip);
    rows_to_sl(reproj, c.clipToPrevClip);
    if (!inv4(reproj, back)) memcpy(back, idm, sizeof back);
    rows_to_sl(back, c.prevClipToClip);
    c.jitterOffset = sl::float2(jitter[0], jitter[1]);
    c.mvecScale = sl::float2(1.0f, 1.0f);  // (motion vectors in UV units)
    c.cameraPinholeOffset = sl::float2(0, 0);
    c.cameraPos = sl::float3(0, 0, 0);
    c.cameraUp = sl::float3(cam->view[4], cam->view[5], cam->view[6]);
    c.cameraRight = sl::float3(cam->view[0], cam->view[1], cam->view[2]);
    c.cameraFwd = sl::float3(-cam->view[8], -cam->view[9], -cam->view[10]);
    c.cameraNear = fmaxf(fabsf(cam->proj[11]) * 0.5f, 0.001f);
    c.cameraFar = 100000.0f;
    c.cameraFOV = cam->proj[5] != 0 ? 2.0f * atanf(1.0f / fabsf(cam->proj[5])) : 0.785f;
    c.cameraAspectRatio = cam->proj[0] != 0 ? fabsf(cam->proj[5] / cam->proj[0]) : 1.778f;
    c.motionVectorsInvalidValue = sl::INVALID_FLOAT; // zero is a valid stationary motion vector
    c.motionVectorsJittered = sl::Boolean::eFalse;
    c.depthInverted = sl::Boolean::eFalse;
    c.cameraMotionIncluded = sl::Boolean::eTrue;
    c.motionVectors3D = sl::Boolean::eFalse;
    c.reset = reset ? sl::Boolean::eTrue : sl::Boolean::eFalse;
    if (p_slSetConstants(c, *token, vp) != sl::Result::eOk) return false;
    sl::DLSSOptions o{};
    static const sl::DLSSMode modes[] = {sl::DLSSMode::eDLAA, sl::DLSSMode::eMaxQuality, sl::DLSSMode::eBalanced,
                                         sl::DLSSMode::eMaxPerformance, sl::DLSSMode::eUltraPerformance};
    int q = g_settings.upscale_quality;
    o.mode = modes[q >= 0 && q <= 4 ? q : 1];
    o.outputWidth = out->width;
    o.outputHeight = out->height;
    o.useAutoExposure = sl::Boolean::eTrue;
    o.colorBuffersHDR = sl::Boolean::eFalse;  // (the game image: gamma-encoded LDR)
    if (p_slDLSSSetOptions(vp, o) != sl::Result::eOk) return false;
    sl::Resource rc = tex(color, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
    sl::Resource rd = tex(depth, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
    sl::Resource rm = tex(mv, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
    sl::Resource ro = tex(out, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
    sl::Extent ein{0, 0, color->width, color->height}, eout{0, 0, out->width, out->height};
    sl::ResourceTag tags[5] = {sl::ResourceTag(&rc, sl::kBufferTypeScalingInputColor, sl::ResourceLifecycle::eOnlyValidNow, &ein),
                               sl::ResourceTag(&rd, sl::kBufferTypeDepth, sl::ResourceLifecycle::eOnlyValidNow, &ein),
                               sl::ResourceTag(&rm, sl::kBufferTypeMotionVectors, sl::ResourceLifecycle::eOnlyValidNow, &ein),
                               sl::ResourceTag(&ro, sl::kBufferTypeScalingOutputColor, sl::ResourceLifecycle::eOnlyValidNow, &eout),
                               sl::ResourceTag()};
    uint32_t ntags = 4;
    sl::Resource rh;
    if (hud) {  // [XCDE] T213: the HUD's pixels (temporal.c tmp_hud_snapshot): DLSS takes the current colour there (no trails)
        rh = tex(hud, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
        tags[ntags++] = sl::ResourceTag(&rh, sl::kBufferTypeBiasCurrentColorHint, sl::ResourceLifecycle::eOnlyValidNow, &ein);
    }
    if (p_slSetTagForFrame(*token, vp, tags, ntags, (sl::CommandBuffer *)cb) != sl::Result::eOk) { vk_external_state_changed(); return false; }
    const sl::BaseStructure *inputs[] = {&vp};
    sl::Result r = p_slEvaluateFeature(sl::kFeatureDLSS, *token, inputs, 1, (sl::CommandBuffer *)cb);
    vk_external_state_changed(); // SDK may change pipeline, descriptors and dynamic state
    if (r != sl::Result::eOk) { snprintf(dlss_note, sizeof dlss_note, "DLSS dispatch falló (SDK %d); frame con escalado alternativo", (int)r); static int n; if (n++ < 5) DLOG("[dlss] %s\n", dlss_note); return false; }
    return true;
}
extern "C" void dlss_reset(void) {}
#elif defined(__linux__)
// [XCDE] LINUX-017: native Linux NVIDIA NGX DLSS SR/DLAA via optional libmonado_dlss.so (dlopen).
#include <SDL3/SDL_filesystem.h>
#include <dlfcn.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vulkan/vulkan.h>

#include "dlss_linux.h"
#include "gpu_scan.h"

extern "C" {
#include "temporal.h"
#include "../../settings.h"
#include "../../paths.h"
}

static void *dlss_mod;
static bool ngx_prepared, ngx_ok;
static char dlss_note[256] = "Not initialized; checked at launch";
static VkDevice ngx_device;

static decltype(&ngx_linux_init) p_ngx_init;
static decltype(&ngx_linux_instance_exts) p_ngx_instance_exts;
static decltype(&ngx_linux_device_exts) p_ngx_device_exts;
static decltype(&ngx_linux_device_created) p_ngx_device_created;
static decltype(&ngx_linux_available) p_ngx_available;
static decltype(&ngx_linux_evaluate) p_ngx_evaluate;
static decltype(&ngx_linux_reset) p_ngx_reset;
static decltype(&ngx_linux_shutdown) p_ngx_shutdown;

extern "C" const char *dlss_status(void) { return dlss_note; }
static void dlss_note_set(const char *text) { snprintf(dlss_note, sizeof dlss_note, "%s", text); rt_logf("[dlss] %s\n", text); }

// [XCDE] GitHub #216: NOT registered with atexit, for the reason next to the Windows dlss_shutdown: NGX would be
// released and the component dlclosed while a render thread could still be inside an evaluation. The exit path ends the
// process through quick_exit_handler and plat_terminate() instead, so the component can stay loaded.
static void dlss_shutdown_linux(void) {
    if (p_ngx_shutdown) {
        if (ngx_device) vkDeviceWaitIdle(ngx_device);
        p_ngx_shutdown();
    }
    ngx_prepared = ngx_ok = false;
    ngx_device = VK_NULL_HANDLE;
    if (dlss_mod) {
        dlclose(dlss_mod);
        dlss_mod = nullptr;
    }
    p_ngx_init = nullptr; p_ngx_instance_exts = nullptr; p_ngx_device_exts = nullptr;
    p_ngx_device_created = nullptr; p_ngx_available = nullptr; p_ngx_evaluate = nullptr;
    p_ngx_reset = nullptr; p_ngx_shutdown = nullptr;
}

static bool file_exists_regular(const char *p) {
    struct stat st{};
    return p && *p && stat(p, &st) == 0 && S_ISREG(st.st_mode);
}

static bool is_ascii_path(const char *s) {
    if (!s || !*s) return false;
    for (const unsigned char *p = (const unsigned char *)s; *p; ++p)
        if (*p >= 0x80) return false;
    return true;
}

static bool exe_dir(char *out, size_t cap) {
    ssize_t n = readlink("/proc/self/exe", out, cap - 1);
    if (n <= 0 || (size_t)n >= cap) return false;
    out[n] = 0;
    char *slash = strrchr(out, '/');
    if (!slash) return false;
    *slash = 0;
    return true;
}

static bool load_dlss_module(void) {
    if (dlss_mod) return true;
    const char *override = getenv("MONADO_DLSS_LIBRARY");
    if (override && *override) {
        dlss_mod = dlopen(override, RTLD_NOW | RTLD_LOCAL);
    } else {
        char dir[4096], cand[4096];
        if (exe_dir(dir, sizeof dir)) {
            if (snprintf(cand, sizeof cand, "%s/libmonado_dlss.so", dir) < (int)sizeof cand && file_exists_regular(cand))
                dlss_mod = dlopen(cand, RTLD_NOW | RTLD_LOCAL);
            if (!dlss_mod && snprintf(cand, sizeof cand, "%s/../lib/libmonado_dlss.so", dir) < (int)sizeof cand && file_exists_regular(cand))
                dlss_mod = dlopen(cand, RTLD_NOW | RTLD_LOCAL);
        }
        if (!dlss_mod) dlss_mod = dlopen("libmonado_dlss.so", RTLD_NOW | RTLD_LOCAL);
    }
    if (!dlss_mod) {
        rt_logf("[dlss] libmonado_dlss.so unavailable: %s\n", dlerror());
        return false;
    }
#define GET(f, sym) f = (decltype(f))dlsym(dlss_mod, sym)
    GET(p_ngx_init, "ngx_linux_init");
    GET(p_ngx_instance_exts, "ngx_linux_instance_exts");
    GET(p_ngx_device_exts, "ngx_linux_device_exts");
    GET(p_ngx_device_created, "ngx_linux_device_created");
    GET(p_ngx_available, "ngx_linux_available");
    GET(p_ngx_evaluate, "ngx_linux_evaluate");
    GET(p_ngx_reset, "ngx_linux_reset");
    GET(p_ngx_shutdown, "ngx_linux_shutdown");
#undef GET
    if (!p_ngx_init || !p_ngx_instance_exts || !p_ngx_device_exts || !p_ngx_device_created ||
        !p_ngx_available || !p_ngx_evaluate || !p_ngx_reset || !p_ngx_shutdown) {
        dlclose(dlss_mod);
        dlss_mod = nullptr;
        return false;
    }
    return true;
}

static bool find_feature_dir(char *out, size_t cap) {
    static const char *const k_lib = "libnvidia-ngx-dlss.so.310.9.1";
    const char *env_dir = getenv("MONADO_DLSS_DIR");
    char probe[4096], dir[4096];
    if (env_dir && *env_dir && snprintf(probe, sizeof probe, "%s/%s", env_dir, k_lib) < (int)sizeof probe && file_exists_regular(probe)) {
        snprintf(out, cap, "%s", env_dir);
        return true;
    }
    const char *root = rt_data_root();
    if (root && *root && snprintf(probe, sizeof probe, "%s/dlss/%s", root, k_lib) < (int)sizeof probe && file_exists_regular(probe)) {
        snprintf(out, cap, "%s/dlss", root);
        return true;
    }
    if (exe_dir(dir, sizeof dir)) {
        const char *rels[] = {"%s/%s", "%s/../share/monado/dlss/%s", "%s/../lib/%s"};
        const char *dirs[] = {"%s", "%s/../share/monado/dlss", "%s/../lib"};
        for (int i = 0; i < 3; i++) {
            if (snprintf(probe, sizeof probe, rels[i], dir, k_lib) < (int)sizeof probe && file_exists_regular(probe)) {
                snprintf(out, cap, dirs[i], dir);
                return true;
            }
        }
    }
    if (snprintf(probe, sizeof probe, "build/linux/%s", k_lib) < (int)sizeof probe && file_exists_regular(probe)) {
        snprintf(out, cap, "build/linux");
        return true;
    }
    return false;
}

extern "C" bool dlss_init_early(void) {
    if ((!g_settings.upscaler_auto && g_settings.upscaler != UPSCALE_DLSS) || getenv("XCDE_TMP_OFF") || getenv("XCDE_PRECOMPILING")) {
        dlss_note_set(g_settings.ui_lang ? "DLSS not requested (choose Automatic or DLSS and restart)"
                                         : "DLSS no solicitado (elige Automático o DLSS y reinicia)");
        return false;
    }
    uint32_t count = 0; char error[512];
    XcdeGpu *adapters = xcde_gpu_scan(&count, error, sizeof error);
    XcdeGpuInfo *infos = count ? (XcdeGpuInfo *)calloc(count, sizeof *infos) : nullptr;
    if (!adapters || !infos) {
        free(adapters); free(infos);
        dlss_note_set(g_settings.ui_lang ? "Could not probe Vulkan adapter" : "No se pudo comprobar el adaptador Vulkan");
        return false;
    }
    for (uint32_t i = 0; i < count; ++i) infos[i] = adapters[i].info;
    bool fallback;
    int selected = xcde_gpu_choose(infos, count, getenv("LARECOMP_GPU"), g_settings.gpu_uuid, g_settings.gpu_index, &fallback);
    int route = selected >= 0 ? xcde_vulkan_route(infos[selected].vulkan_routes,
        10 + (int)VK_API_VERSION_MINOR(xcde_vk_loader_version()), g_settings.vulkan_api, &fallback) : 0;
    bool nvidia = selected >= 0 && infos[selected].vendor == 0x10de;
    free(adapters); free(infos);
    if (!nvidia) {
        dlss_note_set(g_settings.ui_lang ? "Selected GPU is not NVIDIA RTX; DLSS disabled"
                                         : "La GPU seleccionada no es NVIDIA RTX; DLSS desactivado");
        return false;
    }
    if (route < 13) {
        dlss_note_set(g_settings.ui_lang ? "DLSS requires Vulkan 1.3 or 1.4 route"
                                         : "DLSS requiere la ruta Vulkan 1.3 o 1.4");
        return false;
    }
    char feature_dir[4096];
    if (!find_feature_dir(feature_dir, sizeof feature_dir) || !load_dlss_module()) {
        dlss_note_set(g_settings.ui_lang ? "Linux NVIDIA NGX DLSS runtime not installed"
                                         : "Falta el runtime NVIDIA NGX DLSS para Linux");
        return false;
    }
    char cache[4096];
    const char *root = rt_data_root();
    if (is_ascii_path(root) && snprintf(cache, sizeof cache, "%s/cache/dlss", root) < (int)sizeof cache && SDL_CreateDirectory(cache)) {
        // Use private ASCII data root cache/dlss when possible.
    } else {
        snprintf(cache, sizeof cache, "/tmp/monado-dlss-%u", (unsigned)getuid());
        if (mkdir(cache, 0700) != 0 && errno != EEXIST) {
            dlss_note_set("Failed to create NGX cache directory");
            return false;
        }
    }
    if (!p_ngx_init(feature_dir, cache)) {
        dlss_note_set(g_settings.ui_lang ? "NVIDIA NGX initialization failed"
                                         : "No se pudo iniciar NVIDIA NGX");
        return false;
    }
    ngx_prepared = true;  // [XCDE] GitHub #216: no atexit(dlss_shutdown_linux) here (the note at the function has the reason)
    dlss_note_set(g_settings.ui_lang ? "NVIDIA NGX runtime loaded; checking RTX GPU/driver"
                                     : "Runtime NVIDIA NGX cargado; comprobando GPU/controlador RTX");
    return true;
}

extern "C" int dlss_instance_exts(const char **out, int max) {
    return ngx_prepared && p_ngx_instance_exts ? p_ngx_instance_exts(out, max) : 0;
}

extern "C" void dlss_validate_adapter(VkPhysicalDevice phys, uint32_t api, uint32_t qfam) {
    (void)qfam;
    if (!ngx_prepared) return;
    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(phys, &props);
    if (api < VK_API_VERSION_1_3 || props.vendorID != 0x10de) {
        ngx_prepared = ngx_ok = false;
        if (p_ngx_shutdown) p_ngx_shutdown();
        dlss_note_set(g_settings.ui_lang ? "GPU/driver lacks Vulkan 1.3+ NVIDIA RTX support for DLSS"
                                         : "GPU/controlador sin los requisitos DLSS de Vulkan; se usa escalado alternativo");
    }
}

extern "C" void dlss_device_requirements(VkInstance inst, VkPhysicalDevice phys, const char **ext, uint32_t *n, uint32_t max,
                                         VkPhysicalDeviceVulkan12Features *f12, VkPhysicalDeviceVulkan13Features *f13,
                                         uint32_t *extra_gfx, uint32_t *extra_compute) {
    (void)f13;
    *extra_gfx = *extra_compute = 0;
    if (!ngx_prepared || !p_ngx_device_exts) return;
    const uint32_t original_count = n ? *n : 0;
    if (!p_ngx_device_exts(inst, phys, ext, n, max)) {
        ngx_prepared = false;
        if (p_ngx_shutdown) p_ngx_shutdown();
        dlss_note_set(g_settings.ui_lang ? "DLSS device requirements unsupported on this GPU/driver"
                                         : "Requisitos de dispositivo DLSS no compatibles en esta GPU/controlador");
        return;
    }
    bool needs_address = false;
    for (uint32_t i = 0; i < *n; i++) needs_address |= !strcmp(ext[i], VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME);
    if (!needs_address) return;
    VkPhysicalDeviceVulkan12Features supported{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    VkPhysicalDeviceFeatures2 have{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    have.pNext = &supported;
    if (f12) vkGetPhysicalDeviceFeatures2(phys, &have);
    if (!f12 || !supported.bufferDeviceAddress) {
        *n = original_count;
        ngx_prepared = false;
        if (p_ngx_shutdown) p_ngx_shutdown();
        dlss_note_set(g_settings.ui_lang ? "Required bufferDeviceAddress unavailable; using fallback"
                                         : "Falta bufferDeviceAddress requerido para DLSS; se usa escalado alternativo");
        return;
    }
    f12->bufferDeviceAddress = VK_TRUE;
}

extern "C" void dlss_device_created(VkInstance inst, VkPhysicalDevice phys, VkDevice dev, uint32_t qfam) {
    if (!ngx_prepared || !p_ngx_device_created || !p_ngx_available) return;
    ngx_device = dev;
    p_ngx_device_created(inst, phys, dev, qfam);
    ngx_ok = p_ngx_available();
    if (ngx_ok) {
        dlss_note_set(g_settings.ui_lang ? "DLSS Super Resolution / DLAA available on active GPU"
                                         : "DLSS Super Resolution / DLAA disponible en la GPU activa");
    } else {
        dlss_note_set(g_settings.ui_lang ? "DLSS unavailable on this GPU or driver update required"
                                         : "DLSS no compatible con esta GPU o requiere actualizar controlador");
    }
}

extern "C" bool dlss_create_surface(void *sdl_window, VkInstance instance, VkPhysicalDevice phys,
                                    uint32_t qfam, VkSurfaceKHR *surface) {
    (void)sdl_window; (void)instance; (void)phys; (void)qfam; (void)surface;
    return false;
}

extern "C" PFN_vkVoidFunction dlss_proxy(VkDevice dev, const char *name) {
    (void)dev; (void)name;
    return nullptr;
}

extern "C" bool dlss_available(void) {
    return ngx_ok && p_ngx_available && p_ngx_available();
}

static DlssImage ngx_image(const TexBackend *b) {
    static const VkComponentMapping id = {};
    VkImageView v = vk_image_view((TexBackend *)b, VK_IMAGE_VIEW_TYPE_2D, b->format, 0, 1, 0, 1, id, VK_IMAGE_ASPECT_COLOR_BIT);
    return {b->image, v, b->format, b->width, b->height};
}

extern "C" bool dlss_evaluate(VkCommandBuffer cb, const TexBackend *color, const TexBackend *mv, const TexBackend *depth,
                              const TexBackend *out, const TexBackend *hud, const float jitter[2], bool reset, const TmpCamera *cam,
                              const double reproj[16]) {
    (void)cam; (void)reproj;
    if (!ngx_ok || !p_ngx_evaluate || !color || !mv || !depth || !out || !jitter) return false;
    DlssFrame f{};
    f.color = ngx_image(color);
    f.motion = ngx_image(mv);
    f.depth = ngx_image(depth);
    f.output = ngx_image(out);
    f.jitter[0] = jitter[0];
    f.jitter[1] = jitter[1];
    f.reset = reset;
    f.quality = g_settings.upscale_quality;
    f.recording_seq = vk_pending_seq();
    f.completed_seq = vk_completed_seq();
    if (hud) f.reactive = ngx_image(hud);
    bool ok = p_ngx_evaluate(cb, &f);
    vk_external_state_changed();
    return ok;
}

extern "C" void dlss_reset(void) {
    if (p_ngx_reset) p_ngx_reset();
}
#else
// Fallback for unsupported platforms: DLSS is unavailable and adds no extensions, queues, or features.
#include <stdio.h>
#include <vulkan/vulkan.h>
extern "C" {
#include "temporal.h"
#include "../../settings.h"
const char *dlss_status(void) { return "DLSS is not available on this platform"; }
bool dlss_init_early(void) { return false; }
int dlss_instance_exts(const char **out, int max) { (void)out; (void)max; return 0; }
void dlss_validate_adapter(VkPhysicalDevice phys, uint32_t api, uint32_t qfam) { (void)phys; (void)api; (void)qfam; }
void dlss_device_requirements(VkInstance inst, VkPhysicalDevice phys, const char **ext, uint32_t *n, uint32_t max,
                              VkPhysicalDeviceVulkan12Features *f12, VkPhysicalDeviceVulkan13Features *f13,
                              uint32_t *extra_gfx, uint32_t *extra_compute) {
    (void)inst; (void)phys; (void)ext; (void)n; (void)max; (void)f12; (void)f13;
    *extra_gfx = *extra_compute = 0;
}
void dlss_device_created(VkInstance inst, VkPhysicalDevice phys, VkDevice dev, uint32_t qfam) { (void)inst; (void)phys; (void)dev; (void)qfam; }
bool dlss_create_surface(void *sdl_window, VkInstance instance, VkPhysicalDevice phys, uint32_t qfam, VkSurfaceKHR *surface) {
    (void)sdl_window; (void)instance; (void)phys; (void)qfam; (void)surface;
    return false;
}
PFN_vkVoidFunction dlss_proxy(VkDevice dev, const char *name) { (void)dev; (void)name; return NULL; }
bool dlss_available(void) { return false; }
bool dlss_evaluate(VkCommandBuffer cb, const TexBackend *color, const TexBackend *mv, const TexBackend *depth,
                   const TexBackend *out, const TexBackend *hud, const float jitter[2], bool reset, const TmpCamera *cam,
                   const double reproj[16]) {
    (void)cb; (void)color; (void)mv; (void)depth; (void)out; (void)hud; (void)jitter; (void)reset; (void)cam; (void)reproj;
    return false;
}
void dlss_reset(void) {}
}
#endif  // _WIN32 / __linux__
