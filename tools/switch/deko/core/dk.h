// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of
// the MPL was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
// Adapted from SwitchWakerHD (https://github.com/centollOS/SwitchWakerHD) at df8fbde:
// runtime/src/gfx/deko/dk.h, by way of SwitchWaker (switch/deko/dk.h at 56c0758).
// Modified for MLB Power Pros 2008 Recomp (tools/switch/deko, 2026-10-26): WIICOMPILED_DK_* settings
// instead of COS_DK_*, no test pattern or offline cache, a shader registry by GX module (shaders.cpp),
// fences per Aurora staging slot (TrackedFence) and a present split around a pacing callback.
#pragma once
// The deko3d renderer of the Switch NRO (selected at run time with WIICOMPILED_SWITCH_RENDERER=deko3d):
// one device, one graphics queue, one command buffer, per-frame rings with a fence per frame, the
// image heap, the shader code block and the descriptor sets. Namespace swdk. aurora/ (built into the
// NRO next to Aurora) records Aurora's frames with it.
//
// Threads: the command buffer and the queue belong to whoever holds Aurora's renderer GPU mutex for a
// batch or a present: the frame worker, or the producer thread in a staging split (which first waits
// for the frame worker to be done). Never two at once; see aurora/dk_encode.cpp.
//
// Device conventions: DkDeviceFlags_OriginUpperLeft | DkDeviceFlags_DepthZeroToOne: window and image
// row 0 at the top, clip-space z from 0 to 1, clip-space y up (deko3d 0.5.0's viewport transform has
// scaleY = -height/2 with this origin, so normalized y = +1 is row 0; SwitchWakerHD dk.h:6-10). That
// is WebGPU's clip space, so Aurora's WGSL keeps its y through Tint (Tint's position-y-up option);
// the Conventions switches below turn each part around without a rebuild if the console says
// otherwise.
#include <deko3d.h>

#include <atomic>
#include <cstdarg>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include "shader_translate.h"  // ShaderStage

struct ImDrawData;

namespace swdk {

// ---- memory (memory.cpp) -------------------------------------------------------------------------
// Per-frame resources come in kFrames slots; a slot is reused after its fence (the frame kFrames
// earlier) has signalled. Aurora's own frame data (vertices, uniforms, indices, storage) lives in its
// staging blocks (block_create), which the GX FIFO thread writes and the GPU reads in place; the
// stream slice holds the renderer's own per-frame data (texture uploads, ImGui's vertices, uniforms).
constexpr uint32_t kFrames = 3;
constexpr uint32_t kStreamSliceSize = 16u << 20;   // per frame: the renderer's own data
constexpr uint32_t kCmdSliceSize = 4u << 20;       // per frame: command memory
constexpr uint32_t kCmdChunk = 64u << 10;          // fed at frame_begin, then as the command buffer asks
constexpr uint32_t kImageChunkSize = 64u << 20;    // image heap chunks
constexpr uint32_t kCodeSize = 48u << 20;          // shader code (DKSH), bump allocated, never freed
constexpr uint32_t kImageDescriptors = 8192, kSamplerDescriptors = 1024;
constexpr uint32_t kQuerySize = 64u << 10;         // GPU timestamps (kFrames regions)
constexpr uint32_t kQueueCommandMemory = 1u << 20; // the queue's own command memory
// Thread priorities: 0x3B at most (SwitchWakerHD's first deko3d NRO died on a thread created with
// 0x3C, which the NPDM refuses); the uam worker uses this.
constexpr int kMaxThreadPriority = 0x3B;

struct StreamAlloc {
    void* cpu = nullptr;
    DkGpuAddr gpu = 0;
    explicit operator bool() const { return cpu != nullptr; }
};
// a suballocation of the image heap
struct ImageAlloc {
    DkMemBlock block = nullptr;
    uint32_t offset = 0, size = 0;
    int chunk = -1;
};
// a memory block of its own, CPU-uncached and GPU-cached (Aurora's staging, oversized uploads)
struct Block {
    DkMemBlock block = nullptr;
    uint8_t* cpu = nullptr;
    DkGpuAddr gpu = 0;
    uint32_t size = 0;
    explicit operator bool() const { return block != nullptr; }
};

void memory_init();
void frame_begin(uint64_t frame);   // waits for the slot's fence, resets its stream and command memory
void frame_end();                   // the fence after this frame's commands (recorded into the command buffer)
bool frame_done(uint64_t frame);    // the GPU has finished that frame's commands (no wait)
// Any thread: the newest frame the GPU has finished (polls the fences; never waits)
uint64_t frames_completed();
// held while the queue writes the fences (every submit) and while they are polled
std::mutex& fence_mutex();
void fence_submitted();  // under fence_mutex right after a submit of the frame command buffer
StreamAlloc stream_alloc(uint32_t size, uint32_t alignment);  // this frame's slice; empty when full (logged)
// Thread-safe: the FIFO thread creates Aurora's textures
ImageAlloc image_alloc(uint32_t size, uint32_t alignment);
// Before dkImageLayoutInitialize of a block-linear 2D image whose level 0 is `rows` rows tall
// (compressed: rows of blocks): deko3d 0.5.0 picks a tile its level-0 layout then shrinks, so 6-8-row
// images overlap their next level unless the tile is set explicitly (SwitchWakerHD memory.cpp).
void image_tile_size_fix(DkImageLayoutMaker& m, uint32_t rows);
void image_free_later(const ImageAlloc& a);  // render thread: freed when the GPU is done with the current frame
// Any thread: freed once the next frame has started and the GPU has finished it
void image_free_deferred(const ImageAlloc& a);
// Any thread (logged; null when the heap is exhausted)
Block block_create(uint32_t size, const char* what);
void block_free_deferred(DkMemBlock b);  // any thread: destroyed as image_free_deferred frees
// Copies DKSH into the code block and initializes the shader; false (logged) if full or invalid.
// Thread-safe.
bool code_load(DkShader& shader, const void* dksh, uint32_t size, const char* name);
// whether code_load wrote code since the last call (render thread: then the GPU's shader caches are
// invalidated before the next shader bind)
bool code_take_written();
uint32_t code_used();
DkGpuAddr image_descriptors();
DkGpuAddr sampler_descriptors();
// the current frame slot's part of the query block: kQuerySize / kFrames bytes, 16 per report
struct QueryRegion {
    uint8_t* cpu = nullptr;
    DkGpuAddr gpu = 0;
    uint32_t size = 0;
};
QueryRegion query_region(uint64_t frame);

// A fence the command buffer signals somewhere inside a frame (Aurora's staging slots: one per slot,
// signalled after the batch that reads it). States: idle (never signalled: done), recorded (in the
// command buffer, not submitted yet), submitted. fence_record and the submits are the command
// buffer owner's; fence_done / fence_wait any thread.
struct TrackedFence {
    DkFence fence{};
    std::atomic<uint8_t> state{0};
};
void fence_record(TrackedFence& f);
bool fence_done(TrackedFence& f);
// waits until the fence has signalled; false (logged) when it was recorded but never submitted, or
// after the timeout (seconds; the GPU is wedged)
bool fence_wait(TrackedFence& f, int timeoutSeconds = 10);

struct MemoryStats {
    uint64_t cmdBytesMax = 0, cmdBytesSum = 0;  // command memory fed per frame (64 KB steps)
    uint64_t cmdOverflows = 0;                  // frames that needed more than kCmdSliceSize
    uint64_t streamBytesMax = 0, streamFull = 0;
    uint64_t frames = 0;
    uint64_t imageBytes = 0, imageChunks = 0, codeBytes = 0, blockBytes = 0;
};
MemoryStats memory_stats_take();  // per-frame figures since the last call; totals as they are

// ---- descriptors (descriptors.cpp) -----------------------------------------------------------------
// Image slots below kReservedImages and sampler slots below kReservedSamplers belong to the
// renderer's own passes; the rest are handed out (Aurora's texture views and samplers) and come back
// once the GPU has finished the frame that freed them. Writes are commands (dkCmdBufPushData),
// ordered with the draws. Render thread only.
constexpr uint32_t kOverlayFirstImage = 1, kOverlayImages = 255;  // ImGui's textures: slots 1-255
constexpr uint32_t kEmptyImage = 256;                             // a 1x1 black texture (missing views)
constexpr uint32_t kPresentImage = 257;                           // the present pass's source
constexpr uint32_t kReservedImages = 512;
enum : uint32_t { kSamplerLinearClamp = 0, kSamplerNearestClamp = 1, kReservedSamplers = 16 };
void descriptors_frame_start();  // binds the sets; writes the reserved samplers once; retires slots
void write_image_descriptor(uint32_t slot, const DkImage& image);
void write_image_view_descriptor(uint32_t slot, const DkImageView& view);
uint32_t image_slot_alloc();      // a free slot at or above kReservedImages; 0 when none (logged)
void image_slot_free(uint32_t slot);
// A sampler slot for this sampler (key: a hash of it), written now if new; slots are kept by key and
// the least recently used one is reused when all are taken (kSamplerNearestClamp if none can be).
uint32_t sampler_slot(const DkSampler& sampler, uint64_t key);
struct DescriptorStats {
    uint64_t imageWrites = 0, samplerWrites = 0, samplerEvictions = 0;
    uint32_t imagesUsed = 0, samplersUsed = 0;
};
DescriptorStats descriptor_stats();

// ---- the device (device.cpp) ---------------------------------------------------------------------
struct Renderer {
    DkDevice device = nullptr;
    DkQueue queue = nullptr;
    DkCmdBuf cmd = nullptr;  // the command buffer (one list per submit)
    uint64_t frame = 0;      // frames presented
    uint32_t width = 1280, height = 720;  // the swapchain images (the window)
    bool zcull = false;      // WIICOMPILED_DK_ZCULL=1 (queue flag; off by default)
    bool frameOpen = false;  // between frame_open and the present
    bool tiledCache = false; // Maxwell's tiled cache (binning) is on (tiled_cache_set; off by default)
};
extern Renderer R;

// Every barrier that orders work (mode other than DkBarrier_None) goes through here: with the tiled
// cache on, the binned primitives are flushed first (deko3d flushes the tiled cache itself only at
// fences and counter reports). Command buffer owner.
void barrier(DkBarrier mode, uint32_t invalidate);
// Maxwell's tiled cache, which deko3d 0.5.0 never enables (TiledCacheEnable is not written when the
// queue starts): written at a frame's start by the encoder when WIICOMPILED_DK_TILED_CACHE or the A/B
// cycle asks for it (aurora/dk_encode.cpp); turning it off flushes it first.
void tiled_cache_set(bool on);
// before render targets change (a no-op when the tiled cache is off)
void tiled_cache_flush();

// The conventions, each switchable with an nxlink argument so that the first console session can
// correct them without a rebuild; all logged at start ("[dk] conventions:"). The defaults are
// WebGPU's clip space and facing as they are.
struct Conventions {
    bool flipY = false;        // WIICOMPILED_DK_FLIP_Y=1: every game pass drawn with clip-space y negated
    bool flipFront = false;    // WIICOMPILED_DK_FLIP_FRONT=1: Aurora's front face inverted (CW <-> CCW)
    bool flipTexture = false;  // WIICOMPILED_DK_FLIP_TEXTURE=1: texture uploads written bottom row first
    bool flipPresent = false;  // WIICOMPILED_DK_FLIP_PRESENT=1: the picture shown upside down at the present
};
extern Conventions C;

// Device, queue, memory, the swapchain on the default NWindow, the shader registry and its worker,
// the renderer's own shaders. Once, before the first frame (Aurora's webgpu::initialize).
// gxConfigVersion is Aurora's gx::GXPipelineConfigVersion (part of the DKSH cache's compiler id).
void init(uint32_t gxConfigVersion);
// Waits for the GPU and releases the window.
void shutdown();

// The frame's commands: frame_open (at the first batch of a frame) waits for the slot, binds the
// descriptor sets and uploads ImGui's textures; submit sends what is recorded so far (the frame goes
// on in the same slot); frame_present ends it.
void frame_open();
void submit(const char* why);
// The game's picture: called with stage 0 before the swapchain image is bound (offscreen passes),
// then with stage 1 into the bound image, and with stage 2 after ImGui.
using PictureFn = void (*)(void* user, int stage);
struct PresentTimes {
    bool presented = false;
    uint64_t acquireNs = 0, recordNs = 0, submitNs = 0, waitNs = 0, presentNs = 0;
};
// The frame's end into the next swapchain image: clear, the picture, ImGui, the frame fence, the
// submit, beforePresent (Aurora's pacing wait; may be null), the present.
PresentTimes frame_present(const ImDrawData* ui, PictureFn picture, void* user, void (*beforePresent)(void*),
                           void* beforeUser);
// The frame's end without a present (the window cannot be shown): its fence and a submit, so the
// frame's memory comes back as usual.
void frame_close();
struct SubmitStats {
    uint64_t submits = 0, frames = 0;
};
SubmitStats submit_stats();

// ---- shaders (shaders.cpp) -----------------------------------------------------------------------
// Every DKSH the renderer can draw with, by (stage, XXH3 of the GLSL uam compiled): the SD card's
// cache (sdmc:/switch/mlbpowerpros2008/UserData/Cache/dksh_cache.bin, what the uam worker compiled
// in earlier sessions) and the misses compiled during the session.
//
// One stage's shader. state: Missing (no GLSL yet), Queued (the uam worker has it), Compiled (DKSH
// waiting for the render thread's load budget), Ready (loaded: `shader` is valid), Failed (uam
// refused it; the draws that need it stay skipped).
enum class ShaderState : uint8_t { Missing, Queued, Compiled, Ready, Failed };
struct ShaderEntry {
    ShaderStage stage = ShaderStage::Vertex;
    uint64_t glslHash = 0;
    std::atomic<ShaderState> state{ShaderState::Missing};
    DkShader shader{};
    std::vector<uint8_t> dksh;  // Compiled: the uam output until it is loaded
};
// The entry of a stage's GLSL hash, created (Missing) if new. Stable pointers, never freed.
ShaderEntry* shader_entry(ShaderStage stage, uint64_t glslHash);
// Render thread: the entry is drawable now; a Compiled one is loaded if this frame's budget
// (WIICOMPILED_DK_SHADER_BUDGET, default 64; 0 = no limit) allows, or always with force.
bool shader_usable(ShaderEntry* e, bool force);

// A GX shader module (both stages of one generated WGSL), by the XXH3 of its WGSL. Unresolved: its
// stages are not known (no cache record); Queued: the uam worker translates (Tint) and compiles it;
// Resolved: vs and fs point at their entries (which may still be compiling or have failed);
// Failed: Tint refused the WGSL.
enum class ModuleState : uint8_t { Unresolved, Queued, Resolved, Failed };
struct ShaderModule {
    uint64_t key = 0;
    std::atomic<ModuleState> state{ModuleState::Unresolved};
    std::atomic<ShaderEntry*> vs{nullptr}, fs{nullptr};
    std::atomic<uint32_t> skippedDraws{0};  // draws skipped waiting for it (the worker's queue order)
};
// The module of a WGSL hash, created if new (Resolved when the cache knows it). Stable pointers.
ShaderModule* shader_module(uint64_t wgslHash);
// WGSL of a module, generated on the uam worker when its job runs (so that thousands of queued
// warm-up jobs hold a pipeline config each, not a WGSL text each)
using WgslSource = std::function<std::string()>;
// Queues an Unresolved module (any thread). foreground: a draw waits for it (before the warm-up's
// background jobs); for a module already queued, foreground moves it up (source may be empty then).
void module_compile(ShaderModule* m, WgslSource source, bool foreground);
// Blocks until the module and both its stages have left Queued. False if anything failed.
bool module_wait(ShaderModule* m);
// Render thread: both stages loaded (see shader_usable). Counts a skipped draw when not.
bool module_usable(ShaderModule* m, bool force);
// GLSL of the renderer's own shaders (fixed_shaders.cpp): compiled now (cache or uam worker, waits),
// loaded; null if it failed (logged). Any thread but the uam worker.
const DkShader* fixed_shader(ShaderStage stage, const char* glsl, const char* name);
void shaders_frame_start();  // the load budget's frame (render thread)
// Reads the SD card's cache and starts the uam worker.
void shaders_init(uint32_t gxConfigVersion);
struct ShaderTotals {
    uint64_t loaded = 0, pending = 0, failed = 0, compiled = 0, compileNs = 0, compileMaxNs = 0, skippedDraws = 0,
             modules = 0, modulesCached = 0, translateFailures = 0;
};
ShaderTotals shader_totals();
// One line for the periodic statistics: bytes written to out
int shaders_report(char* out, size_t size);

// ---- the renderer's own shaders (fixed_shaders.cpp) ----------------------------------------------
enum FixedId { kImguiVs, kImguiFs, kClearVs, kClearFs, kBlitVs, kBlitFs, kFixedCount };
void fixed_shaders_init();
const DkShader* builtin_shader(FixedId id);  // null if it did not load (logged)

// Viewport and scissor over a window rectangle (pixels from the top left).
void set_view(uint32_t x, uint32_t y, uint32_t w, uint32_t h);
// Rasterizer (no culling), blend (straight alpha) and depth state for the renderer's 2D passes.
void bind_2d_state(bool blend);

// ---- ImGui (overlay.cpp): the runtime's menus, the loading screen, the FPS panel -----------------
void overlay_upload_pending();  // textures overlay_texture queued, before any draw (render thread)
void overlay_draw(const ImDrawData* d);
uint32_t overlay_textures();
// An RGBA8 texture for ImGui: the ImTextureID to draw it with. Any thread; the pixels are copied now
// and reach the GPU at the next frame.
uint64_t overlay_texture(uint32_t width, uint32_t height, const void* rgba8);

// ---- logging and settings ------------------------------------------------------------------------
void dklog(const char* format, ...) __attribute__((format(printf, 1, 2)));
// The log written out now: deko3d aborts without a word when a creation fails (release library:
// 2359-xxxx), so creations log what they are about to do and flush first.
void log_flush();
[[noreturn]] void fatal(const char* format, ...) __attribute__((format(printf, 1, 2)));
// The queue is in an error state after a GPU fault, and deko3d aborts on any call that touches it:
// checked before each submit, acquire and present.
void check_queue(const char* before);
// a setting (an nxlink argument WIICOMPILED_DK_*=value, which the runtime turns into an environment
// variable): unset or empty -> def, "0" -> false, anything else -> true
bool env_flag(const char* name, bool def);
long env_long(const char* name, long def);
// the runtime's thread placement (runtime/src/platform/switch/switch_threads.cpp)
void pin_current_thread(int core, int priority);
// WIICOMPILED_DK_SHOT=<n>: every n-th present, 4-byte colour images recorded with shot_record (the
// present source, the swapchain image) are copied to a buffer and, after the submit, written to
// stderr as base64 "[shot]" lines (tools/switch/deko/shot.py turns an nxlink log into PNGs).
bool shot_due();  // the frame being recorded is one to capture
void shot_record(const DkImage& image, uint32_t width, uint32_t height, const char* tag);

inline uint64_t now_ns() {
    uint64_t ticks;
    asm volatile("mrs %0, cntpct_el0" : "=r"(ticks));
    return ticks * 625 / 12;  // 19.2 MHz
}

}  // namespace swdk
