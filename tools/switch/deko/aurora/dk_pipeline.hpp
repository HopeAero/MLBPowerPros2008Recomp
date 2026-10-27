// SPDX-License-Identifier: MIT
// From SwitchWaker (switch/deko/aurora/dk_pipeline.hpp at 56c0758; LICENSE.SwitchWaker). Modified for MLB
// Power Pros 2008 Recomp (tools/switch/deko, 2026-10-26): a pipe holds its GX module (both stages, by the
// hash of the module's WGSL) or the renderer's own clear shaders, and is found by Aurora's PipelineRef.
#pragma once
// A deko3d pipeline of the Aurora side (dk_gfx.hpp create_gx_pipeline, create_clear_pipeline): the
// shaders and the fixed state that dkCmdBufBind*State takes, made on Aurora's pipeline compile threads
// and bound by the encoder (dk_encode.cpp) through its state cache. Pipes live as long as the process
// (Aurora's pipeline cache keeps every pipeline of the session).
#include <deko3d.h>

#include <cstdint>

#include "dk.h"

namespace aurora::gfx::dk {

struct Pipe {
    swdk::ShaderModule* module = nullptr;  // a GX pipeline's two stages
    const DkShader* fixedVs = nullptr;     // or the renderer's own shaders (clear)
    const DkShader* fixedFs = nullptr;
    DkRasterizerState rasterizer{};
    DkColorState color{};
    DkColorWriteState colorWrite{};
    DkBlendState blend{};
    DkDepthStencilState depthStencil{};
    uint64_t ref = 0;  // Aurora's PipelineRef
};

// The pipe registered for Aurora's PipelineRef, or null (not created yet). Any thread.
const Pipe* find_pipe(uint64_t ref);

// Command buffer owner: both stages are loaded (a compiled one is loaded now if the frame's budget
// allows, or always with wait). wait: a queued module is moved up and waited for. False: the draw is
// skipped.
bool pipe_ready(const Pipe* p, bool wait);
// the stages of a ready pipe
const DkShader* pipe_vs(const Pipe* p);
const DkShader* pipe_fs(const Pipe* p);

struct PipelineStats {
    uint64_t gx = 0, clear = 0, modulesNew = 0;
};
PipelineStats pipeline_stats();

}  // namespace aurora::gfx::dk
