// SPDX-License-Identifier: MIT
// From SwitchWaker (switch/deko/aurora/dk_gfx.hpp at 56c0758; LICENSE.SwitchWaker), rewritten for MLB Power
// Pros 2008 Recomp (tools/switch/deko, 2026-10-26): our Aurora (the Wiicompiled fork) records RenderPass /
// Command lists in gfx/common.cpp, not upstream's FramePacket, and the renderer is chosen at run time.
#pragma once
// The deko3d renderer's side of Aurora (built only into the Switch NRO, with AURORA_SWITCH_DEKO3D; the
// phase 24 hooks call these when enabled()). With WIICOMPILED_SWITCH_RENDERER=deko3d Aurora runs Dawn on
// its Null device as its object model (textures, views, samplers and bind groups are created,
// validated and reference-counted by Dawn with no GPU work, and get deko3d shadows through the
// linker's --wrap: dk_objects.cpp), records every frame as for WebGPU, and these functions turn the
// recorded passes into deko3d commands instead of Dawn's encoders:
//   staging    Aurora's three staging slots are deko3d memory the GX FIFO thread writes and the GPU
//              reads in place (no CopyBufferToBuffer); a slot is reused once the GPU has finished the
//              batch that read it (a fence per slot, signalled at the end of each batch)
//   batches    gfx::render of a sealed frame (frame worker) or of a staging split (producer thread,
//              with the frame worker done): one batch, its passes, a fence for its slot, a submit
//   passes     render targets, load-op clears, viewport, scissor, blend constant, a barrier at every
//              pass end; exact EFB copies with the copy engine after it
//   draws      GX draws (pipeline state cache, both stages, uniform buffer 0 at the draw's offset,
//              storage buffers 0 and 1, the 8 texture handles of the bind group, u16 indices) and the
//              partial-clear draws
//   pipelines  made on Aurora's pipeline compile threads (dk_pipeline.cpp): the GX fixed state as
//              build_pipeline gives Dawn, the module's shaders from the SD card's DKSH cache or the
//              uam worker (Tint + uam); a draw whose shaders are not ready is skipped, or waited for
//              where Aurora's policy waits (skip_unready_pipelines off, persistent copies)
//   present    on the frame worker (the presenter thread is not used): the EFB drawn into the
//              swapchain image in Aurora's present viewport, ImGui over it, dkQueuePresentImage
//   extras     (workstream C) EFB copies with a conversion or shader sampling and palette conversions
//              (Aurora's fixed WGSL as GLSL), the depth peek snapshot and EFB RAM readbacks (the copy
//              engine into CPU-visible memory, a fence)
// Not done here (logged once when met): MSAA, frame interpolation, FSR/MetalFX.
#include <cstddef>
#include <cstdint>
#include <memory>

#include <webgpu/webgpu.h>

namespace aurora::webgpu {
struct Viewport;
}
namespace aurora::gfx {
struct ClipRect;
struct Range;
namespace clear {
struct DrawData;
struct PipelineConfig;
}  // namespace clear
namespace tex_copy_conv {
struct ConvRequest;
}
namespace tex_palette_conv {
struct ConvRequest;
}
namespace depth_peek {
struct FrameMapping;
}
}  // namespace aurora::gfx
namespace aurora::gx {
struct DrawData;
struct PipelineConfig;
}  // namespace aurora::gx
struct ImDrawData;

namespace aurora::gfx::dk {

// WIICOMPILED_SWITCH_RENDERER=deko3d (read once; anything else keeps Dawn on Vulkan)
bool enabled() noexcept;
// After Dawn's (Null) device exists, before any texture: the deko3d device, the swapchain on the
// default NWindow, the shader cache and the uam worker; turns the --wrap shadows on.
void initialize();
void shutdown();
// a line in the log, once per kind, for what this renderer does not do (yet)
void note_unsupported(const char* what);

// ---- staging (gfx/common.cpp)
struct StagingMemory {
  uint8_t* cpu = nullptr;
  uint64_t gpu = 0;
};
// Aurora's staging slot `slot` (gfx::initialize): its deko3d memory, size bytes
StagingMemory staging_create(size_t slot, uint64_t size);
// Any thread: blocks until the GPU has finished the last batch that read the slot (begin_frame's "map")
void staging_wait(size_t slot);

// ---- batches and passes (gfx/common.cpp, dk_common.inc; the command buffer owner)
void batch_begin(uint32_t stagingSlot);
void batch_end(uint32_t stagingSlot);
struct PassTargets {
  WGPUTextureView color = nullptr;
  WGPUTextureView depth = nullptr;
  uint32_t width = 0, height = 0;
  bool clearColor = false, clearDepth = false;
  float clearColorValue[4] = {};
  float clearDepthValue = 0.f;
  bool requireReadyPipelines = false;  // a persistent copy follows: draws wait for their shaders
};
// false: the pass's target has no deko3d image (logged once); its commands are not recorded
bool pass_begin(const PassTargets& targets);
void pass_viewport(const webgpu::Viewport& viewport);
void pass_scissor(const ClipRect& scissor);
void draw_gx(const gx::DrawData& draw, const Range* uniformOverride);
void draw_clear(const clear::DrawData& draw);
void pass_end();
// an exact EFB copy (no conversion, no scaling): the copy engine
void copy_texture(WGPUTexture src, uint32_t srcX, uint32_t srcY, WGPUTexture dst, uint32_t width, uint32_t height);

// ---- workstream C (dk_copy_conv.cpp, dk_depth_peek.cpp, dk_efb_ram.cpp; the command buffer owner)
// tex_copy_conv::run (shaderSampled false) / tex_copy_conv::blit (true) after the pass: Aurora's
// fixed WGSL as GLSL, drawn into the request's dst attachment view; the request's uniformRange is in
// the open batch's staging slot (uniform_address). False: not drawn (logged once).
bool copy_convert(const tex_copy_conv::ConvRequest& request, bool shaderSampled);
// tex_palette_conv::run before the pass (the same way)
bool palette_convert(const tex_palette_conv::ConvRequest& request);
// depth_peek::encode_frame_snapshot after the frame's last pass, when GXPeekZ asked for a snapshot:
// the depth image copied by the copy engine into CPU-visible memory, a fence after it
void depth_peek_snapshot(WGPUTextureView depthView, uint32_t width, uint32_t height, uint32_t samples,
                         const depth_peek::FrameMapping& mapping);
// depth_peek::read_latest (any thread, no wait): the newest snapshot the GPU has finished, read through
// the compute shader's mapping
bool depth_peek_read(uint16_t x, uint16_t y, uint32_t& z) noexcept;

// EFB RAM readbacks (gfx/efb_ram_copy.cpp): a texture's level 0 copied by the copy engine into
// CPU-visible memory, in CopyTextureToBuffer's layout (rows of bytesPerRow bytes), with a fence of its
// own. Recorded between readback_begin and readback_end (a batch of its own on the staging slot the
// last batch read, where efb_ram pushed its blit uniforms); copy_convert may be called in between.
struct Readback;
void readback_begin();
// readback: reused when its memory is large enough and the GPU is done with it (an async slot's)
bool readback_copy(WGPUTexture texture, uint32_t bytesPerRow, std::shared_ptr<Readback>& readback);
void readback_end();  // the fences and the submit
// Any thread: the bytes once the GPU has written them; poll does not wait (null until then), wait
// blocks for up to 5 s (null on a timeout or a GPU fault, logged)
const uint8_t* readback_poll(Readback* readback) noexcept;
const uint8_t* readback_wait(Readback* readback) noexcept;

// ---- pipelines (gx/pipeline.cpp, gfx/clear.cpp; Aurora's pipeline compile threads)
// The deko3d pipeline of a config, registered under Aurora's PipelineRef for it; Dawn's pipeline is not
// created (create_pipeline returns a null wgpu::RenderPipeline)
void create_gx_pipeline(const gx::PipelineConfig& config);
void create_clear_pipeline(const clear::PipelineConfig& config);

// ---- ImGui (imgui.cpp)
uint64_t imgui_texture(uint32_t width, uint32_t height, const void* rgba8);

// ---- present (aurora.cpp, the frame worker)
void swapchain_size(uint32_t& width, uint32_t& height);
struct PresentRequest {
  WGPUTexture source = nullptr;   // the EFB (or a display copy override)
  uint32_t sourceWidth = 0, sourceHeight = 0;
  float viewport[4] = {};         // left, top, width, height in swapchain pixels
  const ImDrawData* ui = nullptr;
  bool presentable = true;  // false: the frame is closed without a present (window::is_presentable)
  void (*beforePresent)(void*) = nullptr;  // Aurora's pacing wait, between the submit and the present
  void* user = nullptr;
};
struct PresentResult {
  bool presented = false;
  uint64_t acquireNs = 0, recordNs = 0, submitNs = 0, waitNs = 0, presentNs = 0;
};
PresentResult present(const PresentRequest& request);

}  // namespace aurora::gfx::dk
