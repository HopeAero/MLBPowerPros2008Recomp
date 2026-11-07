// SPDX-License-Identifier: GPL-3.0-or-later
// The preview panel: the composed body uniform shown large over the region guide (a stand-in for the
// in-game character), plus the palette warning when an atlas needs more than 256 colours.
#pragma once
#include "layers.h"

#include <imgui.h>

struct SDL_Texture;

namespace ustudio {
// `body` is the composed body atlas texture; `colors` its distinct-colour count (> 255 triggers the warning).
void DrawPreview(SDL_Texture* body, SDL_Texture* guide, ImVec2 size, int colors);
} // namespace ustudio
