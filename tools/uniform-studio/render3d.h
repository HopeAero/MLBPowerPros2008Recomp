// SPDX-License-Identifier: GPL-3.0-or-later
// A tiny software 3D rasterizer and an ORIGINAL low-poly humanoid mannequin (not the game's character),
// textured with the composed uniform atlas so the preview resembles how the game wears it. No new deps.
#pragma once
#include "core.h"  // tstudio::Image

#include <vector>

namespace ustudio {

struct Vtx {
    float x, y, z;   // model space
    float u, v;      // atlas UV (0..1); ignored when mat == skin
    int mat;         // 0 = textured uniform, 1 = flat skin, 2 = flat accent (cap bill etc.)
};

// Our own stylised mannequin (normal-ish proportions, small head): built from boxes, UVs mapped to the
// atlas regions (chest front/back, sleeves, trousers, cap) so a logo on the chest shows on the torso.
std::vector<Vtx> BuildMannequin();

// Renders `mesh` textured with `tex` (the composed atlas) into a w*h RGBA image, rotated by yaw/pitch
// (radians) and scaled by zoom. Z-buffered, nearest texture sampling, flat shading; transparent background.
tstudio::Image RenderMesh(const std::vector<Vtx>& mesh, const tstudio::Image& tex, int w, int h,
                          float yaw, float pitch, float zoom);

} // namespace ustudio
