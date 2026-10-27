// SPDX-License-Identifier: MIT
// From SwitchWaker (switch/deko/aurora/dk_encode.cpp at 56c0758; LICENSE.SwitchWaker). Modified for MLB
// Power Pros 2008 Recomp (tools/switch/deko, 2026-10-26): the encoder takes our Aurora's RenderPass /
// Command lists (gfx/common.cpp, through dk_common.inc) instead of upstream's FramePacket ops; Aurora's
// three staging slots are fenced per batch; texture uploads come from the wrapped queue writes; no
// immediates, no prepass, no depth peek (workstream C), no GPU groups by debug marker.
// Aurora's recorded passes to deko3d commands (dk_gfx.hpp), on the command buffer owner: the frame
// worker for a sealed frame, the producer thread for a staging split (gfx::split_staging_batch, which
// first waits for the frame worker to be done and holds the renderer GPU mutex). Never both at once.
//
// Frame data. Aurora's staging slot (vertices 3, uniforms 8, indices 2, storage 8 MiB, at common.cpp's
// offsets) is one deko3d block that the GX FIFO thread writes and the GPU reads in place: storage
// buffers 0 (vbuf) and 1 (abuf) are those regions with their sizes (Tint's length() reads them),
// uniform buffer 0 is the uniform region at each draw's dynamic offset (3840 bytes, both stages), the
// index buffer is the index region with each draw's first index. A batch ends with its slot's fence and
// a submit; begin_frame waits for that fence before the FIFO thread writes the slot again.
//
// Passes. Render targets bound from the views' shadows, load-op clears with deko3d's clear commands,
// then the pass's commands: viewport (reversed Z: depth range 1 - far .. 1 - near, as
// render_pass_impl), scissor (clamped as render_pass_impl), GX draws, partial-clear draws. Every pass
// ends with a Fragments barrier that also invalidates the texture cache (its EFB copy samples what it
// drew), and is followed by its EFB copy: the copy engine for an exact copy, workstream C's
// conversions otherwise. Copy-engine work (uploads, copies) sits between Full barriers. In the match
// (2026-10-27) a frame is ~31 passes on the one EFB (939x528), each but the last followed by a
// conversion: pass, barrier, conversion, barrier, pass... (~64 barriers per frame).
//
// State cache. Pipeline state, shaders, viewport, scissor, blend constant, uniform offset, texture
// handles are bound only when they change within a batch; anything that binds its own state (the
// present, ImGui, workstream C's fixed draws) resets it.
//
// The command list goes to the GPU every WIICOMPILED_DK_SUBMIT_DRAWS draws (default 256, 0 = once
// per batch), as SwitchWakerHD's, so the GPU starts on a frame while it is still being recorded.
//
// GPU cost switches (nxlink arguments, read once; each keeps the pixels as they are):
//   WIICOMPILED_DK_LEAN_BARRIERS=1  a conversion ends with a Fragments barrier alone (no texture cache
//                                   invalidation, so no wait for idle): what follows a conversion is the
//                                   next pass, which overwrites the EFB the conversion sampled (write
//                                   after read: fragment order is enough). The conversion's target is
//                                   remembered; a draw or conversion that samples it before the next
//                                   image barrier (the next pass end) gets one first, as do the batch's
//                                   end and the present.
//   WIICOMPILED_DK_TILED_CACHE=0    turns off Maxwell's tiled cache (binning; deko3d leaves it off, we turn it on
//                                   since run 6: -0.5 ms GPU per frame handheld, same image): on at every
//                                   frame's start, flushed before every barrier and render-target change.
//   WIICOMPILED_DK_AB=n             the two above in turn every n frames (off, lean, tiled, both), the GPU
//                                   time per frame of each logged with the 30 s statistics ("A/B").
//   WIICOMPILED_DK_HW_CLEARS=0      (default on since H4, 2026-10-27) Aurora's partial clear draws (GX copy
//                                   clears of a rectangle: a triangle over the target, scissored, the
//                                   colour as blend constant, the depth from the uniform) as the 3D
//                                   engine's clears: no fragment shader (whose gl_FragDepth forced late
//                                   Z), no blending, no destination read. Why the pixels are the same, from
//                                   deko3d's source (gpu_3d_base.cpp, clear_buffers.mme): ClearColor's
//                                   mask goes to ClearBuffers' R/G/B/A enables, which are the clear draw's
//                                   write mask (clear.cpp clear_write_mask); deko3d's init sets
//                                   ClearBufferFlags to StencilMask | Scissor, so the clear covers scissor 0
//                                   (set here to the draw's scissor, clamped as before; ScreenScissor is the
//                                   target) and the draw's viewport, the whole target, clips nothing more;
//                                   the colour write and depth states are bound all-on first, so whether
//                                   ClearBuffers also applies them cannot mask anything. Values: the colour
//                                   is GX's copy clear colour, u8 / 255.f (command_processor.cpp), which the
//                                   draw's blend (1.0 * constant + 0 * dst) and the clear both turn into the
//                                   same RGBA8 byte; the depth is clear_depth_value(), already in 0..1, which
//                                   both store as is into the Depth32Float EFB. A clear after GX draws of the
//                                   same pass first flushes the tiled cache (Aurora puts them at a pass's
//                                   start, right after pass_begin's flush, so this does not happen today).
// GPU timing (diagnostics):
//   WIICOMPILED_DK_PASS_TIMES=n     timestamps around every pass and clear draw; every n frames (1: 600)
//                                   the GPU time per pass index (palette conversions, load clears, clear
//                                   draws, draws, its EFB copy) with the pass's target, draws, clears and
//                                   copy ("pass times"); with WIICOMPILED_DK_AB, of its base turns only.
//   WIICOMPILED_DK_DRAW_TIMES=1     with PASS_TIMES: a timestamp after every GX draw too, and the GPU time
//                                   by pipeline (the time between a draw's timestamp and the one before;
//                                   the GPU overlaps draws, so this is where the time goes, roughly).
//                                   Each line also has the draws' raster rectangle (viewport and scissor,
//                                   as bound: kpx per draw) and the first texture bound (size, count);
//                                   each pass line the EFB rectangle its copy reads and the bounding box
//                                   of its GX draws' raster rectangles.
//   WIICOMPILED_DK_DRAW_PIXELS=1|2  with DRAW_TIMES: beside every timestamp a counter report, 1 samples
//                                   passed (depth test; deko3d enables it), 2 fragment shader invocations
//                                   (0 if the GPU's statistics counters are off); per pipeline and pass the
//                                   pixels, ps per pixel and SM lane-cycles per pixel at 307 MHz (256 lanes):
//                                   is a pipeline expensive per pixel, or does it cover many pixels?
//                                   (Counter reports flush the tiled cache too: times skew upwards.)
#include "dk_encode.hpp"

#include "dk_gfx.hpp"

#include "gfx/clear.hpp"
#include "gfx/common.hpp"
#include "gfx/pipeline_cache.hpp"
#include "gx/gx.hpp"
#include "gx/pipeline.hpp"
#include "webgpu/gpu.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace aurora::gfx::dk {
using swdk::dklog;
using swdk::R;

namespace {

// Aurora's staging layout (common.cpp begin_frame_impl / end_batch_impl)
constexpr uint64_t kVertexOffset = 0;
constexpr uint64_t kUniformOffset = kVertexOffset + VertexBufferSize;
constexpr uint64_t kIndexOffset = kUniformOffset + UniformBufferSize;
constexpr uint64_t kStorageOffset = kIndexOffset + IndexBufferSize;
constexpr size_t kStagingSlots = 3;  // common.cpp's FrameSlotCount

struct Staging {
    swdk::Block block;
    swdk::TrackedFence fence;  // signalled after the last batch that read the slot
};
Staging g_staging[kStagingSlots];

struct BatchState {
    bool open = false;
    uint32_t slot = 0;
    DkGpuAddr staging = 0;
    uint32_t drawsSinceSubmit = 0;
    bool workSinceTransfer = false;
    uint64_t objectsFrame = 0;  // the frame objects_frame_start ran for
} g_batch;

struct PassState {
    bool open = false;
    uint32_t width = 0, height = 0;
    bool requireReady = false;
    Tex* color = nullptr;  // the colour target (diagnostics)
    bool drawn = false;    // GX or clear draws since pass_begin's tiled cache flush (HW clears flush first)
} g_pass;

// ---- diagnostics for one console session (off by default):
//   WIICOMPILED_DK_DRAW_LOG=n    the first n GX draws of a diagnostic frame logged: pass, target,
//                                viewport, scissor, pipeline, indices, the uniform block's head
//                                (vtx_start .. array_start, then proj) and the first vertex words
//   WIICOMPILED_DK_SHOT_PASSES=n the colour target of the first n passes of a shot frame captured
//                                ("pass-NN"), and the frame's last pass target at the present
//                                ("last-pass"); needs WIICOMPILED_DK_SHOT
// A diagnostic frame is a WIICOMPILED_DK_SHOT frame, or every 600th frame without it.
struct Diag {
    uint32_t drawLog = 0, shotPasses = 0;
    bool frame = false;              // this frame is a diagnostic one
    uint32_t draws = 0, passes = 0;  // GX draws / passes of the frame so far
    Tex* lastColor = nullptr;        // the frame's last pass target
} g_diag;
char g_passTags[32][12];

struct Cache {
    bool globals = false;
    const Pipe* pipe = nullptr;
    const DkShader* vs = nullptr;
    const DkShader* fs = nullptr;
    bool rsValid = false, csValid = false, cwValid = false, bsValid = false, dsValid = false;
    DkRasterizerState rs{};
    DkColorState cs{};
    DkColorWriteState cw{};
    DkBlendState bs{};
    DkDepthStencilState ds{};
    bool vpValid = false, scValid = false, blendValid = false;
    DkViewport vp{};
    DkScissor sc{};
    float blend[4] = {};
    WGPUBindGroup textures = nullptr;
    bool emptyTextures = false;
    DkGpuAddr ubo = 0;
} g_cache;

EncodeStats g_stats;
uint32_t g_submitDraws = 256;

// the pipes this thread already found, by Aurora's PipelineRef (pipes never go away)
std::unordered_map<uint64_t, const Pipe*> g_pipeCache;

// ---- the GPU cost switches (see the top of the file), read at the first batch
struct Options {
    bool lean = false, tiled = false;  // WIICOMPILED_DK_LEAN_BARRIERS, WIICOMPILED_DK_TILED_CACHE
    uint32_t abFrames = 0;             // WIICOMPILED_DK_AB
    uint32_t passTimes = 0;            // WIICOMPILED_DK_PASS_TIMES (frames per report; 0: off)
    bool drawTimes = false;            // WIICOMPILED_DK_DRAW_TIMES
    uint8_t pixels = 0;                // WIICOMPILED_DK_DRAW_PIXELS (1 samples passed, 2 FS invocations)
    bool hwClears = false;             // WIICOMPILED_DK_HW_CLEARS
} g_opt;
// this frame's configuration (bit 0 lean barriers, bit 1 tiled cache): the switches, or the A/B turn
uint8_t g_config = 0;
constexpr const char* kConfigNames[4] = {"base", "lean barriers", "tiled cache", "lean + tiled"};
// lean barriers: conversion targets written since the last barrier that invalidated the texture cache
std::vector<const Tex*> g_pending;

// ---- GPU timestamps: every frame, at its first batch, the end of every pass and copy, the present
// and ImGui; read when the frame's slot comes round again (its fence waited for in frame_open). With
// WIICOMPILED_DK_PASS_TIMES each one also says which pass and which part of it it ends.
// (half of a frame's query region: timestamps; the other half: WIICOMPILED_DK_DRAW_PIXELS's counters)
constexpr uint32_t kMaxMarks = swdk::kQuerySize / swdk::kFrames / 16 / 2;
constexpr uint32_t kMaxPasses = 64;
enum Seg : uint8_t { kSegNone, kSegPalette, kSegLoad, kSegClears, kSegDraws, kSegCopy, kSegCount };
struct Tag {
    uint8_t seg = kSegNone;
    uint8_t pass = 0xFF;
};
// one pass of a frame as recorded (WIICOMPILED_DK_PASS_TIMES)
struct PassRec {
    uint32_t width = 0, height = 0, clearPixels = 0, copyW = 0, copyH = 0;
    int32_t srcX = 0, srcY = 0, srcW = 0, srcH = 0;     // the EFB rectangle its copy reads
    uint32_t drawX0 = ~0u, drawY0 = ~0u, drawX1 = 0, drawY1 = 0;  // bounding box of its GX draws' raster rects
    int32_t format = 0;
    uint16_t gxDraws = 0, clearDraws = 0;
    bool depth = false, loadColor = false, loadDepth = false;
    uint8_t copyKind = 0;  // 0 none, 1 conversion, 2 shader-sampled blit, 3 copy engine
    char label[24] = {};
};
struct Marks {
    GpuWork work[kMaxMarks];
    Tag tag[kMaxMarks];
    const Pipe* pipe[kMaxMarks];  // the GX draw a mark follows (WIICOMPILED_DK_DRAW_TIMES), else null
    uint32_t indices[kMaxMarks];  // (and its index count times instances)
    uint32_t area[kMaxMarks];     // (its raster rectangle, viewport and scissor: pixels)
    uint32_t texWH[kMaxMarks];    // (its first bound texture: width << 16 | height; 0 none)
    uint8_t texCount[kMaxMarks];  // (bound textures)
    uint32_t count = 0;
    uint64_t cpuNs = 0;  // when the frame was opened
    uint8_t config = 0;
    uint32_t passCount = 0;  // the frame's passes so far (index of the next one)
    PassRec passes[kMaxPasses];
};
Marks g_marks[swdk::kFrames];
bool g_gpuTimers = true;
uint64_t g_statsStart = 0;
Tag g_tag;                     // what the next mark without a tag of its own ends
bool g_passEnded = false;      // a pass ended since the walk's last pass_prologue
uint32_t g_drawsSinceMark = 0; // GX draws of the open pass since its last mark
// (diagnostics) the viewport and scissor the GPU has, whatever the state cache says: pass_begin and
// draw_clear set their own
DkViewport g_curVp{};
DkScissor g_curSc{};
struct DrawInfo {
    uint32_t area = 0, texWH = 0;
    uint8_t texCount = 0;
};

Marks& frame_marks() { return g_marks[(R.frame + 1) % swdk::kFrames]; }
PassRec* open_pass_rec() {
    Marks& m = frame_marks();
    return m.passCount && m.passCount <= kMaxPasses ? &m.passes[m.passCount - 1] : nullptr;
}

void gpu_mark_tag(GpuWork work, Tag tag, const Pipe* pipe = nullptr, uint32_t indices = 0, DrawInfo info = {}) {
    if (!g_gpuTimers || !R.frameOpen) return;
    const uint64_t frame = R.frame + 1;
    Marks& m = g_marks[frame % swdk::kFrames];
    if (m.count >= kMaxMarks) {
        g_stats.gpuDropped++;
        return;
    }
    const swdk::QueryRegion q = swdk::query_region(frame);
    dkCmdBufReportCounter(R.cmd, DkCounter_Timestamp, q.gpu + uint64_t(m.count) * 16);
    if (g_opt.pixels) {
        dkCmdBufReportCounter(R.cmd, g_opt.pixels == 2 ? DkCounter_FragmentShaderInvocations : DkCounter_SamplesPassed,
                              q.gpu + uint64_t(kMaxMarks + m.count) * 16);
    }
    m.work[m.count] = work;
    m.tag[m.count] = tag;
    m.pipe[m.count] = pipe;
    m.indices[m.count] = indices;
    m.area[m.count] = info.area;
    m.texWH[m.count] = info.texWH;
    m.texCount[m.count] = info.texCount;
    m.count++;
    g_drawsSinceMark = 0;
}

// ---- the per-pass GPU times (WIICOMPILED_DK_PASS_TIMES) and the A/B turns, summed over frames
struct PassAgg {
    uint64_t frames = 0, varied = 0;
    uint64_t ns[kSegCount] = {};
    uint64_t gxDraws = 0, clearDraws = 0, clearPixels = 0, loadColor = 0, loadDepth = 0, depth = 0;
    uint64_t copies[4] = {};
    uint64_t pixels = 0, drawPixels = 0;  // WIICOMPILED_DK_DRAW_PIXELS: all its marks', its GX draws'
    uint32_t width = 0, height = 0, copyW = 0, copyH = 0;
    int32_t srcX = 0, srcY = 0, srcW = 0, srcH = 0;
    uint32_t drawX0 = 0, drawY0 = 0, drawX1 = 0, drawY1 = 0;  // the last frame's bounding box
    int32_t format = 0;
    char label[24] = {};
};
struct DrawAgg {
    uint64_t ns = 0, draws = 0, indices = 0, passMask = 0;
    uint64_t area = 0, pixels = 0, texCount = 0;
    uint32_t texWH = 0;  // (the last draw's)
};
struct PassTimes {
    uint64_t frames = 0, totalNs = 0, otherNs = 0, passes = 0;
    PassAgg agg[kMaxPasses];
    std::unordered_map<const Pipe*, DrawAgg> draws;
} g_pt;
struct AbStats {
    uint64_t frames[4] = {}, totalNs[4] = {}, passNs[4] = {}, convNs[4] = {};
} g_ab;

void log_pass_times() {
    const double n = double(std::max<uint64_t>(g_pt.frames, 1));
    const auto ms = [&](uint64_t ns) { return double(ns) / n / 1e6; };
    uint64_t seg[kSegCount] = {};
    for (const PassAgg& a : g_pt.agg)
        for (uint32_t s = 0; s < kSegCount; s++) seg[s] += a.ns[s];
    dklog("pass times over %llu frames (GPU ms per frame; WIICOMPILED_DK_PASS_TIMES%s; config %s): total %.3f = palette "
          "conversions %.3f + load clears %.3f + clear draws %.3f + draws %.3f + EFB copies %.3f + other %.3f (uploads, "
          "present, ImGui); %.1f passes per frame",
          (unsigned long long)g_pt.frames, g_opt.drawTimes ? ", with draw timestamps" : "",
          g_opt.abFrames ? "the A/B's base turns" : kConfigNames[g_config], ms(g_pt.totalNs), ms(seg[kSegPalette]),
          ms(seg[kSegLoad]), ms(seg[kSegClears]), ms(seg[kSegDraws]), ms(seg[kSegCopy]),
          ms(g_pt.otherNs + seg[kSegNone]), double(g_pt.passes) / n);
    static const char* const kCopy[4] = {"none", "conversion", "blit", "copy engine"};
    for (uint32_t p = 0; p < kMaxPasses; p++) {
        const PassAgg& a = g_pt.agg[p];
        if (!a.frames) continue;
        const double f = double(a.frames);
        uint32_t copyKind = 0;
        for (uint32_t k = 1; k < 4; k++)
            if (a.copies[k] > a.copies[copyKind]) copyKind = k;
        uint64_t total = 0;
        for (uint32_t s = 0; s < kSegCount; s++) total += a.ns[s];
        char varied[48] = "";
        if (a.varied) snprintf(varied, sizeof varied, " (another target in %.0f%%)", 100.0 * double(a.varied) / f);
        char extra[200] = "";
        if (g_opt.drawTimes) {
            int o = snprintf(extra, sizeof extra, "; copy reads %dx%d at (%d,%d), GX draws within ", a.srcW, a.srcH,
                             a.srcX, a.srcY);
            if (a.drawX1 > a.drawX0)
                o += snprintf(extra + o, sizeof extra - size_t(o), "%ux%u at (%u,%u)", a.drawX1 - a.drawX0,
                              a.drawY1 - a.drawY0, a.drawX0, a.drawY0);
            else
                o += snprintf(extra + o, sizeof extra - size_t(o), "-");
            if (g_opt.pixels)
                snprintf(extra + o, sizeof extra - size_t(o), "; %s %.0f kpx (GX draws %.0f kpx)",
                         g_opt.pixels == 2 ? "FS invocations" : "samples passed", double(a.pixels) / f / 1000.0,
                         double(a.drawPixels) / f / 1000.0);
        }
        dklog("  pass %2u: %ux%u fmt %d '%s'%s%s, in %.0f%% of frames; load clear colour %.0f%% depth %.0f%%; %.1f "
              "draws, %.1f clear draws (%.0f kpx); then %s %ux%u (%.0f%%) | GPU %.3f ms = palette %.3f + load %.3f + "
              "clears %.3f + draws %.3f + copy %.3f%s",
              p, a.width, a.height, int(a.format), a.label, a.depth ? " +depth" : "", varied, 100.0 * f / n,
              100.0 * double(a.loadColor) / f, 100.0 * double(a.loadDepth) / f, double(a.gxDraws) / f,
              double(a.clearDraws) / f, double(a.clearPixels) / f / 1000.0, kCopy[copyKind], a.copyW, a.copyH,
              100.0 * double(a.copies[copyKind]) / f, ms(total), ms(a.ns[kSegPalette]), ms(a.ns[kSegLoad]),
              ms(a.ns[kSegClears]), ms(a.ns[kSegDraws]), ms(a.ns[kSegCopy]), extra);
    }
    if (g_opt.drawTimes && !g_pt.draws.empty()) {
        std::vector<std::pair<const Pipe*, DrawAgg>> v(g_pt.draws.begin(), g_pt.draws.end());
        std::sort(v.begin(), v.end(), [](const auto& a, const auto& b) { return a.second.ns > b.second.ns; });
        uint64_t all = 0;
        for (const auto& e : v) all += e.second.ns;
        dklog("  draw times by pipeline (WIICOMPILED_DK_DRAW_TIMES): %zu pipelines, %.3f ms per frame; the top 24:",
              v.size(), ms(all));
        for (size_t i = 0; i < v.size() && i < 24; i++) {
            const Pipe* p = v[i].first;
            const DrawAgg& d = v[i].second;
            char passes[96];
            int o = 0;
            for (uint32_t b = 0; b < 64 && o < int(sizeof passes) - 8; b++)
                if (d.passMask >> b & 1) o += snprintf(passes + o, sizeof passes - size_t(o), "%s%u", o ? "," : "", b);
            if (!o) snprintf(passes, sizeof passes, "-");
            const double dr = double(std::max<uint64_t>(d.draws, 1));
            char px[160] = "";
            if (g_opt.pixels) {
                // ps per pixel, and what that is in SM lane-cycles (2 SMs x 128 lanes) at 307 MHz
                const double ps = d.pixels ? double(d.ns) * 1000.0 / double(d.pixels) : 0.0;
                snprintf(px, sizeof px, "; %.1f kpx/draw %s, %.0f ps/px = %.0f lane-cycles/px at 307 MHz",
                         double(d.pixels) / dr / 1000.0, g_opt.pixels == 2 ? "shaded" : "passed", ps,
                         ps * 1e-12 * 307e6 * 256.0);
            }
            dklog("    %.3f ms (%.1f%%): %.1f draws/frame, %.0f indices/draw; pipe %016llx module %016llx; depth "
                  "test %u write %u compare %u, blend %s, cull %d; passes %s; raster rect %.1f kpx/draw; textures %.1f (first "
                  "%ux%u)%s",
                  ms(d.ns), all ? 100.0 * double(d.ns) / double(all) : 0.0, double(d.draws) / n,
                  d.draws ? double(d.indices) / double(d.draws) : 0.0, (unsigned long long)p->ref,
                  (unsigned long long)(p->module ? p->module->key : 0), unsigned(p->depthStencil.depthTestEnable),
                  unsigned(p->depthStencil.depthWriteEnable), unsigned(p->depthStencil.depthCompareOp), p->color.blendEnableMask ? "on" : "off",
                  int(p->rasterizer.cullMode), passes, double(d.area) / dr / 1000.0, double(d.texCount) / dr,
                  d.texWH >> 16, d.texWH & 0xFFFF, px);
        }
    }
    g_pt = PassTimes{};
}

// a frame's timestamps read: the intervals by pass and part (each interval belongs to the mark that
// ends it), the pass records
void add_pass_times(const Marks& m, const uint64_t* dt, const uint64_t* dpx) {
    for (uint32_t i = 1; i < m.count; i++) {
        const Tag t = m.tag[i];
        if (t.seg != kSegNone && t.pass < m.passCount && t.pass < kMaxPasses) {
            g_pt.agg[t.pass].ns[t.seg] += dt[i];
            g_pt.agg[t.pass].pixels += dpx[i];
            if (m.pipe[i]) {
                DrawAgg& d = g_pt.draws[m.pipe[i]];
                d.ns += dt[i];
                d.draws++;
                d.indices += m.indices[i];
                d.passMask |= uint64_t(1) << t.pass;
                d.area += m.area[i];
                d.pixels += dpx[i];
                d.texCount += m.texCount[i];
                if (m.texWH[i]) d.texWH = m.texWH[i];
                g_pt.agg[t.pass].drawPixels += dpx[i];
            }
        } else {
            g_pt.otherNs += dt[i];
        }
    }
    const uint32_t passes = std::min(m.passCount, kMaxPasses);
    for (uint32_t p = 0; p < passes; p++) {
        const PassRec& r = m.passes[p];
        PassAgg& a = g_pt.agg[p];
        if (a.frames == 0) {
            a.width = r.width;
            a.height = r.height;
            a.format = r.format;
            memcpy(a.label, r.label, sizeof a.label);
        } else if (a.width != r.width || a.height != r.height || a.format != r.format) {
            a.varied++;
        }
        a.frames++;
        a.gxDraws += r.gxDraws;
        a.clearDraws += r.clearDraws;
        a.clearPixels += r.clearPixels;
        a.loadColor += r.loadColor;
        a.loadDepth += r.loadDepth;
        a.depth += r.depth;
        a.copies[r.copyKind & 3]++;
        if (r.copyKind) {
            a.copyW = r.copyW;
            a.copyH = r.copyH;
            a.srcX = r.srcX;
            a.srcY = r.srcY;
            a.srcW = r.srcW;
            a.srcH = r.srcH;
        }
        if (r.drawX1 > r.drawX0) {
            a.drawX0 = r.drawX0;
            a.drawY0 = r.drawY0;
            a.drawX1 = r.drawX1;
            a.drawY1 = r.drawY1;
        }
    }
    g_pt.passes += m.passCount;
}

// the GPU's timer against the CPU's (SwitchWakerHD backend.cpp GpuClock): deko3d's dkTimestampToNs
// assumes 31.25 MHz, the Tegra X1's runs at 19.2 MHz; measured over the session
struct GpuClock {
    bool have = false;
    uint64_t cpu0 = 0, gpu0 = 0;
    double factor = 31.25 / 19.2;
    void sample(uint64_t cpuNs, uint64_t gpuNs) {
        if (!have) {
            have = true;
            cpu0 = cpuNs;
            gpu0 = gpuNs;
            return;
        }
        if (gpuNs <= gpu0 || cpuNs - cpu0 < 4'000'000'000ull) return;
        factor = double(cpuNs - cpu0) / double(gpuNs - gpu0);
    }
} g_clock;

// ---- late frames: each frame's GPU time beside its present interval (the 30 s statistics). A present
// more than 18 ms after the previous one missed a vblank; when that frame's GPU time is over 16.0 ms the
// GPU made it late, else the CPU side (the game thread, the FIFO worker or the recording).
struct LateFrames {
    static constexpr uint32_t kBuckets = 160;  // GPU time histogram, 0.25 ms each (40 ms and over: the last)
    uint64_t frames = 0, late = 0, lateGpu = 0, gpuOver = 0;
    uint32_t hist[kBuckets] = {};
    struct Worst {
        uint64_t frame, intervalNs, gpuNs;
    } worst[6] = {};
} g_late;

void note_frame(uint64_t frame, uint64_t gpuNs) {
    const uint64_t t1 = swdk::present_ns(frame), t0 = swdk::present_ns(frame - 1);
    if (!t1 || !t0) return;
    const uint64_t interval = t1 - t0;
    LateFrames& l = g_late;
    l.frames++;
    l.hist[std::min<uint64_t>(gpuNs / 250'000, LateFrames::kBuckets - 1)]++;
    if (gpuNs > 16'000'000) l.gpuOver++;
    if (interval > 18'000'000) {
        l.late++;
        if (gpuNs > 16'000'000) l.lateGpu++;
    }
    auto* w = std::min_element(std::begin(l.worst), std::end(l.worst),
                               [](const auto& a, const auto& b) { return a.intervalNs < b.intervalNs; });
    if (interval > w->intervalNs) *w = {frame, interval, gpuNs};
}

void log_late() {
    LateFrames& l = g_late;
    if (!l.frames) return;
    const auto pct = [&](double p) {
        uint64_t seen = 0;
        for (uint32_t i = 0; i < LateFrames::kBuckets; i++)
            if ((seen += l.hist[i]) >= uint64_t(p * double(l.frames))) return double(i + 1) * 0.25;
        return 40.0;
    };
    std::sort(std::begin(l.worst), std::end(l.worst), [](const auto& a, const auto& b) { return a.intervalNs > b.intervalNs; });
    char worst[256];
    int o = 0;
    for (const auto& w : l.worst)
        if (w.intervalNs)
            o += snprintf(worst + o, sizeof worst - size_t(o), "%s#%llu %.1f/%.1f", o ? ", " : "",
                          (unsigned long long)w.frame, double(w.intervalNs) / 1e6, double(w.gpuNs) / 1e6);
    dklog("late frames: %llu of %llu presents over 18 ms after the previous, %llu of them with GPU over 16 ms "
          "(the rest: CPU side); GPU over 16 ms in %llu frames; GPU per frame p50 %.2f p95 %.2f p99 %.2f ms; "
          "worst (frame interval/GPU ms): %s",
          (unsigned long long)l.late, (unsigned long long)l.frames, (unsigned long long)l.lateGpu,
          (unsigned long long)l.gpuOver, pct(.50), pct(.95), pct(.99), o ? worst : "none");
    l = {};
}

void read_marks(uint64_t frame) {
    Marks& m = g_marks[frame % swdk::kFrames];
    if (m.count >= 2) {
        const swdk::QueryRegion q = swdk::query_region(frame);
        static uint64_t ts[kMaxMarks];  // (command buffer owner only; off the stack)
        for (uint32_t i = 0; i < m.count; i++) memcpy(&ts[i], q.cpu + size_t(i) * 16 + 8, 8);
        g_clock.sample(m.cpuNs, dkTimestampToNs(ts[0]));
        bool ok = true;
        for (uint32_t i = 1; i < m.count && ok; i++) ok = ts[i] >= ts[i - 1];
        if (ok) {
            static uint64_t dt[kMaxMarks], dpx[kMaxMarks];
            uint64_t passNs = 0, convNs = 0;
            dpx[0] = 0;
            if (g_opt.pixels) {
                // Every counter report (deko3d: a four-word semaphore release) is {u64 count, u64 timestamp}:
                // the count in the first 8 bytes for samples passed too. (Until H4 the samples were read as
                // the 32 bits at +4, the count's high word: 0 kpx and absurd ps/px in run 9.)
                uint64_t last = 0;
                for (uint32_t i = 0; i < m.count; i++) {
                    const uint8_t* rec = q.cpu + size_t(kMaxMarks + i) * 16;
                    uint64_t v = 0;
                    memcpy(&v, rec, 8);
                    if (i) dpx[i] = v >= last ? v - last : 0;  // (a counter reset between marks: none counted)
                    last = v;
                }
            } else {
                memset(dpx, 0, sizeof(uint64_t) * m.count);
            }
            for (uint32_t i = 1; i < m.count; i++) {
                dt[i] = uint64_t(double(dkTimestampToNs(ts[i] - ts[i - 1])) * g_clock.factor);
                g_stats.gpuNs[size_t(m.work[i])] += dt[i];
                if (m.work[i] == GpuWork::Efb) passNs += dt[i];
                if (m.work[i] == GpuWork::Conversion) convNs += dt[i];
            }
            const uint64_t total = uint64_t(double(dkTimestampToNs(ts[m.count - 1] - ts[0])) * g_clock.factor);
            g_stats.gpuTotalNs += total;
            g_stats.gpuFrames++;
            note_frame(frame, total);
            const uint8_t c = m.config & 3;
            g_ab.frames[c]++;
            g_ab.totalNs[c] += total;
            g_ab.passNs[c] += passNs;
            g_ab.convNs[c] += convNs;
            if (g_opt.passTimes && (!g_opt.abFrames || c == 0)) {  // (with the A/B: its base turns)
                add_pass_times(m, dt, dpx);
                g_pt.totalNs += total;
                if (++g_pt.frames >= g_opt.passTimes) log_pass_times();
            }
        } else {
            g_stats.gpuDropped++;
        }
    }
    m.count = 0;
}

// every 30 s: what the encoder did and the GPU time per frame by kind
void log_stats() {
    const uint64_t now = swdk::now_ns();
    if (g_statsStart == 0) g_statsStart = now;
    if (now - g_statsStart < 30'000'000'000ull) return;
    static EncodeStats last;
    const EncodeStats& s = g_stats;
    const double frames = double(std::max<uint64_t>(s.gpuFrames - last.gpuFrames, 1));
    const auto gpu = [&](GpuWork w) {
        return double(s.gpuNs[size_t(w)] - last.gpuNs[size_t(w)]) / frames / 1e6;
    };
    const ObjectStats o = object_stats();
    const PipelineStats p = pipeline_stats();
    dklog("encoder: %llu batches, %llu passes, %llu draws, %llu clears, skipped %llu (no pipeline) + %llu (shaders) "
          "+ %llu passes; binds: %llu pipelines, %llu shaders, %llu textures, %llu uniforms; %llu copies, %llu "
          "conversions, %llu uploads, %llu barriers, %llu submits; CPU %.2f ms/batch; GPU per frame %.2f ms (passes "
          "%.2f, copies %.2f, conversions %.2f, present %.2f, ImGui %.2f, other %.2f; %llu frames dropped); "
          "textures %llu (%llu MiB), %llu uploads (%llu MiB) and %llu clears in all; pipes %llu GX + %llu clear, "
          "%llu new modules",
          (unsigned long long)(s.batches - last.batches), (unsigned long long)(s.passes - last.passes),
          (unsigned long long)(s.draws - last.draws), (unsigned long long)(s.clears - last.clears),
          (unsigned long long)(s.skippedPipeline - last.skippedPipeline),
          (unsigned long long)(s.skippedShader - last.skippedShader),
          (unsigned long long)(s.skippedPass - last.skippedPass),
          (unsigned long long)(s.pipelineBinds - last.pipelineBinds),
          (unsigned long long)(s.shaderBinds - last.shaderBinds),
          (unsigned long long)(s.textureBinds - last.textureBinds),
          (unsigned long long)(s.uniformBinds - last.uniformBinds), (unsigned long long)(s.copies - last.copies),
          (unsigned long long)(s.conversions - last.conversions), (unsigned long long)(s.uploads - last.uploads),
          (unsigned long long)(s.barriers - last.barriers), (unsigned long long)(s.submits - last.submits),
          double(s.encodeNs - last.encodeNs) / double(std::max<uint64_t>(s.batches - last.batches, 1)) / 1e6,
          double(s.gpuTotalNs - last.gpuTotalNs) / frames / 1e6, gpu(GpuWork::Efb), gpu(GpuWork::Copy),
          gpu(GpuWork::Conversion), gpu(GpuWork::Present), gpu(GpuWork::Imgui),
          gpu(GpuWork::Other) + gpu(GpuWork::FrameStart), (unsigned long long)(s.gpuDropped - last.gpuDropped),
          (unsigned long long)o.textures, (unsigned long long)(o.imageBytes >> 20), (unsigned long long)o.uploads,
          (unsigned long long)(o.uploadBytes >> 20), (unsigned long long)o.clears, (unsigned long long)p.gx,
          (unsigned long long)p.clear, (unsigned long long)p.modulesNew);
    if (g_opt.lean || g_opt.abFrames) {
        dklog("encoder: lean barriers: %llu Fragments-only barriers after conversions, %llu image barriers for a "
              "conversion target sampled before the next pass end",
              (unsigned long long)(s.leanBarriers - last.leanBarriers),
              (unsigned long long)(s.pendingFlushes - last.pendingFlushes));
    }
    if (g_opt.abFrames) {
        // since the session's start: the turns interleave every WIICOMPILED_DK_AB frames, so each one sees the
        // same scenes
        char line[512];
        int o = 0;
        for (uint32_t c = 0; c < 4; c++) {
            const double n = double(std::max<uint64_t>(g_ab.frames[c], 1));
            o += snprintf(line + o, sizeof line - size_t(o), "%s%s %.3f ms (passes %.3f, conversions %.3f; %llu frames)",
                          c ? "; " : "", kConfigNames[c], double(g_ab.totalNs[c]) / n / 1e6,
                          double(g_ab.passNs[c]) / n / 1e6, double(g_ab.convNs[c]) / n / 1e6,
                          (unsigned long long)g_ab.frames[c]);
        }
        dklog("A/B GPU per frame since the start, by turn of %u frames (WIICOMPILED_DK_AB): %s", g_opt.abFrames, line);
    }
    log_late();
    last = s;
    g_statsStart = now;
}

void barrier(DkBarrier mode, uint32_t invalidate) {
    swdk::barrier(mode, invalidate);
    g_stats.barriers++;
    // the texture cache now sees every conversion target written before (lean barriers)
    if (mode != DkBarrier_None && (invalidate & DkInvalidateFlags_Image)) g_pending.clear();
}

bool is_pending(const Tex* t) {
    for (const Tex* p : g_pending)
        if (p == t) return true;
    return false;
}

void set_swizzle(bool flip) {
    const DkViewportSwizzle sw = {DkSwizzle_PositiveX, flip ? DkSwizzle_NegativeY : DkSwizzle_PositiveY,
                                  DkSwizzle_PositiveZ, DkSwizzle_PositiveW};
    dkCmdBufSetViewportSwizzles(R.cmd, 0, &sw, 1);
}

// the batch-wide bindings of GX draws (gx::render's bind group 0 and index buffer)
void ensure_globals() {
    if (g_cache.globals) return;
    const DkGpuAddr s = g_batch.staging;
    for (DkStage stage : {DkStage_Vertex, DkStage_Fragment}) {
        dkCmdBufBindStorageBuffer(R.cmd, stage, 0, s + kVertexOffset, uint32_t(VertexBufferSize));
        dkCmdBufBindStorageBuffer(R.cmd, stage, 1, s + kStorageOffset, uint32_t(StorageBufferSize));
    }
    dkCmdBufBindIdxBuffer(R.cmd, DkIdxFormat_Uint16, s + kIndexOffset);
    dkCmdBufBindVtxAttribState(R.cmd, nullptr, 0);
    dkCmdBufBindVtxBufferState(R.cmd, nullptr, 0);
    set_swizzle(swdk::C.flipY);
    g_cache.globals = true;
}

template <class T> bool changed(bool& valid, T& cached, const T& now) {
    if (valid && memcmp(&cached, &now, sizeof now) == 0) return false;
    cached = now;
    valid = true;
    return true;
}

void bind_pipe(const Pipe* p) {
    if (g_cache.pipe == p) return;
    const DkShader* vs = pipe_vs(p);
    const DkShader* fs = pipe_fs(p);
    if (vs != g_cache.vs || fs != g_cache.fs) {
        if (swdk::code_take_written()) dkCmdBufBarrier(R.cmd, DkBarrier_None, DkInvalidateFlags_Shader);
        const DkShader* sh[] = {vs, fs};
        dkCmdBufBindShaders(R.cmd, DkStageFlag_GraphicsMask, sh, 2);
        g_cache.vs = vs;
        g_cache.fs = fs;
        g_stats.shaderBinds++;
    }
    if (changed(g_cache.rsValid, g_cache.rs, p->rasterizer)) dkCmdBufBindRasterizerState(R.cmd, &p->rasterizer);
    if (changed(g_cache.csValid, g_cache.cs, p->color)) dkCmdBufBindColorState(R.cmd, &p->color);
    if (changed(g_cache.cwValid, g_cache.cw, p->colorWrite)) dkCmdBufBindColorWriteState(R.cmd, &p->colorWrite);
    if (changed(g_cache.bsValid, g_cache.bs, p->blend)) dkCmdBufBindBlendStates(R.cmd, 0, &p->blend, 1);
    if (changed(g_cache.dsValid, g_cache.ds, p->depthStencil)) dkCmdBufBindDepthStencilState(R.cmd, &p->depthStencil);
    g_cache.pipe = p;
    g_stats.pipelineBinds++;
}

void set_blend_const(float r, float g, float b, float a) {
    const float c[4] = {r, g, b, a};
    if (g_cache.blendValid && memcmp(g_cache.blend, c, sizeof c) == 0) return;
    dkCmdBufSetBlendConst(R.cmd, r, g, b, a);
    memcpy(g_cache.blend, c, sizeof c);
    g_cache.blendValid = true;
}

void bind_uniforms(DkGpuAddr ubo, uint32_t size) {
    if (ubo == g_cache.ubo) return;
    dkCmdBufBindUniformBuffer(R.cmd, DkStage_Vertex, 0, ubo, size);
    dkCmdBufBindUniformBuffer(R.cmd, DkStage_Fragment, 0, ubo, size);
    g_cache.ubo = ubo;
    g_stats.uniformBinds++;
}

// a pass starts with Aurora's empty texture bind group (render_pass_impl's SetBindGroup(2, empty))
void bind_empty_textures() {
    if (g_cache.emptyTextures) return;
    DkResHandle handles[8];
    for (DkResHandle& h : handles) h = dkMakeTextureHandle(swdk::kEmptyImage, swdk::kSamplerNearestClamp);
    dkCmdBufBindTextures(R.cmd, DkStage_Fragment, 0, handles, 8);
    g_cache.textures = nullptr;
    g_cache.emptyTextures = true;
}

void maybe_submit() {
    if (g_submitDraws == 0 || ++g_batch.drawsSinceSubmit < g_submitDraws) return;
    g_batch.drawsSinceSubmit = 0;
    swdk::submit("draws");
    g_stats.submits++;
}

// The pipe of a draw's PipelineRef, by Aurora's policy (render_pass_impl's bind_pipeline): wait for it
// unless skip_unready_pipelines (a persistent copy's pass waits anyway), else take it if it exists.
const Pipe* lookup_pipe(uint64_t ref) {
    if (auto it = g_pipeCache.find(ref); it != g_pipeCache.end()) return it->second;
    wgpu::RenderPipeline unused;
    bool ok;
    if (!skip_unready_pipelines()) {
        ok = wait_pipeline(ref, unused);
    } else if (g_pass.requireReady) {
        ok = wait_pipeline_for_persistent_pass(ref, unused);
    } else {
        ok = try_pipeline(ref, unused);
    }
    if (!ok) return nullptr;
    const Pipe* p = find_pipe(ref);
    if (p) g_pipeCache.emplace(ref, p);
    return p;
}

bool wait_for_shaders() { return g_pass.requireReady || !skip_unready_pipelines(); }

uint32_t level_size(uint32_t size, uint32_t level) { return std::max(size >> level, 1u); }

// One GX draw of a diagnostic frame: what the GPU is told, and the bytes it reads (CPU-uncached
// staging memory: slow reads, diagnostics only). The uniform head is Aurora's GX Uniform (shader.cpp):
// words 0 vtx_start, 1 current_pnmtx, 2-3 render_viewport_size, 4-5 logical_viewport_size, 6-7 pad,
// 8-19 array_start[12], then (no line mode) proj at word 20 (mat4x4f, column by column).
void log_draw(const gx::DrawData& d, const Pipe* p, const Range& uniformRange) {
    const uint8_t* base = g_staging[g_batch.slot].block.cpu;
    if (!base) return;
    uint32_t w[36];
    memcpy(w, base + kUniformOffset + uniformRange.offset, sizeof w);
    float f[36];
    memcpy(f, w, sizeof f);
    uint16_t idx[3] = {};
    memcpy(idx, base + kIndexOffset + d.idxRange.offset, sizeof idx);
    uint32_t v[4] = {};  // the draw's first bytes of vertex data (the stride is the shader's)
    if (uint64_t(w[0]) + sizeof v <= VertexBufferSize) memcpy(v, base + kVertexOffset + w[0], sizeof v);
    const swdk::ShaderModule* m = p->module;
    const swdk::ShaderEntry* vs = m ? m->vs.load() : nullptr;
    const swdk::ShaderEntry* fs = m ? m->fs.load() : nullptr;
    dklog("diag frame %llu pass %u draw %u: pipe %016llx module %016llx (vs %016llx fs %016llx); %u indices from %u "
          "(%u %u %u) x%u; uniforms at %u; viewport %.1f %.1f %.1f %.1f z %.3f..%.3f%s; scissor %u %u %u %u%s; "
          "textures %s",
          (unsigned long long)(R.frame + 1), g_diag.passes, g_diag.draws, (unsigned long long)d.pipeline,
          (unsigned long long)(m ? m->key : 0), (unsigned long long)(vs ? vs->glslHash : 0),
          (unsigned long long)(fs ? fs->glslHash : 0), d.indexCount, uint32_t(d.idxRange.offset / sizeof(uint16_t)),
          idx[0], idx[1], idx[2], d.instanceCount, uint32_t(uniformRange.offset), g_cache.vp.x, g_cache.vp.y,
          g_cache.vp.width, g_cache.vp.height, g_cache.vp.near, g_cache.vp.far,
          g_cache.vpValid ? "" : " (stale: the pass's whole target)", g_cache.sc.x, g_cache.sc.y, g_cache.sc.width,
          g_cache.sc.height, g_cache.scValid ? "" : " (stale: the pass's whole target)",
          d.bindGroups.resolvedTextureBindGroup ? "bound" : "kept");
    dklog("diag   uniforms: vtx_start %u pnmtx %u render %.1fx%.1f logical %.1fx%.1f array_start %u %u %u %u %u %u %u "
          "%u %u %u %u %u",
          w[0], w[1], f[2], f[3], f[4], f[5], w[8], w[9], w[10], w[11], w[12], w[13], w[14], w[15], w[16], w[17],
          w[18], w[19]);
    dklog("diag   words 20-35 (proj when no line mode): %g %g %g %g | %g %g %g %g | %g %g %g %g | %g %g %g %g", f[20],
          f[21], f[22], f[23], f[24], f[25], f[26], f[27], f[28], f[29], f[30], f[31], f[32], f[33], f[34], f[35]);
    dklog("diag   vbuf at vtx_start: %08x %08x %08x %08x (GX data, big endian)", v[0], v[1], v[2], v[3]);
}

}  // namespace

// ---- staging

StagingMemory staging_create(size_t slot, uint64_t size) {
    if (slot >= kStagingSlots) swdk::fatal("staging slot %zu: Aurora has %zu", slot, kStagingSlots);
    char what[96];
    snprintf(what, sizeof what, "Aurora staging slot %zu (vertices, uniforms, indices, storage)", slot);
    g_staging[slot].block = swdk::block_create(uint32_t(size), what);
    if (!g_staging[slot].block) swdk::fatal("no memory for Aurora's staging slot %zu", slot);
    return {g_staging[slot].block.cpu, g_staging[slot].block.gpu};
}

void staging_wait(size_t slot) {
    if (slot >= kStagingSlots) return;
    swdk::fence_wait(g_staging[slot].fence);
}

DkGpuAddr uniform_address(uint32_t offset) { return g_batch.staging + kUniformOffset + offset; }

uint32_t batch_slot() { return g_batch.slot; }

// ---- batches

void batch_begin(uint32_t stagingSlot) {
    static const bool once = [] {
        g_submitDraws = uint32_t(std::max(0L, swdk::env_long("WIICOMPILED_DK_SUBMIT_DRAWS", 256)));
        g_gpuTimers = swdk::env_flag("WIICOMPILED_DK_GPU_TIMERS", true);
        g_opt.lean = swdk::env_flag("WIICOMPILED_DK_LEAN_BARRIERS", false);
        g_opt.tiled = swdk::env_flag("WIICOMPILED_DK_TILED_CACHE", true);
        g_opt.hwClears = swdk::env_flag("WIICOMPILED_DK_HW_CLEARS", true);
        dklog("encoder: partial clears %s (WIICOMPILED_DK_HW_CLEARS)",
              g_opt.hwClears ? "as 3D engine clears" : "as clear draws");
        g_opt.abFrames = uint32_t(std::clamp(swdk::env_long("WIICOMPILED_DK_AB", 0), 0L, 1L << 20));
        const long passTimes = swdk::env_long("WIICOMPILED_DK_PASS_TIMES", 0);
        g_opt.passTimes = passTimes <= 0 ? 0u : passTimes == 1 ? 600u : uint32_t(std::min(passTimes, 1L << 20));
        g_opt.drawTimes = g_opt.passTimes && swdk::env_flag("WIICOMPILED_DK_DRAW_TIMES", false);
        g_opt.pixels = g_opt.drawTimes ? uint8_t(std::clamp(swdk::env_long("WIICOMPILED_DK_DRAW_PIXELS", 0), 0L, 2L)) : 0;
        if (!g_gpuTimers) g_opt.passTimes = 0, g_opt.drawTimes = false, g_opt.pixels = 0;  // (GPU timestamps)
        dklog("encoder: Aurora's passes recorded into deko3d; submits every %u draws (WIICOMPILED_DK_SUBMIT_DRAWS), "
              "a barrier at every pass end, GPU timestamps %s (WIICOMPILED_DK_GPU_TIMERS)",
              g_submitDraws, g_gpuTimers ? "on" : "off");
        char ab[96];
        snprintf(ab, sizeof ab, "taken in turn every %u frames: off, lean, tiled, both", g_opt.abFrames);
        dklog("encoder: lean barriers %s (WIICOMPILED_DK_LEAN_BARRIERS), tiled cache %s (WIICOMPILED_DK_TILED_CACHE)%s%s; "
              "per-pass GPU times %s%u frames (WIICOMPILED_DK_PASS_TIMES), per-draw %s (WIICOMPILED_DK_DRAW_TIMES), "
              "pixel counters %s (WIICOMPILED_DK_DRAW_PIXELS)",
              g_opt.lean ? "ON" : "off", g_opt.tiled ? "ON" : "off", g_opt.abFrames ? ", A/B: " : "",
              g_opt.abFrames ? ab : "", g_opt.passTimes ? "every " : "off; ", g_opt.passTimes,
              g_opt.drawTimes ? "on" : "off",
              g_opt.pixels == 2 ? "FS invocations" : g_opt.pixels == 1 ? "samples passed" : "off");
        return true;
    }();
    (void)once;
    const uint64_t t0 = swdk::now_ns();
    const bool newFrame = !R.frameOpen;
    swdk::frame_open();
    const uint64_t f = R.frame + 1;
    if (newFrame) {
        static const bool diagOnce = [] {
            g_diag.drawLog = uint32_t(std::max(0L, swdk::env_long("WIICOMPILED_DK_DRAW_LOG", 0)));
            g_diag.shotPasses = uint32_t(std::clamp(swdk::env_long("WIICOMPILED_DK_SHOT_PASSES", 0), 0L, 32L));
            for (uint32_t i = 0; i < 32; i++) snprintf(g_passTags[i], sizeof g_passTags[i], "pass-%02u", i);
            if (g_diag.drawLog || g_diag.shotPasses)
                dklog("encoder diagnostics: %u GX draws logged and %u pass targets captured per diagnostic frame "
                      "(WIICOMPILED_DK_DRAW_LOG, WIICOMPILED_DK_SHOT_PASSES)", g_diag.drawLog, g_diag.shotPasses);
            return true;
        }();
        (void)diagOnce;
        g_diag.frame = (g_diag.drawLog || g_diag.shotPasses) &&
                       (swdk::shot_due() || (swdk::env_long("WIICOMPILED_DK_SHOT", 0) <= 0 && f % 600 == 0));
        g_diag.draws = g_diag.passes = 0;
        g_diag.lastColor = nullptr;
        if (f > swdk::kFrames) read_marks(f - swdk::kFrames);  // that frame's fence was waited for in frame_open
        // this frame's configuration: the switches, or the A/B cycle's turn
        g_config = g_opt.abFrames ? uint8_t((f / g_opt.abFrames) & 3) : uint8_t((g_opt.lean ? 1 : 0) | (g_opt.tiled ? 2 : 0));
        if (g_opt.tiled || g_opt.abFrames) swdk::tiled_cache_set((g_config & 2) != 0);
        Marks& m = g_marks[f % swdk::kFrames];
        m.count = 0;
        m.cpuNs = swdk::now_ns();
        m.config = g_config;
        m.passCount = 0;
        g_tag = Tag{};
        gpu_mark(GpuWork::FrameStart);
        log_stats();
    }
    if (g_batch.objectsFrame != f) {
        objects_frame_start();
        g_batch.objectsFrame = f;
    }
    g_batch.open = true;
    g_batch.slot = stagingSlot < kStagingSlots ? stagingSlot : 0;
    g_batch.staging = g_staging[g_batch.slot].block.gpu;
    g_batch.drawsSinceSubmit = 0;
    invalidate_state();
    // what was written into textures since the last batch: before any draw samples them. The draws of
    // the previous batch may still sample what this one's uploads overwrite: the copy engine waits for
    // them (the queue orders submissions, not engines).
    // Lean barriers: with nothing to upload, the two Full barriers order nothing but what the previous
    // frame's present (or batch) still samples, which this batch's first pass may overwrite: a Fragments
    // barrier does that. The 3D work before it is not known to be done, so copy-engine work later in this
    // batch (efb_ram's readback batches) still gets its Full barrier first.
    uint32_t uploads = 0;
    if ((g_config & 1) && !objects_uploads_pending()) {
        barrier(DkBarrier_Fragments, DkInvalidateFlags_Image);
        g_stats.leanBarriers++;
        g_batch.workSinceTransfer = true;
    } else {
        g_batch.workSinceTransfer = true;
        transfer_begin();
        uploads = objects_flush_uploads();
        transfer_end();
    }
    if (uploads) {
        g_stats.uploads += uploads;
        gpu_mark(GpuWork::Copy);
    }
    invalidate_state();  // (the clears bound render targets)
    g_stats.batches++;
    g_stats.encodeNs += swdk::now_ns() - t0;
}

void batch_end(uint32_t stagingSlot) {
    if (!g_batch.open) return;
    if (g_pass.open) pass_end();
    flush_pending();  // (lean barriers: the next batch or the present may sample a conversion target)
    g_tag = Tag{};    // (the present's and ImGui's timestamps belong to no pass)
    const uint64_t t0 = swdk::now_ns();
    swdk::fence_record(g_staging[stagingSlot < kStagingSlots ? stagingSlot : g_batch.slot].fence);
    swdk::submit("batch");
    g_stats.submits++;
    g_batch.open = false;
    g_stats.encodeNs += swdk::now_ns() - t0;
}

// ---- passes

bool pass_begin(const PassTargets& t) {
    if (g_pass.open) pass_end();
    View* colorView = find_view(t.color);
    if (!colorView || !colorView->tex->valid) {
        note_unsupported("a pass whose colour target has no deko3d image (skipped)");
        g_stats.skippedPass++;
        return false;
    }
    const uint64_t t0 = swdk::now_ns();
    DkImageView color = level_view(colorView->tex, colorView->baseMip);
    const DkImageView* colors[] = {&color};
    DkImageView depth;
    const DkImageView* depthPtr = nullptr;
    if (View* dv = find_view(t.depth); dv && dv->tex->valid) {
        depth = level_view(dv->tex, dv->baseMip);
        depthPtr = &depth;
    }
    if (colorView->tex->samples > 1) note_unsupported("a multisampled pass (drawn single-sampled; MSAA is workstream C)");
    swdk::tiled_cache_flush();
    dkCmdBufBindRenderTargets(R.cmd, colors, 1, depthPtr);
    g_pass = {true, t.width, t.height, t.requireReadyPipelines, colorView->tex, false};
    if (g_diag.frame && g_diag.drawLog) {
        dklog("diag frame %llu pass %u: colour %ux%u (level %u, format %d, %s) depth %s; load clear colour %d "
              "(%.3f %.3f %.3f %.3f) depth %d (%.3f)",
              (unsigned long long)(R.frame + 1), g_diag.passes, t.width, t.height, colorView->baseMip,
              int(colorView->tex->format), colorView->tex->label.c_str(), depthPtr ? "bound" : "none",
              int(t.clearColor), t.clearColorValue[0], t.clearColorValue[1], t.clearColorValue[2],
              t.clearColorValue[3], int(t.clearDepth), t.clearDepthValue);
    }
    // load ops: clears over the whole target
    const DkScissor full = {0, 0, t.width, t.height};
    dkCmdBufSetScissors(R.cmd, 0, &full, 1);
    g_cache.scValid = false;
    if (t.clearColor) {
        dkCmdBufClearColorFloat(R.cmd, 0, DkColorMask_RGBA, t.clearColorValue[0], t.clearColorValue[1],
                                t.clearColorValue[2], t.clearColorValue[3]);
    }
    if (t.clearDepth && depthPtr) dkCmdBufClearDepthStencil(R.cmd, true, t.clearDepthValue, 0, 0);
    // WebGPU's defaults at a pass's start: the whole target as viewport and scissor
    const DkViewport vp = {0.f, 0.f, float(t.width), float(t.height), 0.f, 1.f};
    dkCmdBufSetViewports(R.cmd, 0, &vp, 1);
    g_cache.vpValid = false;
    g_curVp = vp;
    g_curSc = full;
    g_cache.emptyTextures = false;
    bind_empty_textures();
    g_batch.workSinceTransfer = true;
    if (g_opt.passTimes) {
        Marks& m = frame_marks();
        const uint32_t index = m.passCount++;
        if (index < kMaxPasses) {
            PassRec& r = m.passes[index];
            r = PassRec{};
            r.width = t.width;
            r.height = t.height;
            r.format = int32_t(colorView->tex->format);
            r.depth = depthPtr != nullptr;
            r.loadColor = t.clearColor;
            r.loadDepth = t.clearDepth && depthPtr;
            snprintf(r.label, sizeof r.label, "%s", colorView->tex->label.c_str());
        }
        const uint8_t pass = uint8_t(std::min<uint32_t>(index, 0xFE));
        gpu_mark_tag(GpuWork::Efb, Tag{kSegLoad, pass});  // (the load clears, and what was before them)
        g_tag = Tag{kSegDraws, pass};
    }
    g_stats.encodeNs += swdk::now_ns() - t0;
    return true;
}

void pass_viewport(const webgpu::Viewport& vp) {
    if (!g_pass.open || !(vp.width > 0.f) || !(vp.height > 0.f)) return;  // (deko3d's viewport size is unsigned)
    // render_pass_impl: the GX depth range through UseReversedZ's 1 - x
    const float minDepth = gx::UseReversedZ ? 1.f - vp.zfar : vp.znear;
    const float maxDepth = gx::UseReversedZ ? 1.f - vp.znear : vp.zfar;
    const DkViewport v = {vp.left, vp.top, vp.width, vp.height, minDepth, maxDepth};
    if (changed(g_cache.vpValid, g_cache.vp, v)) dkCmdBufSetViewports(R.cmd, 0, &v, 1);
    g_curVp = v;
}

void pass_scissor(const ClipRect& sc) {
    if (!g_pass.open) return;
    // render_pass_impl's clamp
    const auto w = static_cast<int32_t>(g_pass.width), h = static_cast<int32_t>(g_pass.height);
    const int32_t left = std::clamp(sc.x, 0, w);
    const int32_t top = std::clamp(sc.y, 0, h);
    const int32_t right = std::clamp(sc.x + sc.width, left, w);
    const int32_t bottom = std::clamp(sc.y + sc.height, top, h);
    const DkScissor s = {uint32_t(left), uint32_t(top), uint32_t(right - left), uint32_t(bottom - top)};
    if (changed(g_cache.scValid, g_cache.sc, s)) dkCmdBufSetScissors(R.cmd, 0, &s, 1);
    g_curSc = s;
}

void draw_gx(const gx::DrawData& d, const Range* uniformOverride) {
    if (!g_pass.open) return;
    const uint64_t t0 = swdk::now_ns();
    const Pipe* p = lookup_pipe(d.pipeline);
    if (!p) {
        g_stats.skippedPipeline++;
        return;
    }
    if (!pipe_ready(p, wait_for_shaders())) {
        g_stats.skippedShader++;
        return;
    }
    if (d.indexCount == 0) return;
    ensure_globals();
    const Range& uniformRange = uniformOverride != nullptr ? *uniformOverride : d.uniformRange;
    bind_uniforms(uniform_address(uniformRange.offset), gx::MaxUniformSize);
    // resolved when the draw was recorded; without one the draw keeps the textures bound before, as in
    // the WebGPU pass
    if (d.bindGroups.resolvedTextureBindGroup != nullptr && d.bindGroups.resolvedTextureBindGroup != g_cache.textures) {
        if (BindGroup* g = find_bind_group(d.bindGroups.resolvedTextureBindGroup)) {
            // (lean barriers: a conversion target written since the last image barrier is sampled here; a
            // group bound before that conversion was rebound since: passes start with the empty textures)
            if (!g_pending.empty()) {
                for (const BindGroup::Entry& e : g->entries) {
                    if (e.view && is_pending(e.view->tex)) {
                        barrier(DkBarrier_Fragments, DkInvalidateFlags_Image);
                        g_stats.pendingFlushes++;
                        break;
                    }
                }
            }
            dkCmdBufBindTextures(R.cmd, DkStage_Fragment, 0, gx_handles(g), 8);
            g_stats.textureBinds++;
            g_cache.textures = d.bindGroups.resolvedTextureBindGroup;
            g_cache.emptyTextures = false;
        }
    }
    bind_pipe(p);
    if (d.dstAlpha != UINT32_MAX) set_blend_const(0.f, 0.f, 0.f, float(d.dstAlpha) / 255.f);
    // first index from the draw's range in the index region; vertex offset and first instance 0, so
    // gl_VertexID / gl_InstanceID are WebGPU's indices
    dkCmdBufDrawIndexed(R.cmd, DkPrimitive_Triangles, d.indexCount, d.instanceCount,
                        d.idxRange.offset / uint32_t(sizeof(uint16_t)), 0, 0);
    g_pass.drawn = true;
    if (g_diag.frame && g_diag.draws < g_diag.drawLog) log_draw(d, p, uniformRange);
    g_diag.draws++;
    g_stats.draws++;
    if (g_opt.passTimes) {
        if (PassRec* r = open_pass_rec()) r->gxDraws++;
        g_drawsSinceMark++;
        // (a reserve of marks kept for the passes' own and the present's)
        if (g_opt.drawTimes && frame_marks().count + 256 < kMaxMarks) {
            // the raster rectangle (viewport and scissor as the GPU has them) and the textures bound
            DrawInfo info;
            const float vx0 = std::max(g_curVp.x, 0.f), vy0 = std::max(g_curVp.y, 0.f);
            const float vx1 = std::min(g_curVp.x + g_curVp.width, float(g_pass.width));
            const float vy1 = std::min(g_curVp.y + g_curVp.height, float(g_pass.height));
            const float x0 = std::max(vx0, float(g_curSc.x)), y0 = std::max(vy0, float(g_curSc.y));
            const float x1 = std::min(vx1, float(g_curSc.x + g_curSc.width));
            const float y1 = std::min(vy1, float(g_curSc.y + g_curSc.height));
            if (x1 > x0 && y1 > y0) {
                info.area = uint32_t((x1 - x0) * (y1 - y0));
                if (PassRec* r = open_pass_rec()) {
                    r->drawX0 = std::min(r->drawX0, uint32_t(x0));
                    r->drawY0 = std::min(r->drawY0, uint32_t(y0));
                    r->drawX1 = std::max(r->drawX1, uint32_t(x1 + 0.999f));
                    r->drawY1 = std::max(r->drawY1, uint32_t(y1 + 0.999f));
                }
            }
            if (g_cache.textures) {
                if (BindGroup* g = find_bind_group(g_cache.textures)) {
                    for (const BindGroup::Entry& e : g->entries) {
                        if (!e.view || !e.view->tex) continue;
                        if (!info.texCount)
                            info.texWH = std::min<uint32_t>(e.view->tex->width, 0xFFFF) << 16 |
                                         std::min<uint32_t>(e.view->tex->height, 0xFFFF);
                        info.texCount++;
                    }
                }
            }
            gpu_mark_tag(GpuWork::Efb, g_tag, p, d.indexCount * std::max<uint32_t>(d.instanceCount, 1), info);
        }
    }
    g_batch.workSinceTransfer = true;
    g_stats.encodeNs += swdk::now_ns() - t0;
    maybe_submit();
}

void draw_clear(const clear::DrawData& d) {
    if (!g_pass.open) return;
    const Pipe* p = lookup_pipe(d.pipeline);
    if (!p) {
        g_stats.skippedPipeline++;
        return;
    }
    if (!pipe_ready(p, false)) {
        g_stats.skippedShader++;
        return;
    }
    // clear::render: the whole target as viewport (depth 0..1, the clear's frag depth is the uniform),
    // the clear's scissor or the whole target
    const uint32_t tw = g_pass.width, th = g_pass.height;
    DkScissor s = {0, 0, tw, th};
    if (d.useScissor) {
        const auto clampi = [](int32_t v, uint32_t max) { return uint32_t(std::clamp<int32_t>(v, 0, int32_t(max))); };
        const uint32_t x = clampi(d.scissor.x, tw), y = clampi(d.scissor.y, th);
        const uint32_t right = clampi(d.scissor.x + d.scissor.width, tw);
        const uint32_t bottom = clampi(d.scissor.y + d.scissor.height, th);
        if (right <= x || bottom <= y) return;
        s = {x, y, right - x, bottom - y};
    }
    // (pass times: the GX draws before this clear end there, the clear draw at its own mark)
    if (g_opt.passTimes && g_drawsSinceMark) gpu_mark_tag(GpuWork::Efb, g_tag);
    if (g_opt.hwClears && p->clearMask) {
        // WIICOMPILED_DK_HW_CLEARS: the 3D engine's clears in the scissor, with every channel's write and
        // the depth write on so that no state bound before can mask them (after draws of this pass: the
        // tiled cache flushed first, as before the load clears)
        if (g_pass.drawn) swdk::tiled_cache_flush();
        dkCmdBufSetScissors(R.cmd, 0, &s, 1);
        g_cache.scValid = false;
        g_curSc = s;
        if (const uint32_t colorMask = p->clearMask & 0xFu) {
            DkColorWriteState cw;
            dkColorWriteStateDefaults(&cw);
            dkCmdBufBindColorWriteState(R.cmd, &cw);
            g_cache.cwValid = false;
            dkCmdBufClearColorFloat(R.cmd, 0, colorMask, float(d.color.r), float(d.color.g), float(d.color.b),
                                    float(d.color.a));
        }
        if (p->clearMask & 0x10u) {
            DkDepthStencilState ds;
            dkDepthStencilStateDefaults(&ds);
            ds.depthTestEnable = true;
            ds.depthWriteEnable = true;
            ds.depthCompareOp = DkCompareOp_Always;
            dkCmdBufBindDepthStencilState(R.cmd, &ds);
            g_cache.dsValid = false;
            dkCmdBufClearDepthStencil(R.cmd, true, std::clamp(d.depth, 0.f, 1.f), 0, 0);
        }
        g_cache.pipe = nullptr;  // (its write and depth states were replaced)
        g_stats.clears++;
        g_batch.workSinceTransfer = true;
        if (g_opt.passTimes) {
            if (PassRec* r = open_pass_rec()) {
                r->clearDraws++;
                r->clearPixels += s.width * s.height;
            }
            gpu_mark_tag(GpuWork::Efb, Tag{kSegClears, g_tag.pass});
        }
        return;
    }
    ensure_globals();
    bind_pipe(p);
    set_blend_const(float(d.color.r), float(d.color.g), float(d.color.b), float(d.color.a));
    bind_uniforms(uniform_address(d.uniformRange.offset), std::max<uint32_t>((d.uniformRange.size + 15u) & ~15u, 16u));
    const DkViewport v = {0.f, 0.f, float(tw), float(th), 0.f, 1.f};
    dkCmdBufSetViewports(R.cmd, 0, &v, 1);
    g_cache.vpValid = false;
    dkCmdBufSetScissors(R.cmd, 0, &s, 1);
    g_cache.scValid = false;
    g_curVp = v;
    g_curSc = s;
    dkCmdBufDraw(R.cmd, DkPrimitive_Triangles, 3, 1, 0, 0);
    g_pass.drawn = true;
    g_stats.clears++;
    g_batch.workSinceTransfer = true;
    if (g_opt.passTimes) {
        if (PassRec* r = open_pass_rec()) {
            r->clearDraws++;
            r->clearPixels += s.width * s.height;
        }
        gpu_mark_tag(GpuWork::Efb, Tag{kSegClears, g_tag.pass});
    }
}

void pass_end() {
    if (!g_pass.open) return;
    // later passes and copies read what this one wrote
    barrier(DkBarrier_Fragments, DkInvalidateFlags_Image);
    gpu_mark(GpuWork::Efb);
    if (g_opt.passTimes) g_tag = Tag{kSegCopy, g_tag.pass};  // (its EFB copy follows)
    g_passEnded = true;
    if (g_diag.frame) {
        Tex* c = g_pass.color;
        const bool rgba = c && (c->format == DkImageFormat_RGBA8_Unorm || c->format == DkImageFormat_BGRA8_Unorm);
        if (g_diag.shotPasses && g_diag.passes < g_diag.shotPasses && rgba && swdk::shot_due()) {
            transfer_begin();
            swdk::shot_record(c->image, c->width, c->height, g_passTags[g_diag.passes]);
            transfer_end();
        }
        if (g_diag.drawLog)
            dklog("diag frame %llu pass %u ends after %u GX draws of the frame", (unsigned long long)(R.frame + 1),
                  g_diag.passes, g_diag.draws);
    }
    g_diag.lastColor = g_pass.color;
    g_diag.passes++;
    g_stats.passes++;
    g_pass.open = false;
}

void copy_texture(WGPUTexture srcHandle, uint32_t sx, uint32_t sy, WGPUTexture dstHandle, uint32_t w, uint32_t h) {
    Tex* src = find_texture(srcHandle);
    Tex* dst = find_texture(dstHandle);
    if (!src || !dst || !src->valid || !dst->valid) return;
    if (src->format != dst->format && src->blockBytes != dst->blockBytes) {
        note_unsupported("an EFB copy between formats of different sizes (workstream C: conversions)");
        return;
    }
    const uint32_t sw = level_size(src->width, 0), shh = level_size(src->height, 0);
    const uint32_t dw = level_size(dst->width, 0), dh = level_size(dst->height, 0);
    if (sx >= sw || sy >= shh) return;
    w = std::min({w, sw - sx, dw});
    h = std::min({h, shh - sy, dh});
    if (w == 0 || h == 0) return;
    uint32_t dx = 0, dy = 0;
    if (src->compressed) {
        // dkCmdBufCopyImage takes compressed rectangles in blocks (SwitchWakerHD surfaces.cpp)
        w = (w + src->blockW - 1) / src->blockW;
        h = (h + src->blockH - 1) / src->blockH;
        sx /= src->blockW;
        sy /= src->blockH;
    }
    transfer_begin();
    const DkImageView sv = level_view(src, 0), dv = level_view(dst, 0);
    const DkImageRect sr = {sx, sy, 0, w, h, 1}, dr = {dx, dy, 0, w, h, 1};
    dkCmdBufCopyImage(R.cmd, &sv, &sr, &dv, &dr, 0);
    transfer_end();
    g_stats.copies++;
    gpu_mark(GpuWork::Copy);
}

// ---- shared with the present and workstream C

void invalidate_state() { g_cache = Cache{}; }

void transfer_begin() {
    if (g_batch.workSinceTransfer) barrier(DkBarrier_Full, DkInvalidateFlags_Image);
    g_batch.workSinceTransfer = false;
}

void transfer_end() { barrier(DkBarrier_Full, DkInvalidateFlags_Image); }

void note_work() { g_batch.workSinceTransfer = true; }

Tex* diag_last_pass_target() { return g_diag.frame && g_diag.shotPasses ? g_diag.lastColor : nullptr; }

void gpu_mark(GpuWork work) { gpu_mark_tag(work, g_tag); }

void conversion_barrier(Tex* dst) {
    if (!(g_config & 1) || !dst) {
        barrier(DkBarrier_Fragments, DkInvalidateFlags_Image);
        return;
    }
    // the next pass overwrites what the conversion sampled once its fragments are done; whoever samples
    // dst before the next image barrier gets one first
    barrier(DkBarrier_Fragments, 0);
    g_stats.leanBarriers++;
    if (!is_pending(dst)) g_pending.push_back(dst);
}

void before_sampling(const Tex* t) {
    if (t && !g_pending.empty() && is_pending(t)) {
        barrier(DkBarrier_Fragments, DkInvalidateFlags_Image);
        g_stats.pendingFlushes++;
    }
}

void flush_pending() {
    if (g_pending.empty()) return;
    barrier(DkBarrier_Fragments, DkInvalidateFlags_Image);
    g_stats.pendingFlushes++;
}

void pass_prologue() {
    g_passEnded = false;
    if (g_opt.passTimes) {
        // (the palette conversions before the pass the walk is at: the next index)
        const uint32_t next = frame_marks().passCount;
        g_tag = Tag{kSegPalette, uint8_t(std::min<uint32_t>(next, 0xFE))};
    }
}

void note_pass_copy(uint8_t kind, uint32_t width, uint32_t height, const ClipRect& source) {
    if (!g_opt.passTimes || !g_passEnded) return;
    if (PassRec* r = open_pass_rec()) {
        r->copyKind = kind;
        r->copyW = width;
        r->copyH = height;
        r->srcX = source.x;
        r->srcY = source.y;
        r->srcW = source.width;
        r->srcH = source.height;
    }
}

EncodeStats& encode_stats() { return g_stats; }

}  // namespace aurora::gfx::dk
