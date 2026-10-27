// SPDX-License-Identifier: MIT
// From SwitchWaker (switch/deko/aurora/dk_objects.hpp at 56c0758; LICENSE.SwitchWaker). Modified for MLB
// Power Pros 2008 Recomp (tools/switch/deko, 2026-10-26): wgpuQueueWriteTexture is wrapped too (our Aurora
// uploads textures through the queue, not through staging), new textures Dawn would zero-initialize
// are cleared, the shadows are only made while the deko3d renderer runs (objects_activate).
#pragma once
// The deko3d side of Aurora's WebGPU objects. With the deko3d renderer Dawn's Null device stays
// Aurora's object model (textures, views, samplers, bind groups are still created, validated and
// reference-counted by Dawn, with no GPU work), and every texture, view, sampler and bind group Aurora
// creates gets a shadow here, through the linker's --wrap of the WebGPU C functions that create them
// and count their references (phase 24's CMake):
//   texture      -> a DkImage in the image heap (format, size, levels, usage from the descriptor)
//   texture view -> a DkImageView of it (levels, format, swizzle) and, once sampled, an image
//                   descriptor slot
//   sampler      -> a DkSampler (its descriptor slot comes from descriptors.cpp's cache by key)
//   bind group   -> its entries' shadows, for the 8 texture handles of a GX draw
//   WriteTexture -> the pixels copied now, uploaded by the copy engine at the next batch
// The shadows mirror Dawn's lifetimes: one count per WebGPU reference, plus the references Dawn's
// objects hold on each other (a view on its texture, a bind group on its views and samplers), so a
// shadow lives exactly as long as the object Aurora can still reach. The last release frees the
// image memory and the descriptor slot once the GPU is past the frames that used them.
//
// Thread-safety: creation, uploads and release from any thread (Aurora creates and fills textures on
// its GX FIFO thread); lookups and the upload flush from the command buffer owner. Descriptor slots
// are the command buffer owner's.
#include <deko3d.h>
#include <webgpu/webgpu.h>

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

#include "dk.h"

namespace aurora::gfx::dk {

struct Tex {
    std::atomic<int32_t> refs{1};
    WGPUTexture handle = nullptr;
    WGPUTextureFormat wformat = WGPUTextureFormat_Undefined;
    DkImageFormat format = DkImageFormat_None;
    uint32_t width = 0, height = 0, layers = 1, mips = 1, samples = 1;
    uint64_t usage = 0;
    bool depth = false, compressed = false, valid = false, render = false;
    uint8_t blockW = 1, blockH = 1, blockBytes = 4;
    DkImage image{};
    swdk::ImageAlloc mem;
    std::string label;
};

struct View {
    std::atomic<int32_t> refs{1};
    WGPUTextureView handle = nullptr;
    Tex* tex = nullptr;
    uint32_t baseMip = 0, mipCount = 1;
    DkImageView view{};  // pImage set to &tex->image
    uint32_t slot = 0;   // the image descriptor (command buffer owner; 0 until first sampled)
};

struct Samp {
    std::atomic<int32_t> refs{1};
    WGPUSampler handle = nullptr;
    DkSampler sampler{};
    uint64_t key = 0;
};

struct BindGroup {
    std::atomic<int32_t> refs{1};
    WGPUBindGroup handle = nullptr;
    struct Entry {
        uint32_t binding = 0;
        View* view = nullptr;
        Samp* sampler = nullptr;
    };
    std::vector<Entry> entries;
    // the GX layout's 8 combined texture handles (binding 2i: view, 2i + 1: sampler), made once per frame
    DkResHandle handles[8] = {};
    uint64_t handlesFrame = 0;
};

// The shadows are made from now on (the deko3d device exists); before, the wraps only forward.
void objects_activate();
bool objects_active();

// Lookups (command buffer owner). Null when the object has no shadow (created before the device
// existed, or not a WebGPU object of this process).
Tex* find_texture(WGPUTexture t);
View* find_view(WGPUTextureView v);
Samp* find_sampler(WGPUSampler s);
BindGroup* find_bind_group(WGPUBindGroup g);

// The view's image descriptor slot, written on first use (kEmptyImage if none is free or the view is
// invalid)
uint32_t view_slot(View* v);
// A combined handle for a view and a sampler (null sampler: the nearest-clamp one)
DkResHandle texture_handle(View* v, Samp* s);
// The GX bind group's 8 handles, valid for the frame being recorded
const DkResHandle* gx_handles(BindGroup* g);

// At the frame's start: descriptor slots of views released since are retired
void objects_frame_start();
// At each batch's start, between copy-engine barriers: new textures cleared (what Dawn's lazy
// zero-initialization gives) and the queued WriteTexture uploads copied into their images. The number
// of copies recorded.
uint32_t objects_flush_uploads();

// a DkImageView of one level of a texture (transfers, render targets)
DkImageView level_view(Tex* t, uint32_t level);

struct ObjectStats {
    uint64_t textures = 0, views = 0, samplers = 0, bindGroups = 0;  // alive
    uint64_t texturesCreated = 0, imageBytes = 0, unknownFormats = 0;
    uint64_t uploads = 0, uploadBytes = 0, clears = 0;
};
ObjectStats object_stats();

// The deko3d format of a WebGPU texture format (DkImageFormat_None when unsupported), with its block
// geometry (compressed formats: 4x4 blocks)
struct FormatInfo {
    DkImageFormat format = DkImageFormat_None;
    uint8_t blockW = 1, blockH = 1, blockBytes = 4;
    bool depth = false, compressed = false, canRender = false, can2d = false;
};
FormatInfo format_info(WGPUTextureFormat f);

}  // namespace aurora::gfx::dk
