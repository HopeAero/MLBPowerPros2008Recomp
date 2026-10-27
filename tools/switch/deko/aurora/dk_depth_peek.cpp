// SPDX-License-Identifier: MIT
// MLB Power Pros 2008 Recomp (tools/switch/deko, 2026-10-26; workstream C). After SwitchWaker's depth peek
// (switch/native/aurora/patches/0016 and switch/deko/aurora/dk_encode.cpp at 56c0758; LICENSE.SwitchWaker),
// with the whole snapshot and the mapping of our Aurora's depth_peek.cpp (MIT, Copyright (c) 2022 Luke
// Street) instead of SwitchWaker's area around the peeked points.
// GXPeekZ with the deko3d renderer (dk_gfx.hpp depth_peek_snapshot, depth_peek_read). Aurora's own path
// snapshots the EFB depth after the frame's last pass with a compute pass into a logical-size u32
// array (encode_frame_snapshot: at most 30 times a second, when GXPeekZ asked), maps it after the
// submit and GXPeekZ reads the newest mapped one (read_latest), one to two frames late, never waiting.
// Here, at the same point of the frame (dk_common.inc, after the last pass's barrier), the request is
// taken with the same limit and checks (depth_peek::take_snapshot_request, phase 24) and the depth
// image is copied as it is by the copy engine into CPU-visible memory, with a fence after it;
// read_latest (phase 24) asks depth_peek_read, which reads the newest copy whose fence has signalled
// through the compute shader's arithmetic: the depth pixel at
// clamp(floor(offset + (x + 0.5, y + 0.5) * scale), 0, srcSize - 1), turned into GX's 24-bit Z
// (reversed Z: 1 - depth) with its rounding. The copy is 4 bytes a pixel of the EFB's size (Aurora's
// depth format is Depth32Float); no compute shader and no per-pixel CPU work for unread pixels.
// Three slots: the one read and up to two in flight (encode_frame_snapshot skips the snapshot when its
// three are busy too).
#include "dk_encode.hpp"
#include "dk_gfx.hpp"

#include "gfx/depth_peek.hpp"
#include "gx/gx.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <mutex>

namespace aurora::gfx::dk {
using swdk::R;

namespace {

constexpr uint32_t kSlots = 3;

enum class SlotState : uint8_t { Free, InFlight, Done };
struct Slot {
    swdk::Block mem;
    swdk::TrackedFence fence;
    SlotState state = SlotState::Free;
    uint64_t seq = 0;
    uint32_t copyWidth = 0, copyHeight = 0;  // the copied part of the depth image (from its origin)
    depth_peek::SnapshotParams params;
};

// Guards the slots: GXPeekZ's thread reads, the command buffer owner copies. Taken before
// swdk::fence_mutex (fence_done, fence_record), never after it.
std::mutex g_mutex;
Slot g_slots[kSlots];
int g_latest = -1;  // the Done slot read
uint64_t g_seq = 0, g_copies = 0;

// g_mutex: copies the GPU has finished become Done; the newest is read, the older ones are free again
void refresh_locked() {
    for (Slot& s : g_slots)
        if (s.state == SlotState::InFlight && swdk::fence_done(s.fence)) s.state = SlotState::Done;
    int newest = -1;
    for (uint32_t i = 0; i < kSlots; i++) {
        if (g_slots[i].state == SlotState::Done && (newest < 0 || g_slots[i].seq > g_slots[newest].seq)) newest = int(i);
    }
    for (uint32_t i = 0; i < kSlots; i++)
        if (int(i) != newest && g_slots[i].state == SlotState::Done) g_slots[i].state = SlotState::Free;
    g_latest = newest;
}

}  // namespace

void depth_peek_snapshot(WGPUTextureView depthView, uint32_t width, uint32_t height, uint32_t samples,
                         const depth_peek::FrameMapping& mapping) {
    depth_peek::SnapshotParams params;
    if (!depth_peek::take_snapshot_request(wgpu::Extent3D{width, height, 1}, mapping, params)) return;
    if (depthView == nullptr) return;
    if (samples > 1) {
        note_unsupported("the depth peek snapshot of a multisampled EFB (Aurora: fatal; skipped)");
        return;
    }
    View* v = find_view(depthView);
    Tex* t = v ? v->tex : nullptr;
    if (!t || !t->valid || t->format != DkImageFormat_ZF32 || t->samples > 1) {
        note_unsupported("the depth peek snapshot of a depth buffer that is not single-sampled Depth32Float (skipped)");
        return;
    }
    // the part of the depth image the shader can read (srcSize clamped); beyond the image it reads 0, as
    // an out-of-bounds textureLoad on NVK (robust image access)
    const uint32_t w = std::min(params.srcWidth, std::max(t->width >> v->baseMip, 1u));
    const uint32_t h = std::min(params.srcHeight, std::max(t->height >> v->baseMip, 1u));
    if (w == 0 || h == 0) return;
    std::lock_guard<std::mutex> lock(g_mutex);
    refresh_locked();
    Slot* slot = nullptr;
    for (Slot& s : g_slots)
        if (!slot && s.state == SlotState::Free) slot = &s;
    if (!slot) return;  // (two copies in flight and the one read: as no available slot)
    const uint32_t bytes = w * h * 4;
    if (slot->mem.size < bytes) {
        if (slot->mem) swdk::block_free_deferred(slot->mem.block);
        slot->mem = swdk::block_create(bytes, "a depth peek snapshot");
        if (!slot->mem) return;
    }
    transfer_begin();
    const DkImageView src = level_view(t, v->baseMip);
    const DkImageRect rect = {0, 0, 0, w, h, 1};
    const DkCopyBuf out = {slot->mem.gpu, w * 4, 0};
    dkCmdBufCopyImageToBuffer(R.cmd, &src, &rect, &out, 0);
    // the copy out of the GPU's L2 into memory before the CPU reads it (SwitchWaker's read_texture)
    swdk::barrier(DkBarrier_Full, DkInvalidateFlags_Image | DkInvalidateFlags_L2Cache);
    swdk::fence_record(slot->fence);
    EncodeStats& s = encode_stats();
    s.copies++;
    s.barriers++;
    gpu_mark(GpuWork::Copy);
    slot->state = SlotState::InFlight;
    slot->seq = ++g_seq;
    slot->copyWidth = w;
    slot->copyHeight = h;
    slot->params = params;
    if (g_copies++ == 0) {
        swdk::dklog("depth peek: first snapshot, %ux%u of the depth buffer for a %ux%u GXPeekZ grid", w, h,
                    params.dstWidth, params.dstHeight);
    }
}

bool depth_peek_read(uint16_t x, uint16_t y, uint32_t& z) noexcept {
    std::lock_guard<std::mutex> lock(g_mutex);
    refresh_locked();
    if (g_latest < 0) return false;
    const Slot& s = g_slots[g_latest];
    const depth_peek::SnapshotParams& p = s.params;
    if (x >= p.dstWidth || y >= p.dstHeight || p.srcWidth == 0 || p.srcHeight == 0) return false;
    // depth_peek.cpp's cs_main for invocation (x, y)
    const float fx = std::floor(p.offsetX + (float(x) + 0.5f) * p.scaleX);
    const float fy = std::floor(p.offsetY + (float(y) + 0.5f) * p.scaleY);
    const int px = std::clamp(int(fx), 0, int(p.srcWidth) - 1);
    const int py = std::clamp(int(fy), 0, int(p.srcHeight) - 1);
    float depth = 0.f;
    if (uint32_t(px) < s.copyWidth && uint32_t(py) < s.copyHeight)
        memcpy(&depth, s.mem.cpu + (size_t(py) * s.copyWidth + size_t(px)) * 4, sizeof depth);
    // gx_z24
    const float d = gx::UseReversedZ ? 1.f - depth : depth;
    z = std::min(uint32_t(std::clamp(d, 0.f, 1.f) * 16777215.f + 0.5f), 0x00FFFFFFu);
    return true;
}

}  // namespace aurora::gfx::dk
