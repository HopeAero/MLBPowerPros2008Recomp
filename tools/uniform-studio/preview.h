// SPDX-License-Identifier: GPL-3.0-or-later
// The preview panel: either a chibi "paper-doll" of the Power Pros character wearing the composed
// uniform (atlas regions mapped onto body parts), or the flat composed atlas. A toggle switches them.
// Also shows the palette warning when an atlas needs more than 256 colours.
#pragma once
#include "layers.h"

#include <imgui.h>

struct SDL_Texture;

namespace ustudio {
// `body` is the composed body atlas texture; `colors` its distinct-colour count (> 255 triggers the
// warning). `*dollView` picks the chibi view (true) or the flat atlas (false) and is toggled in-panel.
void DrawPreview(SDL_Texture* body, SDL_Texture* guide, ImVec2 size, int colors, bool* dollView);
} // namespace ustudio
