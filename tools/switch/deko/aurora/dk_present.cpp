// SPDX-License-Identifier: MIT
// From SwitchWaker (switch/deko/aurora/dk_present.cpp at 56c0758; LICENSE.SwitchWaker). Modified for MLB
// Power Pros 2008 Recomp (tools/switch/deko, 2026-10-26): Aurora's own present (the XFB copy shader into
// the present viewport aurora.cpp computes) without SwitchWaker's resample, FXAA, SSAO, FSR or gamma
// passes; the renderer's selection, set-up and ImGui texture entry points; no readback.
// The present of the deko3d renderer (dk_gfx.hpp), on the frame worker (aurora.cpp's deko3d encode):
// the EFB (or the display copy that overrides it) drawn into the swapchain image with the blit shader
// in the viewport webgpu::calculate_present_viewport fits it to, ImGui over it, the frame's fence, the
// submit, Aurora's pacing wait and dkQueuePresentImage (core/device.cpp frame_present).
#include "dk_encode.hpp"
#include "dk_gfx.hpp"

#include "gx/pipeline.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

namespace aurora::gfx::dk {
using swdk::dklog;
using swdk::R;

namespace {

struct Picture {
    const PresentRequest* request = nullptr;
    bool valid = false;
} g_picture;

// the image kPresentImage describes (its address, size and format: a new texture can reuse the memory)
struct SourceKey {
    DkGpuAddr addr = 0;
    uint32_t width = 0, height = 0;
    DkImageFormat format = DkImageFormat_None;
    bool operator==(const SourceKey&) const = default;
} g_source;

void picture(void*, int stage) {
    const PresentRequest& req = *g_picture.request;
    if (stage == 0) {
        g_picture.valid = false;
        flush_pending();  // (a display copy's conversion: WIICOMPILED_DK_LEAN_BARRIERS)
        Tex* src = find_texture(req.source);
        if (!src || !src->valid || req.viewport[2] <= 0.f || req.viewport[3] <= 0.f) {
            note_unsupported(!src            ? "a present whose source texture has no deko3d image (black frame)"
                             : !src->valid   ? "a present whose source image is not valid (black frame)"
                                             : "a present with an empty viewport (black frame)");
            return;
        }
        if (!swdk::builtin_shader(swdk::kBlitVs) || !swdk::builtin_shader(swdk::kBlitFs)) return;
        // the source's descriptor of our own, written again when its image changes (a resize, another
        // display copy)
        const SourceKey key{dkImageGetGpuAddr(&src->image), src->width, src->height, src->format};
        if (!(key == g_source)) {
            DkImageView view;
            dkImageViewDefaults(&view, &src->image);
            swdk::write_image_view_descriptor(swdk::kPresentImage, view);
            g_source = key;
        }
        if (swdk::shot_due()) {
            if (src->format == DkImageFormat_RGBA8_Unorm || src->format == DkImageFormat_BGRA8_Unorm)
                swdk::shot_record(src->image, src->width, src->height, "present-source");
            else
                dklog("shot: present source %ux%u has format %d (not captured)", src->width, src->height, int(src->format));
            // WIICOMPILED_DK_SHOT_PASSES: the frame's last pass target too (the EFB when no display copy)
            if (Tex* last = diag_last_pass_target();
                last && last != src && last->valid &&
                (last->format == DkImageFormat_RGBA8_Unorm || last->format == DkImageFormat_BGRA8_Unorm))
                swdk::shot_record(last->image, last->width, last->height, "last-pass");
        }
        g_picture.valid = true;
        return;
    }
    if (stage == 1) {
        if (!g_picture.valid) return;
        const float* v = req.viewport;
        const DkViewport vp = {v[0], v[1], v[2], v[3], 0.f, 1.f};
        dkCmdBufSetViewports(R.cmd, 0, &vp, 1);
        const auto clampc = [](float x, uint32_t max) { return uint32_t(std::clamp(x, 0.f, float(max))); };
        const uint32_t x0 = clampc(std::floor(v[0]), R.width), y0 = clampc(std::floor(v[1]), R.height);
        const uint32_t x1 = clampc(std::ceil(v[0] + v[2]), R.width), y1 = clampc(std::ceil(v[1] + v[3]), R.height);
        const DkScissor sc = {x0, y0, x1 - x0, y1 - y0};
        dkCmdBufSetScissors(R.cmd, 0, &sc, 1);
        const DkViewportSwizzle sw = {DkSwizzle_PositiveX,
                                      swdk::C.flipPresent ? DkSwizzle_NegativeY : DkSwizzle_PositiveY,
                                      DkSwizzle_PositiveZ, DkSwizzle_PositiveW};
        dkCmdBufSetViewportSwizzles(R.cmd, 0, &sw, 1);
        const DkShader* shaders[] = {swdk::builtin_shader(swdk::kBlitVs), swdk::builtin_shader(swdk::kBlitFs)};
        dkCmdBufBindShaders(R.cmd, DkStageFlag_GraphicsMask, shaders, 2);
        swdk::bind_2d_state(false);
        dkCmdBufBindTexture(R.cmd, DkStage_Fragment, 0,
                            dkMakeTextureHandle(swdk::kPresentImage, swdk::kSamplerLinearClamp));
        dkCmdBufBindVtxAttribState(R.cmd, nullptr, 0);
        dkCmdBufBindVtxBufferState(R.cmd, nullptr, 0);
        dkCmdBufDraw(R.cmd, DkPrimitive_Triangles, 3, 1, 0, 0);
        invalidate_state();
        gpu_mark(GpuWork::Present);
        return;
    }
    gpu_mark(GpuWork::Imgui);  // stage 2: after ImGui
}

std::mutex g_noteMutex;
std::vector<std::string> g_notes;

}  // namespace

bool enabled() noexcept {
    // the default since it holds 60 fps at stock clocks; WIICOMPILED_SWITCH_RENDERER=vulkan for Dawn's Vulkan
    static const bool on = [] {
        const char* e = std::getenv("WIICOMPILED_SWITCH_RENDERER");
        return e == nullptr || std::strcmp(e, "vulkan") != 0;
    }();
    return on;
}

}  // namespace aurora::gfx::dk

// for the runtime's overlay and benchmark summary, which do not see Aurora's headers
extern "C" bool SwitchUsesDeko3d() { return aurora::gfx::dk::enabled(); }

namespace aurora::gfx::dk {

void initialize() {
    if (!enabled()) return;
    swdk::init(gx::GXPipelineConfigVersion);
    objects_activate();
}

void shutdown() {
    if (!enabled()) return;
    swdk::shutdown();
}

void note_unsupported(const char* what) {
    std::lock_guard<std::mutex> lock(g_noteMutex);
    for (const std::string& s : g_notes)
        if (s == what) return;
    g_notes.emplace_back(what);
    dklog("not done by the deko3d renderer (logged once): %s", what);
}

uint64_t imgui_texture(uint32_t width, uint32_t height, const void* rgba8) {
    return swdk::overlay_texture(width, height, rgba8);
}

void swapchain_size(uint32_t& width, uint32_t& height) {
    width = R.width;
    height = R.height;
}

PresentResult present(const PresentRequest& request) {
    if (!request.presentable) {
        swdk::frame_close();
        invalidate_state();
        return {};
    }
    // Aurora's pacing wait (the native slot's deadline) blocks the frame worker here, where the Vulkan
    // path's presenter thread waited on its own: WIICOMPILED_DK_PACING=0 leaves the pacing to the swap
    // interval alone (an A/B for the match benchmark)
    static const bool pacing = [] {
        const bool on = swdk::env_flag("WIICOMPILED_DK_PACING", true);
        dklog("present: Aurora's present deadline %s (WIICOMPILED_DK_PACING)", on ? "waited for" : "ignored");
        return on;
    }();
    g_picture.request = &request;
    const swdk::PresentTimes t = swdk::frame_present(request.ui, picture, nullptr,
                                                     pacing ? request.beforePresent : nullptr, request.user);
    g_picture.request = nullptr;
    invalidate_state();
    PresentResult r;
    r.presented = t.presented;
    r.acquireNs = t.acquireNs;
    r.recordNs = t.recordNs;
    r.submitNs = t.submitNs;
    r.waitNs = t.waitNs;
    r.presentNs = t.presentNs;
    return r;
}

}  // namespace aurora::gfx::dk
