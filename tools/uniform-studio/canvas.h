// SPDX-License-Identifier: GPL-3.0-or-later
// The editing canvas: the UV mould behind the live-composited atlas, with click-to-select and drag.
#pragma once
#include "layers.h"

#include <imgui.h>

struct SDL_Texture;

namespace ustudio {
// Draws the mould (checker + region guide) with the composed atlas on top, scaled to fit, and lets the
// player pick and drag a layer. `composed` and `guide` are atlas-resolution textures (guide may be null).
void DrawCanvas(Canvas& c, SDL_Texture* composed, SDL_Texture* guide, ImVec2 size);
} // namespace ustudio
