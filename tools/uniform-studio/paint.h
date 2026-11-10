// SPDX-License-Identifier: GPL-3.0-or-later
// Painting on a layer's own atlas-sized image: brush, straight line, rectangle, eraser, flood fill, and
// line patterns (stripes, pinstripes, checks). Everything can be kept inside a zone mask (zones.h), so a
// pattern stays on the jersey's chest, the trousers, etc. All in atlas pixels.
#pragma once
#include "core.h"  // tstudio::Image

#include <array>
#include <cstdint>
#include <vector>

namespace ustudio {

using Rgba8 = std::array<uint8_t, 4>;

// mask: atlas-sized (img.width * img.height), 1 = paintable; null = everywhere.
struct PaintMask {
    const std::vector<uint8_t>* mask = nullptr;
    bool Allows(const tstudio::Image& img, int x, int y) const;
};

// A round brush dab of diameter `size` (1 = one pixel). `erase` clears to transparent.
void PaintDab(tstudio::Image& img, float x, float y, float size, Rgba8 c, bool erase, const PaintMask& m);
// A stroke from (x0, y0) to (x1, y1) made of dabs.
void PaintLine(tstudio::Image& img, float x0, float y0, float x1, float y1, float size, Rgba8 c, bool erase,
               const PaintMask& m);
// A rectangle between two corners: filled, or an outline `size` thick.
void PaintRect(tstudio::Image& img, float x0, float y0, float x1, float y1, float size, bool filled, Rgba8 c,
               const PaintMask& m);
// Flood fill from (x, y) over the 4-connected pixels of `ref` (what the player sees; null = img itself)
// that have the start pixel's colour, painting them into img.
void PaintFill(tstudio::Image& img, const tstudio::Image* ref, int x, int y, Rgba8 c, const PaintMask& m);

enum class Pattern { Vertical, Horizontal, Diagonal, DiagonalBack, Checks };
// Stripes `width` px wide every `spacing` px (checks: squares of `spacing`), shifted by `offset`, over the
// whole image (inside the mask).
void PaintPattern(tstudio::Image& img, Pattern p, float width, float spacing, float offset, Rgba8 c, const PaintMask& m);

}  // namespace ustudio
