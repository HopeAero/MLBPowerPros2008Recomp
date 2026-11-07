// SPDX-License-Identifier: GPL-3.0-or-later
// Uniform Studio data model: editable layers over a uniform atlas, CPU compositing (what gets baked),
// the over-256-colour check, and the small Python bridge that bakes the .ppu and imports .fsh/.iff.
#pragma once
#include "core.h"  // tstudio::Image

#include <array>
#include <optional>
#include <string>
#include <vector>

struct ImFont;

namespace ustudio {
using tstudio::Image;

// Atlas slots, in the same order bake.py writes them (body=0, cap=1, helmet=2, number=3).
enum class Kind { Body = 0, Cap = 1, Helmet = 2, Number = 3 };
inline constexpr int kKindCount = 4;
struct KindInfo { const char* id; int w, h; };
const KindInfo& Info(Kind k);

// A layer is either an imported image or a line of text, placed in atlas pixels and scaled.
struct Layer {
    std::string name;
    bool visible = true;
    bool isText = false;
    Image src;                               // RGBA source for image layers (empty for text)
    std::string text = "TEXTO";              // text layers
    float fontPx = 20.0f;
    unsigned char color[4] = {255, 255, 255, 255};
    float x = 0.0f, y = 0.0f;                // top-left in atlas pixels
    float scale = 1.0f;                      // image layers only
    // Drawn size in atlas pixels (image layers).
    float DrawnW() const;
    float DrawnH() const;
};

struct Canvas {
    Kind kind = Kind::Body;
    std::vector<Layer> layers;               // layers[0] is the bottom-most
    int selected = -1;
};

// Composite the visible layers (bottom to top) into an atlas-sized RGBA image. This is exactly what is
// written to the PNG the baker reads, so the canvas is what the game stores.
Image Compose(const Canvas& c);
// Distinct opaque (alpha >= 128) RGB colours; > 255 means the atlas won't fit the 256-entry palette.
int CountColors(const Image& img);
// Blit a line of text (from an ImGui font's alpha atlas) into dst at the layer's placement.
void RasterText(Image& dst, ImFont* font, const Layer& layer);

// An authored, schematic region map drawn behind the body canvas as the mould (chest/back/sleeves/
// trousers/cap). It is the editor's own data, NOT derived from the game: the player can drop a precise
// map generated from their own install at UserData/arrange/uniform/body_regions.png to override it.
// Returns an empty image for atlases without a mould.
Image BuildMould(Kind k);

// ---- Python bridge --------------------------------------------------------------------------------
// We invoke the project's Python interpreter to reuse the tested tools/uniform-studio/bake.py and
// import_sources.py instead of porting them to C++. See README.md for the rationale.
struct BridgeResult { bool ok = false; std::string message; };

// Composites every non-empty canvas, writes the PNGs, and bakes <out> via bake.write_ppu.
BridgeResult SaveUniform(const std::filesystem::path& pyExe, const std::filesystem::path& toolDir,
                         const std::filesystem::path& tempDir, const std::filesystem::path& out,
                         const std::array<Canvas, kKindCount>& canvases,
                         const std::array<int, 10>& backup);

// Loads <file> as one or more layers. PNG/DDS are read directly; .fsh/.iff go through import_sources.py.
BridgeResult ImportSource(const std::filesystem::path& pyExe, const std::filesystem::path& toolDir,
                          const std::filesystem::path& tempDir, const std::filesystem::path& file,
                          std::vector<Layer>* out);

} // namespace ustudio
