// SPDX-License-Identifier: MIT
// MLB Power Pros 2008 Recomp (tools/switch/deko, 2026-10-26; workstream C). The copy, barrier and fence
// follow SwitchWaker's read_texture (switch/deko/aurora/dk_present.cpp at 56c0758; LICENSE.SwitchWaker).
// EFB RAM readbacks with the deko3d renderer (dk_gfx.hpp readback_*). gfx/efb_ram_copy.cpp (phase 24)
// keeps its requests, slots and timing and calls these where Dawn's path records CopyTextureToBuffer
// and maps the buffer:
//   sync   aurora_flush_efb_copies_to_ram (producer, frame worker joined): the frame so far is rendered
//          (one batch), then encode_downloads records each native blit and copy as a batch of their own
//          (readback_begin .. readback_end: a fence per readback, a submit) and complete_downloads
//          waits for each fence (readback_wait, at most 5 s as WaitAny) and encodes guest RAM at once
//   async  probe-sized copies (frame worker): encode_async_downloads records them after the sealed
//          frame's batch, before its present; after_submit marks them mapped and every drain
//          (encode_async_downloads' start, after_submit's end, as Dawn's ProcessEvents points) encodes
//          those whose fence has signalled (readback_poll), so guest RAM gets them one frame later as on
//          Dawn
// A readback is a texture's level 0 copied by the copy engine into CPU-visible memory (rows of the
// caller's bytesPerRow: Dawn's 256-byte aligned layout, which efb_ram's encoder reads), a Full barrier
// that also writes the GPU's L2 out, and a fence. Memory comes from a small pool of blocks (sync
// readbacks are made and dropped per flush; an async slot keeps its own).
#include "dk_encode.hpp"
#include "dk_gfx.hpp"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <memory>
#include <mutex>
#include <vector>

namespace aurora::gfx::dk {
using swdk::R;

struct Readback {
    swdk::Block mem;
    swdk::TrackedFence fence;
    uint32_t bytes = 0;
    bool recorded = false;
    std::atomic<bool> done{false};  // its fence was seen signalled: the block can go back to the pool
};

namespace {

constexpr size_t kPoolBlocks = 4;
// blocks of finished readbacks (never destroyed: efb_ram's statics may drop readbacks at exit)
std::mutex& g_poolMutex = *new std::mutex;
std::vector<swdk::Block>& g_pool = *new std::vector<swdk::Block>;
uint64_t g_readbacks = 0;
bool g_batchOpen = false;

swdk::Block pool_take(uint32_t bytes) {
    {
        std::lock_guard<std::mutex> lock(g_poolMutex);
        for (size_t i = 0; i < g_pool.size(); i++) {
            if (g_pool[i].size >= bytes) {
                const swdk::Block b = g_pool[i];
                g_pool.erase(g_pool.begin() + ptrdiff_t(i));
                return b;
            }
        }
    }
    return swdk::block_create(bytes, "an EFB RAM readback");
}

// The last reference: a finished readback's memory goes back to the pool (or is freed once the GPU is
// past the current frame when the readback may still be in flight: shutdown, a dropped request)
void readback_delete(Readback* r) {
    if (r->mem) {
        bool pooled = false;
        if (r->done.load(std::memory_order_acquire) || !r->recorded) {
            std::lock_guard<std::mutex> lock(g_poolMutex);
            if (g_pool.size() < kPoolBlocks) {
                g_pool.push_back(r->mem);
                pooled = true;
            }
        }
        if (!pooled) swdk::block_free_deferred(r->mem.block);
    }
    delete r;
}

}  // namespace

void readback_begin() {
    if (g_batchOpen) return;
    batch_begin(batch_slot());
    g_batchOpen = true;
}

bool readback_copy(WGPUTexture texture, uint32_t bytesPerRow, std::shared_ptr<Readback>& readback) {
    if (!g_batchOpen) readback_begin();
    Tex* t = find_texture(texture);
    if (!t || !t->valid || t->compressed || t->depth || t->blockBytes != 4 || t->samples > 1) {
        note_unsupported("an EFB RAM readback of a texture that is not a 4-byte colour image (the guest keeps its RAM)");
        return false;
    }
    if (bytesPerRow < t->width * 4) return false;
    const uint32_t bytes = bytesPerRow * t->height;
    // a slot's readback is reused once the GPU is done with it (efb_ram only asks for an idle slot's)
    if (!readback || readback->mem.size < bytes || (readback->recorded && !readback->done.load(std::memory_order_acquire))) {
        auto* r = new Readback;
        r->mem = pool_take(bytes);
        if (!r->mem) {
            delete r;
            return false;
        }
        readback = std::shared_ptr<Readback>(r, readback_delete);
    }
    Readback& r = *readback;
    r.bytes = bytes;
    r.done.store(false, std::memory_order_release);
    r.recorded = true;
    transfer_begin();
    const DkImageView src = level_view(t, 0);
    const DkImageRect rect = {0, 0, 0, t->width, t->height, 1};
    const DkCopyBuf out = {r.mem.gpu, bytesPerRow, 0};
    dkCmdBufCopyImageToBuffer(R.cmd, &src, &rect, &out, 0);
    // the copy out of the GPU's L2 into memory before the CPU reads it (SwitchWaker's read_texture)
    swdk::barrier(DkBarrier_Full, DkInvalidateFlags_Image | DkInvalidateFlags_L2Cache);
    swdk::fence_record(r.fence);
    EncodeStats& s = encode_stats();
    s.copies++;
    s.barriers++;
    gpu_mark(GpuWork::Copy);
    if (g_readbacks++ == 0) {
        swdk::dklog("EFB RAM readbacks: first one, %ux%u (%u bytes a row), by the copy engine", t->width, t->height,
                    bytesPerRow);
    }
    return true;
}

void readback_end() {
    if (!g_batchOpen) return;
    batch_end(batch_slot());
    g_batchOpen = false;
}

const uint8_t* readback_poll(Readback* r) noexcept {
    if (!r || !r->recorded) return nullptr;
    if (!r->done.load(std::memory_order_acquire)) {
        if (!swdk::fence_done(r->fence)) return nullptr;
        r->done.store(true, std::memory_order_release);
    }
    return r->mem.cpu;
}

const uint8_t* readback_wait(Readback* r) noexcept {
    if (!r || !r->recorded) return nullptr;
    if (!r->done.load(std::memory_order_acquire)) {
        if (!swdk::fence_wait(r->fence, 5)) return nullptr;  // (logged)
        r->done.store(true, std::memory_order_release);
    }
    return r->mem.cpu;
}

}  // namespace aurora::gfx::dk
