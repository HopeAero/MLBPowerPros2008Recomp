// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of
// the MPL was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
// Adapted from SwitchWakerHD (https://github.com/centollOS/SwitchWakerHD) at df8fbde:
// runtime/src/gfx/deko/backend.cpp (device, queue, swapchain, present, frame fences, debug callback,
// queue checks, the set-up cost and the statistics), by way of SwitchWaker (switch/deko/device.cpp at
// 56c0758).
// Modified for MLB Power Pros 2008 Recomp (tools/switch/deko, 2026-10-26): no test pattern, no
// SwitchWaker hooks (JKRHeap host scope, cos_switch_*); the settings are WIICOMPILED_DK_* nxlink
// arguments; the window is 1280x720 in both modes by default (as the Vulkan path's); the present takes
// Aurora's pacing callback between the submit and dkQueuePresentImage; the renderer's own shaders are
// compiled at run time (fixed_shaders.cpp) instead of embedded DKSH.
// The deko3d device, the frame's command recording and the present pass (dk.h). The command buffer
// owner opens a frame at the frame's first batch (frame_open: the slot's fence, the descriptor sets,
// ImGui's texture uploads), records the frame's passes into it (aurora/), submits along the way, and
// presents: the swapchain image cleared, the game's picture (the picture callback), then ImGui, the
// fence, the submit and the present.
#include <switch.h>
#include <unistd.h>

#include "dk.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

extern "C" char* fake_heap_end;                                   // libnx: the end of the heap malloc grows into
extern "C" void SwitchPinCurrentThread(int core, int priority);  // runtime/src/platform/switch/switch_threads.cpp

namespace swdk {

Renderer R;
Conventions C;

namespace {

constexpr uint32_t kSwapImages = 3;
#ifdef MLB_DK_DEBUG_LIB
constexpr bool kDebugLib = true;  // linked against libdeko3dd
#else
constexpr bool kDebugLib = false;
#endif

DkSwapchain g_swapchain = nullptr;
DkImage g_swapImages[kSwapImages];
ImageAlloc g_swapMem[kSwapImages];
DkImage g_depth;  // the present pass's depth buffer (cleared; ImGui does not test depth)
ImageAlloc g_depthMem;
int g_debugMessages = 0;

// the heap malloc has never grown into, in MiB (libnx's heap end against malloc's break): deko3d's
// memory blocks come from it, so the difference across the set-up is its cost
size_t heap_never_used_mib() {
    char* top = static_cast<char*>(sbrk(0));
    if (!fake_heap_end || !top || top == reinterpret_cast<char*>(-1)) return 0;
    return size_t(fake_heap_end - top) >> 20;
}

// deko3d's messages. Only the debug library (libdeko3dd) calls this: its checks of every call,
// warnings (result DkResult_Success) and errors, after which it traps. The release library calls
// nothing on an error: it aborts with result 2359-xxxx.
void debug_message(void*, const char* context, DkResult result, const char* message) {
    if (result != DkResult_Success) {
        fatal("deko3d error in %s: result %d: %s (frame %llu)", context ? context : "?", int(result),
              message ? message : "", (unsigned long long)R.frame + 1);
    }
    const int n = g_debugMessages++;
    if (n < 200) {
        dklog("deko3d %s: %s", context ? context : "?", message ? message : "");
    } else if (n == 200) {
        dklog("deko3d: more messages, not logged");
    }
}

void init_image(DkImage& image, ImageAlloc& mem, DkImageFormat format, uint32_t flags, const char* what) {
    DkImageLayoutMaker m;
    dkImageLayoutMakerDefaults(&m, R.device);
    m.flags = flags;
    m.format = format;
    m.dimensions[0] = R.width;
    m.dimensions[1] = R.height;
    DkImageLayout layout;
    dkImageLayoutInitialize(&layout, &m);
    mem = image_alloc(uint32_t(dkImageLayoutGetSize(&layout)), dkImageLayoutGetAlignment(&layout));
    dkImageInitialize(&image, &layout, mem.block, mem.offset);
    dklog("%s: %ux%u, %u KiB at image heap offset 0x%X", what, R.width, R.height, mem.size >> 10, mem.offset);
}

// ---- the window (the swapchain images): 1280x720 in both modes by default (the Vulkan path's size;
// docked, the system scales it), WIICOMPILED_DK_DOCKED_1080=1: 1920x1080 docked, the swapchain
// recreated when the operation mode changes (SwitchWakerHD backend.cpp window_wanted).
struct WindowConfig {
    bool byMode = false;
    int swapInterval = 1;
};
const WindowConfig& window_config() {
    static const WindowConfig c = [] {
        WindowConfig c;
        c.byMode = env_flag("WIICOMPILED_DK_DOCKED_1080", false);
        c.swapInterval = int(env_long("WIICOMPILED_DK_SWAP_INTERVAL", 1));
        if (c.swapInterval < 0 || c.swapInterval > 4) c.swapInterval = 1;
        dklog("window: %s; swap interval %d (WIICOMPILED_DK_SWAP_INTERVAL)",
              c.byMode ? "docked 1920x1080, handheld 1280x720 (WIICOMPILED_DK_DOCKED_1080=1)"
                       : "1280x720 in both modes (WIICOMPILED_DK_DOCKED_1080=1: 1920x1080 docked)",
              c.swapInterval);
        return c;
    }();
    return c;
}
int g_opMode = -1;  // the operation mode the window was last sized for: 1 docked, 0 handheld, -1 not yet
uint64_t g_windowResizes = 0;

void window_wanted(uint32_t& w, uint32_t& h) {
    const WindowConfig& c = window_config();
    const int docked = appletGetOperationMode() == AppletOperationMode_Console ? 1 : 0;
    if (docked != g_opMode) {
        dklog("operation mode: %s", docked ? "docked (TV)" : "handheld");
        g_opMode = docked;
    }
    const bool big = c.byMode && docked == 1;
    w = big ? 1920 : 1280;
    h = big ? 1080 : 720;
}

void init_swapchain() {
    const DkImage* images[kSwapImages];
    for (uint32_t i = 0; i < kSwapImages; i++) {
        char what[48];
        snprintf(what, sizeof what, "swapchain image %u (RGBA8)", i);
        init_image(g_swapImages[i], g_swapMem[i], DkImageFormat_RGBA8_Unorm,
                   DkImageFlags_UsageRender | DkImageFlags_UsagePresent | DkImageFlags_HwCompression, what);
        images[i] = &g_swapImages[i];
    }
    init_image(g_depth, g_depthMem, DkImageFormat_Z24S8, DkImageFlags_UsageRender | DkImageFlags_HwCompression,
               "present depth buffer (Z24S8)");
    DkSwapchainMaker m;
    dkSwapchainMakerDefaults(&m, R.device, nwindowGetDefault(), images, kSwapImages);
    dklog("creating the swapchain on the default window");
    log_flush();
    g_swapchain = dkSwapchainCreate(&m);
    dkSwapchainSetSwapInterval(g_swapchain, uint32_t(window_config().swapInterval));
    u32 nw = 0, nh = 0;
    nwindowGetDimensions(nwindowGetDefault(), &nw, &nh);
    dklog("swapchain: %u RGBA8 images of %ux%u on the default window (%ux%u)", kSwapImages, R.width, R.height, nw,
          nh);
}

// a new window size (the operation mode changed): the GPU finishes what was submitted, the old
// swapchain gives the window's buffers back, and images and a swapchain of the new size take their
// place. The old images' memory is freed once the GPU is done with this frame.
void resize_window(uint32_t w, uint32_t h, uint64_t frame) {
    const uint64_t t0 = now_ns();
    const uint32_t ow = R.width, oh = R.height;
    check_queue("recreating the swapchain");
    dkQueueWaitIdle(R.queue);
    dkSwapchainDestroy(g_swapchain);
    g_swapchain = nullptr;
    for (uint32_t i = 0; i < kSwapImages; i++) image_free_later(g_swapMem[i]);
    image_free_later(g_depthMem);
    R.width = w;
    R.height = h;
    init_swapchain();
    g_windowResizes++;
    dklog("frame %llu: window %ux%u -> %ux%u: swapchain recreated in %.1f ms", (unsigned long long)frame, ow, oh, w,
          h, double(now_ns() - t0) / 1e6);
}

// ---- statistics every 30 s: presents and where their time went, submits, command and stream memory per
// frame, the image heap, blocks, shader code, descriptors, the heap
struct Times {
    uint64_t presents = 0, fenceNs = 0, acquireNs = 0, recordNs = 0, submitNs = 0, waitNs = 0, maxNs = 0;
    uint64_t start = 0;
} g_times;
SubmitStats g_submits;
uint64_t g_submitsAtStats = 0;

void frame_stats(uint64_t presentNs) {
    g_times.presents++;
    g_times.maxNs = presentNs > g_times.maxNs ? presentNs : g_times.maxNs;
    const uint64_t now = now_ns();
    if (g_times.start == 0) g_times.start = now;
    if (now - g_times.start < 30'000'000'000ull) return;
    const double n = double(g_times.presents);
    const MemoryStats m = memory_stats_take();
    const DescriptorStats d = descriptor_stats();
    char shaders[320];
    shaders_report(shaders, sizeof shaders);
    dklog("frames %llu-%llu: %.1f presents/s; per present: frame fence %.2f ms, acquire %.2f ms, record %.2f ms, "
          "submit %.2f ms, pacing %.2f ms, longest %.2f ms, %.1f submits; command memory max %llu KiB/frame (%llu "
          "overflows), stream max %llu KiB/frame (%llu full); image heap %llu KiB in %llu chunks, blocks %llu KiB; "
          "descriptors: %u images, %u samplers in use, written %llu images, %llu samplers (%llu sampler "
          "evictions); ImGui textures %u; window %ux%u (%llu resizes); deko3d messages %d; heap never used %zu MiB",
          (unsigned long long)(R.frame - g_times.presents + 1), (unsigned long long)R.frame,
          n * 1e9 / double(now - g_times.start), double(g_times.fenceNs) / n / 1e6, double(g_times.acquireNs) / n / 1e6,
          double(g_times.recordNs) / n / 1e6, double(g_times.submitNs) / n / 1e6, double(g_times.waitNs) / n / 1e6,
          double(g_times.maxNs) / 1e6, double(g_submits.submits - g_submitsAtStats) / n,
          (unsigned long long)(m.cmdBytesMax >> 10), (unsigned long long)m.cmdOverflows,
          (unsigned long long)(m.streamBytesMax >> 10), (unsigned long long)m.streamFull,
          (unsigned long long)(m.imageBytes >> 10), (unsigned long long)m.imageChunks,
          (unsigned long long)(m.blockBytes >> 10), d.imagesUsed, d.samplersUsed, (unsigned long long)d.imageWrites,
          (unsigned long long)d.samplerWrites, (unsigned long long)d.samplerEvictions, overlay_textures(), R.width,
          R.height, (unsigned long long)g_windowResizes, g_debugMessages, heap_never_used_mib());
    dklog("%s", shaders);
    g_submitsAtStats = g_submits.submits;
    g_times = Times{};
    g_times.start = now;
}

// a 1x1 black texture in kEmptyImage: what a draw samples where its view is missing
bool g_emptyReady = false;
DkImage g_empty;
ImageAlloc g_emptyMem;
void empty_image_init() {
    if (g_emptyReady) return;
    DkImageLayoutMaker m;
    dkImageLayoutMakerDefaults(&m, R.device);
    m.format = DkImageFormat_RGBA8_Unorm;
    m.dimensions[0] = 1;
    m.dimensions[1] = 1;
    DkImageLayout layout;
    dkImageLayoutInitialize(&layout, &m);
    StreamAlloc s = stream_alloc(4, DK_IMAGE_LINEAR_STRIDE_ALIGNMENT);
    if (!s) return;
    g_emptyMem = image_alloc(uint32_t(dkImageLayoutGetSize(&layout)), dkImageLayoutGetAlignment(&layout));
    dkImageInitialize(&g_empty, &layout, g_emptyMem.block, g_emptyMem.offset);
    memset(s.cpu, 0, 4);
    static_cast<uint8_t*>(s.cpu)[3] = 0xFF;
    DkImageView view;
    dkImageViewDefaults(&view, &g_empty);
    const DkCopyBuf src = {s.gpu, 0, 0};
    const DkImageRect r = {0, 0, 0, 1, 1, 1};
    dkCmdBufCopyBufferToImage(R.cmd, &src, &view, &r, 0);
    barrier(DkBarrier_Full, DkInvalidateFlags_Image);
    write_image_descriptor(kEmptyImage, g_empty);
    g_emptyReady = true;
}

uint64_t g_frameOpenNs = 0;
bool g_initialized = false;

// the commands recorded so far to the GPU (the fences they signal are written under fence_mutex)
void submit_list() {
    check_queue("a submit");
    const DkCmdList list = dkCmdBufFinishList(R.cmd);
    std::lock_guard<std::mutex> lock(fence_mutex());
    dkQueueSubmitCommands(R.queue, list);
    fence_submitted();
    g_submits.submits++;
}

}  // namespace

void frame_open() {
    if (R.frameOpen) return;
    const uint64_t frame = R.frame + 1;
    const uint64_t t0 = now_ns();
    check_queue("the frame fence");
    frame_begin(frame);
    g_times.fenceNs += now_ns() - t0;
    g_frameOpenNs = now_ns();
    descriptors_frame_start();
    empty_image_init();
    overlay_upload_pending();
    shaders_frame_start();
    R.frameOpen = true;
}

void submit(const char* why) {
    (void)why;
    if (!R.frameOpen) return;
    submit_list();
}

SubmitStats submit_stats() { return g_submits; }

void frame_close() {
    if (!R.frameOpen) return;
    frame_end();
    submit_list();
    R.frame++;
    R.frameOpen = false;
    g_submits.frames++;
}

namespace {
struct ShotItem {
    uint32_t offset, width, height;
    const char* tag;
};
Block g_shot;
std::vector<ShotItem> g_shotItems;
uint32_t g_shotUsed = 0;

// after the frame's submit: wait for the copies, then each image at most 640 wide (every other pixel
// above that), RGB, base64 in 4096-character lines
void shot_flush(uint64_t frame) {
    if (g_shotItems.empty()) return;
    dkQueueWaitIdle(R.queue);
    static const char kB64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    for (const ShotItem& it : g_shotItems) {
        const uint32_t step = it.width > 800 ? 2 : 1, w = it.width / step, h = it.height / step;
        std::string rgb;
        rgb.reserve(size_t(w) * h * 3);
        for (uint32_t y = 0; y < h; y++) {
            const uint8_t* row = g_shot.cpu + it.offset + size_t(y) * step * it.width * 4;
            for (uint32_t x = 0; x < w; x++) rgb.append(reinterpret_cast<const char*>(row + size_t(x) * step * 4), 3);
        }
        std::fprintf(stderr, "[shot] begin %s frame=%llu %ux%u\n", it.tag, (unsigned long long)frame, w, h);
        std::string line;
        for (size_t i = 0; i < rgb.size(); i += 3) {
            const uint32_t v = uint8_t(rgb[i]) << 16 | uint8_t(rgb[i + 1]) << 8 | uint8_t(rgb[i + 2]);
            for (int s = 18; s >= 0; s -= 6) line += kB64[(v >> s) & 63];
            if (line.size() >= 4096 || i + 3 >= rgb.size()) {
                std::fprintf(stderr, "[shot] d %s\n", line.c_str());
                line.clear();
            }
        }
        std::fprintf(stderr, "[shot] end\n");
    }
    std::fflush(stderr);
    g_shotItems.clear();
    g_shotUsed = 0;
}
}  // namespace

bool shot_due() {
    static const long every = env_long("WIICOMPILED_DK_SHOT", 0);
    return every > 0 && (R.frame + 1) % uint64_t(every) == 0;
}

void shot_record(const DkImage& image, uint32_t width, uint32_t height, const char* tag) {
    if (!g_shot) {
        // WIICOMPILED_DK_SHOT_MB: the capture memory (default 16 MiB, 64 with WIICOMPILED_DK_SHOT_PASSES);
        // an image that does not fit in what is left of the frame's share is skipped (logged)
        const long mb = env_long("WIICOMPILED_DK_SHOT_MB", env_long("WIICOMPILED_DK_SHOT_PASSES", 0) > 0 ? 64 : 16);
        g_shot = block_create(uint32_t(std::clamp(mb, 4L, 256L)) << 20, "screenshots (WIICOMPILED_DK_SHOT)");
    }
    const uint32_t size = width * height * 4;
    if (!g_shot || g_shotUsed + size > g_shot.size) {
        dklog("shot: %s %ux%u skipped (WIICOMPILED_DK_SHOT_MB full)", tag, width, height);
        return;
    }
    barrier(DkBarrier_Fragments, 0);  // (whatever drew into it has finished)
    DkImageView view;
    dkImageViewDefaults(&view, &image);
    const DkImageRect rect = {0, 0, 0, width, height, 1};
    const DkCopyBuf dst = {g_shot.gpu + g_shotUsed, 0, 0};
    dkCmdBufCopyImageToBuffer(R.cmd, &view, &rect, &dst, 0);
    g_shotItems.push_back({g_shotUsed, width, height, tag});
    g_shotUsed += size;
}

PresentTimes frame_present(const ImDrawData* ui, PictureFn picture, void* user, void (*beforePresent)(void*),
                           void* beforeUser) {
    PresentTimes times;
    frame_open();
    const uint64_t frame = R.frame + 1;
    {
        uint32_t ww, wh;
        window_wanted(ww, wh);
        if (ww != R.width || wh != R.height) resize_window(ww, wh, frame);
    }
    const uint64_t t1 = now_ns();
    check_queue("acquiring a swapchain image");
    const int slot = dkQueueAcquireImage(R.queue, g_swapchain);
    const uint64_t t2 = now_ns();
    if (picture) picture(user, 0);
    DkImageView color, depth;
    dkImageViewDefaults(&color, &g_swapImages[slot]);
    dkImageViewDefaults(&depth, &g_depth);
    const DkImageView* colors[] = {&color};
    tiled_cache_flush();
    dkCmdBufBindRenderTargets(R.cmd, colors, 1, &depth);
    static const DkViewportSwizzle kIdentity = {DkSwizzle_PositiveX, DkSwizzle_PositiveY, DkSwizzle_PositiveZ,
                                                DkSwizzle_PositiveW};
    dkCmdBufSetViewportSwizzles(R.cmd, 0, &kIdentity, 1);  // (the game's passes may have flipped y: C.flipY)
    set_view(0, 0, R.width, R.height);
    dkCmdBufClearColorFloat(R.cmd, 0, DkColorMask_RGBA, 0.0f, 0.0f, 0.0f, 1.0f);
    dkCmdBufClearDepthStencil(R.cmd, true, 1.0f, 0xFF, 0);
    if (picture) {
        picture(user, 1);
        dkCmdBufSetViewportSwizzles(R.cmd, 0, &kIdentity, 1);  // (C.flipPresent)
        set_view(0, 0, R.width, R.height);
    }
    overlay_draw(ui);
    if (picture) picture(user, 2);  // (after ImGui: its GPU time)
    if (shot_due()) shot_record(g_swapImages[slot], R.width, R.height, "swapchain");
    frame_end();
    const uint64_t t3 = now_ns();
    submit_list();
    shot_flush(frame);
    const uint64_t t4 = now_ns();
    if (beforePresent) beforePresent(beforeUser);
    const uint64_t t5 = now_ns();
    check_queue("the present");
    dkQueuePresentImage(R.queue, g_swapchain, slot);
    const uint64_t t6 = now_ns();
    R.frame = frame;
    R.frameOpen = false;
    g_submits.frames++;
    if (frame == 1) {
        dklog("first present: acquire %.2f ms, record %.2f ms, submit %.2f ms, present %.2f ms; heap never used "
              "%zu MiB",
              double(t2 - t1) / 1e6, double(t3 - t2) / 1e6, double(t4 - t3) / 1e6, double(t6 - t5) / 1e6,
              heap_never_used_mib());
    }
    times.presented = true;
    times.acquireNs = t2 - t1;
    times.recordNs = (t1 - g_frameOpenNs) + (t3 - t2);
    times.submitNs = t4 - t3;
    times.waitNs = t5 - t4;
    times.presentNs = t6 - t5;
    g_times.acquireNs += times.acquireNs;
    g_times.recordNs += times.recordNs;
    g_times.submitNs += times.submitNs + times.presentNs;
    g_times.waitNs += times.waitNs;
    frame_stats(t6 - g_frameOpenNs);
    return times;
}

bool env_flag(const char* name, bool def) {
    const char* e = getenv(name);
    if (!e || !*e) return def;
    return *e != '0';
}

long env_long(const char* name, long def) {
    const char* e = getenv(name);
    if (!e || !*e) return def;
    char* end = nullptr;
    const long v = strtol(e, &end, 0);
    return end && end != e ? v : def;
}

void pin_current_thread(int core, int priority) { SwitchPinCurrentThread(core, priority); }

void dklog(const char* format, ...) {
    char line[1024];
    va_list args;
    va_start(args, format);
    const int n = vsnprintf(line, sizeof line, format, args);
    va_end(args);
    if (n < 0) return;
    fprintf(stdout, "[dk] %s\n", line);
}

void log_flush() { fflush(stdout); }

void fatal(const char* format, ...) {
    char line[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(line, sizeof line, format, args);
    va_end(args);
    fprintf(stdout, "[dk] FATAL: %s\n", line);
    log_flush();
    abort();
}

void check_queue(const char* before) {
    if (!dkQueueIsInErrorState(R.queue)) return;
    fatal("the deko3d queue is in an error state (a GPU fault) before %s of frame %llu; deko3d messages %d", before,
          (unsigned long long)R.frame + 1, g_debugMessages);
}

void barrier(DkBarrier mode, uint32_t invalidate) {
    if (mode != DkBarrier_None && R.tiledCache) dkCmdBufTiledCacheOp(R.cmd, DkTiledCacheOp_Flush);
    dkCmdBufBarrier(R.cmd, mode, invalidate);
}

void tiled_cache_set(bool on) {
    if (!on && R.tiledCache) dkCmdBufTiledCacheOp(R.cmd, DkTiledCacheOp_Flush);
    dkCmdBufTiledCacheOp(R.cmd, on ? DkTiledCacheOp_Enable : DkTiledCacheOp_Disable);
    R.tiledCache = on;
}

void tiled_cache_flush() {
    if (R.tiledCache) dkCmdBufTiledCacheOp(R.cmd, DkTiledCacheOp_Flush);
}

void set_view(uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    const DkViewport vp = {float(x), float(y), float(w), float(h), 0.0f, 1.0f};
    const DkScissor sc = {x, y, w, h};
    dkCmdBufSetViewports(R.cmd, 0, &vp, 1);
    dkCmdBufSetScissors(R.cmd, 0, &sc, 1);
}

void bind_2d_state(bool blend) {
    DkRasterizerState rs;
    dkRasterizerStateDefaults(&rs);
    rs.cullMode = DkFace_None;
    dkCmdBufBindRasterizerState(R.cmd, &rs);
    DkColorState cs;
    dkColorStateDefaults(&cs);
    dkColorStateSetBlendEnable(&cs, 0, blend);
    dkCmdBufBindColorState(R.cmd, &cs);
    DkColorWriteState cw;
    dkColorWriteStateDefaults(&cw);
    dkCmdBufBindColorWriteState(R.cmd, &cw);
    DkBlendState bs;
    dkBlendStateDefaults(&bs);
    dkBlendStateSetFactors(&bs, DkBlendFactor_SrcAlpha, DkBlendFactor_InvSrcAlpha, DkBlendFactor_One,
                           DkBlendFactor_InvSrcAlpha);
    dkCmdBufBindBlendStates(R.cmd, 0, &bs, 1);
    DkDepthStencilState ds;
    dkDepthStencilStateDefaults(&ds);
    ds.depthTestEnable = false;
    ds.depthWriteEnable = false;
    dkCmdBufBindDepthStencilState(R.cmd, &ds);
}

void init(uint32_t gxConfigVersion) {
    if (g_initialized) return;
    const uint64_t t0 = now_ns();
    const size_t heapBefore = heap_never_used_mib();
    const AppletType applet = appletGetAppletType();
    dklog("deko3d renderer (WIICOMPILED_SWITCH_RENDERER=deko3d): %s library; heap never used before the set-up "
          "%zu MiB",
          kDebugLib ? "debug (libdeko3dd: every call checked)" : "release", heapBefore);
    if (applet != AppletType_Application && applet != AppletType_SystemApplication) {
        dklog("WARNING: not in title mode: the deko3d set-up alone needs a few hundred MiB, which an applet may not "
              "have");
    }
    DkDeviceMaker dm;
    dkDeviceMakerDefaults(&dm);
    dm.cbDebug = debug_message;
    // window origin top left, depth 0 to 1, clip-space y up: WebGPU's conventions (dk.h)
    dm.flags = DkDeviceFlags_OriginUpperLeft | DkDeviceFlags_DepthZeroToOne;
    dklog("creating the device");
    log_flush();
    R.device = dkDeviceCreate(&dm);
    dklog("device created: flags 0x%X (origin upper left, depth 0 to 1)", dm.flags);

    DkQueueMaker qm;
    dkQueueMakerDefaults(&qm, R.device);
    // zcull off unless WIICOMPILED_DK_ZCULL=1: SwitchWakerHD saw depth-biased decals lose whole 4x8-pixel
    // tiles with it, and Mesa's nouveau (the Vulkan path's NVK) never enables it
    R.zcull = env_flag("WIICOMPILED_DK_ZCULL", false);
    qm.flags = DkQueueFlags_Graphics | DkQueueFlags_MediumPrio |
               (R.zcull ? DkQueueFlags_EnableZcull : DkQueueFlags_DisableZcull);
    qm.commandMemorySize = kQueueCommandMemory;
    qm.flushThreshold = qm.commandMemorySize / 8;
    dklog("creating the queue");
    log_flush();
    R.queue = dkQueueCreate(&qm);
    dklog("queue created: graphics, command memory %u KiB, zcull %s", qm.commandMemorySize >> 10,
          R.zcull ? "ON (WIICOMPILED_DK_ZCULL=1)" : "off");

    C.flipY = env_flag("WIICOMPILED_DK_FLIP_Y", false);
    C.flipFront = env_flag("WIICOMPILED_DK_FLIP_FRONT", false);
    C.flipTexture = env_flag("WIICOMPILED_DK_FLIP_TEXTURE", false);
    C.flipPresent = env_flag("WIICOMPILED_DK_FLIP_PRESENT", false);
    dklog("conventions: clip-space y %s, Aurora's front face %s, texture uploads %s, present %s "
          "(WIICOMPILED_DK_FLIP_Y / _FRONT / _TEXTURE / _PRESENT; the defaults are WebGPU's conventions as they are)",
          C.flipY ? "NEGATED by a viewport swizzle" : "as WebGPU's (up)",
          C.flipFront ? "INVERTED" : "as it is (CW -> DkFrontFace_CW)",
          C.flipTexture ? "FLIPPED (bottom row first)" : "row 0 first", C.flipPresent ? "FLIPPED vertically" : "upright");

    memory_init();
    window_wanted(R.width, R.height);  // the first swapchain at the current mode's size
    init_swapchain();
    shaders_init(gxConfigVersion);
    fixed_shaders_init();
    g_initialized = true;
    const size_t heapAfter = heap_never_used_mib();
    dklog("set-up cost: %zu MiB of heap (never used: %zu MiB before, %zu MiB after), %.0f ms",
          heapBefore > heapAfter ? heapBefore - heapAfter : 0, heapBefore, heapAfter, double(now_ns() - t0) / 1e6);
    log_flush();
}

void shutdown() {
    if (R.queue == nullptr) return;
    dkQueueWaitIdle(R.queue);
    if (g_swapchain != nullptr) {
        dkSwapchainDestroy(g_swapchain);  // gives the window's buffers back (nwindowReleaseBuffers)
        g_swapchain = nullptr;
    }
    dklog("shut down after %llu frames", (unsigned long long)R.frame);
    log_flush();
}

}  // namespace swdk
