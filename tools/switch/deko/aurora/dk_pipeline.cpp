// SPDX-License-Identifier: MIT
// From SwitchWaker (switch/deko/aurora/dk_pipeline.cpp at 56c0758; LICENSE.SwitchWaker). Modified for MLB
// Power Pros 2008 Recomp (tools/switch/deko, 2026-10-26): our Aurora's PipelineConfig (one colour target,
// no prepass, no dual-source blending, no depth bias), modules keyed by the XXH3 of their WGSL and
// translated on the uam worker (Tint is not called on the compile threads), pipes registered by
// Aurora's PipelineRef, clear pipelines with the renderer's own GLSL shaders.
// deko3d pipelines for Aurora (dk_pipeline.hpp), on Aurora's pipeline compile threads (and the producer
// for the synchronous clear pipelines). A GX pipeline is a module plus build_pipeline's fixed state:
//   - the module is the WGSL gx::build_shader_source generates for the config's ShaderConfig, known by
//     its hash: the SD card's cache maps it to its two DKSH stages, or the uam worker translates and
//     compiles it (background: the warm-up of Aurora's initial_pipeline_cache.db queues every cached
//     config; a draw that needs it moves it up), so the compile threads never wait for uam;
//   - the state is gx::pipeline_state_desc's (the WebGPU values build_pipeline gives Dawn), converted.
// Clear pipelines (gfx/clear.cpp) use the renderer's own clear shaders (core/fixed_shaders.cpp).
#include "dk_pipeline.hpp"

#include "dk_gfx.hpp"

#include "gfx/clear.hpp"
#include "gfx/common.hpp"
#include "gx/pipeline.hpp"

#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>

namespace aurora::gfx::dk {
using swdk::dklog;
using swdk::ShaderStage;

namespace {

std::mutex g_mutex;
std::unordered_map<uint64_t, Pipe*> g_pipes;
PipelineStats g_stats;

DkCompareOp compare_op(wgpu::CompareFunction f) {
    switch (f) {
    case wgpu::CompareFunction::Never: return DkCompareOp_Never;
    case wgpu::CompareFunction::Less: return DkCompareOp_Less;
    case wgpu::CompareFunction::Equal: return DkCompareOp_Equal;
    case wgpu::CompareFunction::LessEqual: return DkCompareOp_Lequal;
    case wgpu::CompareFunction::Greater: return DkCompareOp_Greater;
    case wgpu::CompareFunction::NotEqual: return DkCompareOp_NotEqual;
    case wgpu::CompareFunction::GreaterEqual: return DkCompareOp_Gequal;
    default: return DkCompareOp_Always;
    }
}

DkBlendFactor blend_factor(wgpu::BlendFactor f, bool alpha) {
    switch (f) {
    case wgpu::BlendFactor::Zero: return DkBlendFactor_Zero;
    case wgpu::BlendFactor::One: return DkBlendFactor_One;
    case wgpu::BlendFactor::Src: return DkBlendFactor_SrcColor;
    case wgpu::BlendFactor::OneMinusSrc: return DkBlendFactor_InvSrcColor;
    case wgpu::BlendFactor::SrcAlpha: return DkBlendFactor_SrcAlpha;
    case wgpu::BlendFactor::OneMinusSrcAlpha: return DkBlendFactor_InvSrcAlpha;
    case wgpu::BlendFactor::Dst: return DkBlendFactor_DstColor;
    case wgpu::BlendFactor::OneMinusDst: return DkBlendFactor_InvDstColor;
    case wgpu::BlendFactor::DstAlpha: return DkBlendFactor_DstAlpha;
    case wgpu::BlendFactor::OneMinusDstAlpha: return DkBlendFactor_InvDstAlpha;
    case wgpu::BlendFactor::SrcAlphaSaturated: return DkBlendFactor_SrcAlphaSaturate;
    // WebGPU's constant is the blend constant's rgb for the colour, its a for the alpha
    case wgpu::BlendFactor::Constant: return alpha ? DkBlendFactor_ConstAlpha : DkBlendFactor_ConstColor;
    case wgpu::BlendFactor::OneMinusConstant: return alpha ? DkBlendFactor_InvConstAlpha : DkBlendFactor_InvConstColor;
    case wgpu::BlendFactor::Src1: return DkBlendFactor_Src1Color;
    case wgpu::BlendFactor::OneMinusSrc1: return DkBlendFactor_InvSrc1Color;
    case wgpu::BlendFactor::Src1Alpha: return DkBlendFactor_Src1Alpha;
    case wgpu::BlendFactor::OneMinusSrc1Alpha: return DkBlendFactor_InvSrc1Alpha;
    default: return DkBlendFactor_One;
    }
}

DkBlendOp blend_op(wgpu::BlendOperation o) {
    switch (o) {
    case wgpu::BlendOperation::Subtract: return DkBlendOp_Sub;
    case wgpu::BlendOperation::ReverseSubtract: return DkBlendOp_RevSub;
    case wgpu::BlendOperation::Min: return DkBlendOp_Min;
    case wgpu::BlendOperation::Max: return DkBlendOp_Max;
    default: return DkBlendOp_Add;
    }
}

// zeroed first: the encoder compares states byte for byte
Pipe* new_pipe(uint64_t ref) {
    auto* p = new Pipe;
    memset(&p->rasterizer, 0, sizeof p->rasterizer);
    memset(&p->color, 0, sizeof p->color);
    memset(&p->colorWrite, 0, sizeof p->colorWrite);
    memset(&p->blend, 0, sizeof p->blend);
    memset(&p->depthStencil, 0, sizeof p->depthStencil);
    dkRasterizerStateDefaults(&p->rasterizer);
    dkColorStateDefaults(&p->color);
    dkColorWriteStateDefaults(&p->colorWrite);
    dkBlendStateDefaults(&p->blend);
    dkDepthStencilStateDefaults(&p->depthStencil);
    p->ref = ref;
    return p;
}

void set_blend(Pipe* p, const wgpu::BlendState& b) {
    dkBlendStateSetOps(&p->blend, blend_op(b.color.operation), blend_op(b.alpha.operation));
    dkBlendStateSetFactors(&p->blend, blend_factor(b.color.srcFactor, false), blend_factor(b.color.dstFactor, false),
                           blend_factor(b.alpha.srcFactor, true), blend_factor(b.alpha.dstFactor, true));
}

void register_pipe(Pipe* p) {
    std::lock_guard<std::mutex> lock(g_mutex);
    auto [it, inserted] = g_pipes.emplace(p->ref, p);
    if (!inserted) {
        delete p;  // (made twice by two compile threads: the first one stays)
        return;
    }
    (p->module ? g_stats.gx : g_stats.clear)++;
}

}  // namespace

const Pipe* find_pipe(uint64_t ref) {
    std::lock_guard<std::mutex> lock(g_mutex);
    auto it = g_pipes.find(ref);
    return it == g_pipes.end() ? nullptr : it->second;
}

bool pipe_ready(const Pipe* p, bool wait) {
    if (!p) return false;
    if (!p->module) return p->fixedVs && p->fixedFs;
    swdk::ShaderModule* m = p->module;
    if (wait) swdk::module_wait(m);
    if (swdk::module_usable(m, wait)) return true;
    // the first draw that misses it moves its compile ahead of the warm-up's
    if (m->skippedDraws.load(std::memory_order_relaxed) == 1) swdk::module_compile(m, {}, true);
    return false;
}

const DkShader* pipe_vs(const Pipe* p) { return p->module ? &p->module->vs.load()->shader : p->fixedVs; }
const DkShader* pipe_fs(const Pipe* p) { return p->module ? &p->module->fs.load()->shader : p->fixedFs; }

void create_gx_pipeline(const gx::PipelineConfig& config) {
    const uint64_t ref = xxh3_hash(config, static_cast<HashType>(ShaderType::GX));
    if (find_pipe(ref)) return;
    // the module: by the hash of its WGSL (the cache's key); a new one is compiled in the background
    const std::string wgsl = gx::build_shader_source(config.shaderConfig);
    swdk::ShaderModule* m = swdk::shader_module(XXH3_64bits(wgsl.data(), wgsl.size()));
    if (m->state.load(std::memory_order_acquire) == swdk::ModuleState::Unresolved) {
        const gx::ShaderConfig shaderConfig = config.shaderConfig;
        swdk::module_compile(m, [shaderConfig] { return gx::build_shader_source(shaderConfig); }, false);
        std::lock_guard<std::mutex> lock(g_mutex);
        g_stats.modulesNew++;
    }
    // the fixed state: build_pipeline's
    const gx::PipelineStateDesc d = gx::pipeline_state_desc(config);
    Pipe* p = new_pipe(ref);
    p->module = m;
    DkRasterizerState& rs = p->rasterizer;
    rs.cullMode = d.primitive.cullMode == wgpu::CullMode::Front  ? DkFace_Front
                  : d.primitive.cullMode == wgpu::CullMode::Back ? DkFace_Back
                                                                 : DkFace_None;
    const bool ccw = d.primitive.frontFace == wgpu::FrontFace::CCW;
    rs.frontFace = ccw != swdk::C.flipFront ? DkFrontFace_CCW : DkFrontFace_CW;
    // (WebGPU tests depth whenever the pipeline has a depth attachment, Always when GX's compare is off;
    // deko3d writes depth only with the test on)
    DkDepthStencilState& ds = p->depthStencil;
    ds.depthTestEnable = true;
    ds.depthWriteEnable = d.depthStencil.depthWriteEnabled == wgpu::OptionalBool::True;
    ds.depthCompareOp = compare_op(d.depthStencil.depthCompare);
    // isolation switches for one console session (off by default; logged once): GX draws without
    // culling (WIICOMPILED_DK_GX_NO_CULL=1) or without the depth test (WIICOMPILED_DK_GX_DEPTH_ALWAYS=1)
    static const bool noCull = swdk::env_flag("WIICOMPILED_DK_GX_NO_CULL", false);
    static const bool depthAlways = swdk::env_flag("WIICOMPILED_DK_GX_DEPTH_ALWAYS", false);
    static const bool logged = [] {
        if (noCull || depthAlways)
            dklog("pipelines: GX draws%s%s (isolation switches)", noCull ? " without culling" : "",
                  depthAlways ? " with depth compare Always" : "");
        return true;
    }();
    (void)logged;
    if (noCull) rs.cullMode = DkFace_None;
    if (depthAlways) ds.depthCompareOp = DkCompareOp_Always;
    dkColorStateSetBlendEnable(&p->color, 0, true);
    dkColorWriteStateSetMask(&p->colorWrite, 0, uint32_t(d.writeMask) & 0xF);
    set_blend(p, d.blend);
    register_pipe(p);
}

void create_clear_pipeline(const clear::PipelineConfig& config) {
    const uint64_t ref = xxh3_hash(config, static_cast<HashType>(ShaderType::Clear));
    if (find_pipe(ref)) return;
    Pipe* p = new_pipe(ref);
    p->fixedVs = swdk::builtin_shader(swdk::kClearVs);
    p->fixedFs = swdk::builtin_shader(swdk::kClearFs);
    if (!p->fixedVs || !p->fixedFs) note_unsupported("partial clears (the clear shaders did not compile)");
    p->rasterizer.cullMode = DkFace_None;
    p->depthStencil.depthTestEnable = true;
    p->depthStencil.depthWriteEnable = config.clearDepth;
    p->depthStencil.depthCompareOp = DkCompareOp_Always;
    // clear.cpp: the blend constant is the colour (Constant, Zero); the write mask picks the channels
    dkColorStateSetBlendEnable(&p->color, 0, true);
    dkBlendStateSetOps(&p->blend, DkBlendOp_Add, DkBlendOp_Add);
    dkBlendStateSetFactors(&p->blend, DkBlendFactor_ConstColor, DkBlendFactor_Zero, DkBlendFactor_ConstAlpha,
                           DkBlendFactor_Zero);
    const uint32_t mask = (config.clearColor ? uint32_t(DkColorMask_RGB) : 0u) |
                          (config.clearAlpha ? uint32_t(DkColorMask_A) : 0u);
    dkColorWriteStateSetMask(&p->colorWrite, 0, mask);
    register_pipe(p);
}

PipelineStats pipeline_stats() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_stats;
}

}  // namespace aurora::gfx::dk
