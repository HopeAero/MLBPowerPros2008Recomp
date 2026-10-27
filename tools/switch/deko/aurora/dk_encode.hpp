// SPDX-License-Identifier: MIT
// From SwitchWaker (switch/deko/aurora/dk_encode.hpp at 56c0758; LICENSE.SwitchWaker). Modified for MLB
// Power Pros 2008 Recomp (tools/switch/deko, 2026-10-26): the encoder's own helpers for the present
// (dk_present.cpp) and workstream C's conversions and readbacks (dk_copy_conv.cpp, dk_depth_peek.cpp,
// dk_efb_ram.cpp).
#pragma once
// What the encoder (dk_encode.cpp) shares with the present and with workstream C: the batch's staging
// addresses, the state cache's reset, copy-engine barriers, the GPU timestamps and the counters.
#include <deko3d.h>

#include <cstdint>

#include "dk_objects.hpp"
#include "dk_pipeline.hpp"

namespace aurora::gfx::dk {

// The GPU address of a range of the current batch's uniform region (Aurora's push_uniform offsets:
// the resolve uniforms of tex_copy_conv, the clear depth)
DkGpuAddr uniform_address(uint32_t offset);
// The staging slot of the open batch, or of the last one (efb_ram's readbacks run as a batch of their
// own on it: the native blit's uniforms are there)
uint32_t batch_slot();
// the encoder's view of the command buffer state is lost (another pass bound its own state)
void invalidate_state();
// Copy-engine work (dkCmdBufCopyImage, CopyBufferToImage, CopyImageToBuffer) between transfer_begin and
// transfer_end: Full barriers, so it neither overtakes the draws before it nor is overtaken by the
// draws after it
void transfer_begin();
void transfer_end();
// a draw or a render-target write since the last transfer (the next transfer_begin waits for it)
void note_work();
// WIICOMPILED_DK_SHOT_PASSES on a shot frame: the frame's last pass target (null otherwise)
Tex* diag_last_pass_target();

// GPU time per frame, split by the kind of work that ends at each timestamp
enum class GpuWork : uint8_t { FrameStart, Efb, Copy, Conversion, Present, Imgui, Other, Count };
void gpu_mark(GpuWork work);

// counters (running totals; logged every 30 s)
struct EncodeStats {
    uint64_t batches = 0, passes = 0, draws = 0, clears = 0, skippedPipeline = 0, skippedShader = 0, skippedPass = 0;
    uint64_t pipelineBinds = 0, shaderBinds = 0, textureBinds = 0, uniformBinds = 0;
    uint64_t copies = 0, conversions = 0, barriers = 0, submits = 0, uploads = 0;
    uint64_t encodeNs = 0;
    uint64_t gpuFrames = 0, gpuNs[size_t(GpuWork::Count)] = {}, gpuTotalNs = 0, gpuDropped = 0;
};
EncodeStats& encode_stats();

}  // namespace aurora::gfx::dk
