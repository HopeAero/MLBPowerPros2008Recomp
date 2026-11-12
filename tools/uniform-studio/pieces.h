// SPDX-License-Identifier: GPL-3.0-or-later
// Colouring the uniform piece by piece, as the game's Arrange editor does (CAP, BRIM, SLEEVES/ETC, UPPER,
// LOWER, LINE and LINE TYPE). The body atlas indices, the Arrange template palette and the 32 colours are
// read from the PLAYER'S OWN game into a local cache (studio_bridge.py "gamebase"); none of it ships with
// the tool or is committed. The palette is composed exactly as the game composes it (0x806BDE24, checked
// entry by entry against the running game: docs/ingenieria-inversa.md, «Uniforme importado»).
#pragma once
#include "core.h"  // tstudio::Image

#include <array>
#include <cstdint>
#include <filesystem>

namespace ustudio {

struct GameBase {
    std::array<uint8_t, 128 * 128> index{};             // body atlas, C8 indices, row-major
    std::array<uint16_t, 256> templatePalette{};         // RGB5A3 (the game's)
    std::array<std::array<uint8_t, 9>, 32> colors{};     // light, mid, dark RGB per colour number
    std::array<uint8_t, 64 * 64> smallDigits{};          // chest number glyphs, C8
    std::array<uint8_t, 128 * 128> backDigits{};         // back number glyphs, C8
    std::array<uint16_t, 256> numberPalette{};           // the number's template palette (RGB5A3)
    bool ok = false;
};

// Reads the cache written by studio_bridge.py ("PPGB" v2); false if missing or malformed.
bool LoadGameBase(const std::filesystem::path& file, GameBase* out);

// The pieces the body atlas carries, in the game's editor order.
enum Piece { kCap = 0, kBrim, kSleeves, kUpper, kLower, kLine, kPieceCount };
struct Pieces {
    std::array<int, kPieceCount> color{21, 21, 21, 0, 0, 21};  // colour numbers 0..31 (the game shows n + 1)
    // A free colour per piece (0xRRGGBB) instead of the game's 32, -1 = use `color`. The body atlas is an
    // image in the .ppu, so any colour reaches the game; the backup bytes keep the nearest game colour.
    std::array<int, kPieceCount> rgb{-1, -1, -1, -1, -1, -1};
    int lineType = 0;                                          // 0 none, 1 piping, 2 pinstripes
    // Stripes the Studio paints itself on the jersey (and the trousers), on top of the game's line type
    // (so piping in the LINE colour + stripes works): 0 none, 1 fine, 2 double, 3 braided, in `stripe1`
    // (0xRRGGBB; the last two pair it with `stripe2`). One every `stripeGap` atlas pixels.
    int stripes = 0, stripeGap = 7, stripe1 = 0x1B2A5B, stripe2 = 0xC8102E;
    bool stripeTrousers = true;
    int number = 0, outline = 21;                              // UNIFORM # and # OUTLINE colours
    int previewNumber = 1;                                     // the shirt number shown in the preview only
    // Where the number goes (saved in the .ppu; the game applies it): chest 0 as the game (Arrange uses
    // position 2), 1 none, 2..9 the game's positions 0..7; offsets in body-atlas pixels; back size in %.
    int chest = 0, chestDx = 0, chestDy = 0, backDx = 0, backDy = 0, backSize = 100;
    bool LayoutSet() const { return chest || chestDx || chestDy || backDx || backDy || backSize != 100; }
};

// The 256-entry body palette (RGB5A3) the game would build for these pieces.
std::array<uint16_t, 256> ComposeBodyPalette(const GameBase& base, const Pieces& pieces);
// The 128x128 RGBA body atlas painted with that palette (the editor's base layer).
tstudio::Image BuildPiecesBase(const GameBase& base, const Pieces& pieces);
// The number's palette as the game builds it (0x806BEEF4): entry 3 the number colour, 4..30 a ramp to
// the outline colour (t = (i - 3) / 28), 31 the outline, 32..52 the outline fading in (alpha,
// t = (i - 32) / 20).
std::array<uint16_t, 256> ComposeNumberPalette(const GameBase& base, const Pieces& pieces);
// Draws the shirt number on the body atlas as the game does over the composed body (0x806CD258): the
// back number (rects from the game tables, 1 to 3 digits) and the small chest number, both placed as the
// pieces' layout says (as the runtime does for the game). For the preview only: the game draws it itself.
void DrawNumber(tstudio::Image& body, const GameBase& base, const Pieces& pieces);
// RGB (0xRRGGBB) of a colour number; shade 0 light, 1 mid, 2 dark.
uint32_t ColorRgb(const GameBase& base, int color, int shade);
// RGB of a piece's shade: its free colour if set (mid / dark derived with the game colours' average
// mid/light and dark/light ratios), else its game colour.
uint32_t PieceRgb(const GameBase& base, const Pieces& pieces, int piece, int shade);
// The game colour number closest to an RGB (for the backup bytes the game falls back to).
int NearestColor(const GameBase& base, uint32_t rgb);
// RGBA of an RGB5A3 value, decoded as GX does.
std::array<uint8_t, 4> DecodeRgb5a3(uint16_t v);

}  // namespace ustudio
