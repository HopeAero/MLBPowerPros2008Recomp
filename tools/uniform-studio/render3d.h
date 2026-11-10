// SPDX-License-Identifier: GPL-3.0-or-later
// A tiny software 3D rasterizer and an ORIGINAL low-poly humanoid mannequin (not the game's character),
// textured with the composed uniform atlas so the preview resembles how the game wears it. No new deps.
#pragma once
#include "core.h"  // tstudio::Image

#include <filesystem>
#include <vector>

namespace ustudio {

struct Vtx {
    float x, y, z;   // model space
    float u, v;      // atlas UV (0..1, v = 0 top row); ignored when mat == skin
    int mat;         // 0 = textured uniform, 1 = flat skin, 2 = flat accent (cap bill etc.),
                     // 3 = outline hull (black, back faces only), 4 + i = extras[i] (alpha-tested)
};

// The game's own chibi figure, read from the player's disc into a local cache by studio_bridge.py
// ("gamedoll", game_doll.py): triangles normalised to the mannequin's frame (feet at 0, 8 units tall)
// and the figure's small textures (extras 1..; extras[0] is the cap letter the editor composes).
struct GameDoll {
    std::vector<Vtx> mesh;
    std::vector<tstudio::Image> textures;
    bool ok = false;
};
bool LoadGameDoll(const std::filesystem::path& file, GameDoll* out);

// Our own stylised mannequin (normal-ish proportions, small head): built from boxes, UVs mapped to the
// atlas regions (chest front/back, sleeves, trousers, cap) so a logo on the chest shows on the torso.
std::vector<Vtx> BuildMannequin();

// Renders `mesh` textured with `tex` (the composed atlas) into a w*h RGBA image, rotated by yaw/pitch
// (radians) and scaled by zoom, framed on model height `centerY`. Z-buffered, nearest texture sampling, flat
// shading; transparent background.
tstudio::Image RenderMesh(const std::vector<Vtx>& mesh, const tstudio::Image& tex, int w, int h,
                          float yaw, float pitch, float zoom, float centerY = 3.7f,
                          const std::vector<const tstudio::Image*>* extras = nullptr, float panX = 0.0f);

} // namespace ustudio
