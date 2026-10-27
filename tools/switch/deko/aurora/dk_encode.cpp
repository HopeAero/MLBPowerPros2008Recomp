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
// ends with a Fragments barrier that also invalidates the texture cache, and is followed by its EFB
// copy: the copy engine for an exact copy, workstream C's conversions otherwise. Copy-engine work
// (uploads, copies) sits between Full barriers.
//
// State cache. Pipeline state, shaders, viewport, scissor, blend constant, uniform offset, texture
// handles are bound only when they change within a batch; anything that binds its own state (the
// present, ImGui, workstream C's fixed draws) resets it.
//
// The command list goes to the GPU every WIICOMPILED_DK_SUBMIT_DRAWS draws (default 256, 0 = once
// per batch), as SwitchWakerHD's, so the GPU starts on a frame while it is still being recorded.
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

// ---- GPU timestamps: every frame, at its first batch, the end of every pass and copy, the present
// and ImGui; read when the frame's slot comes round again (its fence waited for in frame_open)
constexpr uint32_t kMaxMarks = swdk::kQuerySize / swdk::kFrames / 16;
struct Marks {
    GpuWork work[kMaxMarks];
    uint32_t count = 0;
    uint64_t cpuNs = 0;  // when the frame was opened
};
Marks g_marks[swdk::kFrames];
bool g_gpuTimers = true;
uint64_t g_statsStart = 0;

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

void read_marks(uint64_t frame) {
    Marks& m = g_marks[frame % swdk::kFrames];
    if (m.count >= 2) {
        const swdk::QueryRegion q = swdk::query_region(frame);
        uint64_t ts[kMaxMarks];
        for (uint32_t i = 0; i < m.count; i++) memcpy(&ts[i], q.cpu + size_t(i) * 16 + 8, 8);
        g_clock.sample(m.cpuNs, dkTimestampToNs(ts[0]));
        bool ok = true;
        for (uint32_t i = 1; i < m.count && ok; i++) ok = ts[i] >= ts[i - 1];
        if (ok) {
            for (uint32_t i = 1; i < m.count; i++)
                g_stats.gpuNs[size_t(m.work[i])] += uint64_t(double(dkTimestampToNs(ts[i] - ts[i - 1])) * g_clock.factor);
            g_stats.gpuTotalNs += uint64_t(double(dkTimestampToNs(ts[m.count - 1] - ts[0])) * g_clock.factor);
            g_stats.gpuFrames++;
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
    last = s;
    g_statsStart = now;
}

void barrier(DkBarrier mode, uint32_t invalidate) {
    dkCmdBufBarrier(R.cmd, mode, invalidate);
    g_stats.barriers++;
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
        dklog("encoder: Aurora's passes recorded into deko3d; submits every %u draws (WIICOMPILED_DK_SUBMIT_DRAWS), "
              "a barrier at every pass end, GPU timestamps %s (WIICOMPILED_DK_GPU_TIMERS)",
              g_submitDraws, g_gpuTimers ? "on" : "off");
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
        g_marks[f % swdk::kFrames].count = 0;
        g_marks[f % swdk::kFrames].cpuNs = swdk::now_ns();
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
    g_batch.workSinceTransfer = true;
    transfer_begin();
    const uint32_t uploads = objects_flush_uploads();
    transfer_end();
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
    dkCmdBufBindRenderTargets(R.cmd, colors, 1, depthPtr);
    g_pass = {true, t.width, t.height, t.requireReadyPipelines, colorView->tex};
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
    g_cache.emptyTextures = false;
    bind_empty_textures();
    g_batch.workSinceTransfer = true;
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
    if (g_diag.frame && g_diag.draws < g_diag.drawLog) log_draw(d, p, uniformRange);
    g_diag.draws++;
    g_stats.draws++;
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
    ensure_globals();
    bind_pipe(p);
    set_blend_const(float(d.color.r), float(d.color.g), float(d.color.b), float(d.color.a));
    bind_uniforms(uniform_address(d.uniformRange.offset), std::max<uint32_t>((d.uniformRange.size + 15u) & ~15u, 16u));
    const DkViewport v = {0.f, 0.f, float(tw), float(th), 0.f, 1.f};
    dkCmdBufSetViewports(R.cmd, 0, &v, 1);
    g_cache.vpValid = false;
    dkCmdBufSetScissors(R.cmd, 0, &s, 1);
    g_cache.scValid = false;
    dkCmdBufDraw(R.cmd, DkPrimitive_Triangles, 3, 1, 0, 0);
    g_stats.clears++;
    g_batch.workSinceTransfer = true;
}

void pass_end() {
    if (!g_pass.open) return;
    // later passes and copies read what this one wrote
    barrier(DkBarrier_Fragments, DkInvalidateFlags_Image);
    gpu_mark(GpuWork::Efb);
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

void gpu_mark(GpuWork work) {
    if (!g_gpuTimers || !R.frameOpen) return;
    const uint64_t frame = R.frame + 1;
    Marks& m = g_marks[frame % swdk::kFrames];
    if (m.count >= kMaxMarks) {
        g_stats.gpuDropped++;
        return;
    }
    const swdk::QueryRegion q = swdk::query_region(frame);
    dkCmdBufReportCounter(R.cmd, DkCounter_Timestamp, q.gpu + uint64_t(m.count) * 16);
    m.work[m.count++] = work;
}

EncodeStats& encode_stats() { return g_stats; }

}  // namespace aurora::gfx::dk
