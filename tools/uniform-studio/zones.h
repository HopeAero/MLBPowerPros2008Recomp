// SPDX-License-Identifier: GPL-3.0-or-later
// The places of the uniform the player works on (chest, back, sleeves, trousers, cap, brim, cap letter,
// helmet, number). Body zones are worked out at run time from the player's own game: the piece of each
// atlas pixel from its palette index (the entries the game's palette writer gives each piece, pieces.h)
// and front / back from the game figure's UVs (render3d.h GameDoll). Nothing is stored in the repo.
#pragma once
#include "layers.h"
#include "pieces.h"
#include "render3d.h"

#include <cstdint>
#include <vector>

namespace ustudio {

struct Zone {
    const char* es;
    const char* en;
    Kind kind;
    float x0, y0, x1, y1;          // bounding box in atlas pixels (the canvas zooms to it)
    std::vector<uint8_t> mask;     // atlas-sized, 1 inside the zone; empty = the whole atlas
};

// Always: "whole uniform" + the cap letter, helmet and number atlases. With the game's data also the
// chest, back, sleeves, trousers, cap and brim of the body atlas.
std::vector<Zone> BuildZones(const GameBase* base, const GameDoll* doll);

}  // namespace ustudio
