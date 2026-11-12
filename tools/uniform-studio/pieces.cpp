// SPDX-License-Identifier: GPL-3.0-or-later
#include "pieces.h"

#include "paint.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

namespace ustudio {
namespace {
using Rgba = std::array<uint8_t, 4>;

// As the game's palette writer (0x8001614C): opaque colours as RGB555, the rest as RGB4A3.
uint16_t Encode(const Rgba& c) {
    if (c[3] == 255) return static_cast<uint16_t>(0x8000 | (c[0] >> 3) << 10 | (c[1] >> 3) << 5 | (c[2] >> 3));
    return static_cast<uint16_t>((c[3] >> 5) << 12 | (c[0] >> 4) << 8 | (c[1] >> 4) << 4 | (c[2] >> 4));
}

// The game's mix (0x806BF3F8): t = (i - i0) / (i1 - i0); each channel a * (1 - t) + b * t, single
// precision, products and sum rounded separately (no fused multiply-add), truncated.
Rgba Mix(const Rgba& a, int i0, const Rgba& b, int i1, int i) {
    const float t = static_cast<float>(i - i0) / static_cast<float>(i1 - i0);
    const float s = 1.0f - t;
    Rgba out{};
    for (int k = 0; k < 4; ++k) {
        const float pa = static_cast<float>(a[k]) * s;
        const float pb = static_cast<float>(b[k]) * t;
        const float sum = pa + pb;
        out[k] = static_cast<uint8_t>(static_cast<int>(sum));
    }
    return out;
}
}  // namespace

std::array<uint8_t, 4> DecodeRgb5a3(uint16_t v) {
    if (v & 0x8000) {
        const int r = (v >> 10) & 31, g = (v >> 5) & 31, b = v & 31;
        return {static_cast<uint8_t>(r << 3 | r >> 2), static_cast<uint8_t>(g << 3 | g >> 2),
                static_cast<uint8_t>(b << 3 | b >> 2), 255};
    }
    const int a = (v >> 12) & 7, r = (v >> 8) & 15, g = (v >> 4) & 15, b = v & 15;
    return {static_cast<uint8_t>(r * 17), static_cast<uint8_t>(g * 17), static_cast<uint8_t>(b * 17),
            static_cast<uint8_t>(a << 5 | a << 2 | a >> 1)};
}

bool LoadGameBase(const std::filesystem::path& file, GameBase* out) {
    std::ifstream f(file, std::ios::binary);
    if (!f) return false;
    const std::vector<uint8_t> d{std::istreambuf_iterator<char>(f), {}};
    constexpr size_t kSize = 8 + 128 * 128 + 512 + 32 * 9 + 64 * 64 + 128 * 128 + 512;
    if (d.size() != kSize || std::memcmp(d.data(), "PPGB", 4) != 0 || d[4] || d[5] || d[6] || d[7] != 2) return false;
    GameBase b;
    std::memcpy(b.index.data(), d.data() + 8, b.index.size());
    for (int i = 0; i < 256; ++i)
        b.templatePalette[i] = static_cast<uint16_t>(d[8 + 16384 + i * 2] << 8 | d[8 + 16384 + i * 2 + 1]);
    for (int i = 0; i < 32; ++i) std::memcpy(b.colors[i].data(), d.data() + 8 + 16384 + 512 + i * 9, 9);
    size_t o = 8 + 16384 + 512 + 32 * 9;
    std::memcpy(b.smallDigits.data(), d.data() + o, b.smallDigits.size());
    o += b.smallDigits.size();
    std::memcpy(b.backDigits.data(), d.data() + o, b.backDigits.size());
    o += b.backDigits.size();
    for (int i = 0; i < 256; ++i) b.numberPalette[i] = static_cast<uint16_t>(d[o + i * 2] << 8 | d[o + i * 2 + 1]);
    b.ok = true;
    *out = b;
    return true;
}

uint32_t ColorRgb(const GameBase& base, int color, int shade) {
    const auto& c = base.colors[static_cast<size_t>(color & 31)];
    return static_cast<uint32_t>(c[shade * 3] << 16 | c[shade * 3 + 1] << 8 | c[shade * 3 + 2]);
}

uint32_t PieceRgb(const GameBase& base, const Pieces& p, int piece, int shade) {
    const int free = p.rgb[static_cast<size_t>(piece)];
    if (free < 0) return ColorRgb(base, p.color[static_cast<size_t>(piece)], shade);
    if (shade == 0) return static_cast<uint32_t>(free);
    // ponytail: one brightness ratio per shade (averaged over the game's 32 colours), not a per-hue model.
    double sum = 0;
    int n = 0;
    for (const auto& c : base.colors) {
        const int light = c[0] + c[1] + c[2];
        if (light < 60) continue;  // near-black colours say nothing about the ratio
        sum += static_cast<double>(c[shade * 3] + c[shade * 3 + 1] + c[shade * 3 + 2]) / light;
        ++n;
    }
    const double f = n ? sum / n : (shade == 1 ? 0.8 : 0.6);
    uint32_t out = 0;
    for (int k = 0; k < 3; ++k) {
        const int v = std::clamp(static_cast<int>(((free >> (16 - 8 * k)) & 255) * f), 0, 255);
        out |= static_cast<uint32_t>(v) << (16 - 8 * k);
    }
    return out;
}

int NearestColor(const GameBase& base, uint32_t rgb) {
    int best = 0;
    long bestD = -1;
    for (int i = 0; i < 32; ++i) {
        const uint32_t c = ColorRgb(base, i, 0);
        long d = 0;
        for (int k = 0; k < 3; ++k) {
            const long e = static_cast<long>((c >> (16 - 8 * k)) & 255) - static_cast<long>((rgb >> (16 - 8 * k)) & 255);
            d += e * e;
        }
        if (bestD < 0 || d < bestD) { bestD = d; best = i; }
    }
    return best;
}

// The game's body palette (0x806BDE24), entry by entry.
std::array<uint16_t, 256> ComposeBodyPalette(const GameBase& base, const Pieces& p) {
    std::array<uint16_t, 256> pal = base.templatePalette;
    auto shade = [&](int piece, int s) {
        const uint32_t c = PieceRgb(base, p, piece, s);
        return Rgba{static_cast<uint8_t>(c >> 16), static_cast<uint8_t>(c >> 8), static_cast<uint8_t>(c), 255};
    };
    auto w = [&](int e, const Rgba& c) { pal[static_cast<size_t>(e)] = Encode(c); };
    const Rgba U = shade(kUpper, 0), Ud = shade(kUpper, 2), Ln = shade(kLine, 0), Lo = shade(kLower, 0),
               Lod = shade(kLower, 2), S = shade(kSleeves, 0), Sm = shade(kSleeves, 1), Sd = shade(kSleeves, 2),
               C = shade(kCap, 0), Cm = shade(kCap, 1), Cd = shade(kCap, 2), B = shade(kBrim, 0),
               Bm = shade(kBrim, 1), Bd = shade(kBrim, 2);
    const int lt = p.lineType;
    if (lt == 0) {
        for (int e : {0x80, 0x81, 0x82, 0x92, 0x93, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7}) w(e, U);
    } else if (lt == 1) {
        for (int e : {0x80, 0x81, 0x82}) w(e, U);
        for (int e : {0x92, 0x93, 0xA2, 0xA3}) w(e, Ln);
        w(0xA4, U);
        w(0xA5, Mix(U, 0xA4, Ln, 0xA7, 0xA5));
        w(0xA6, Mix(U, 0xA4, Ln, 0xA7, 0xA6));
        w(0xA7, Ln);
    } else if (lt == 2) {
        w(0x80, U);
        w(0x81, Mix(U, 0x80, Ln, 0x82, 0x81));
        w(0x82, Ln);
        for (int e : {0x92, 0x93}) w(e, Ln);
        for (int e : {0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7}) w(e, U);
    }
    for (int e : {0x88, 0x89, 0x8A}) w(e, Ud);
    if (lt == 2) {
        w(0x70, U);
        w(0x71, Ln);
        w(0x78, Ud);
    } else {
        w(0x70, Lo);
        w(0x71, Lo);
        w(0x78, Lod);
    }
    const Rgba x = DecodeRgb5a3(pal[0x71]);  // read back from the palette (0x80016264), as the game does
    w(0x72, Mix(x, 0x71, Sm, 0x74, 0x72));
    w(0x73, Mix(x, 0x71, Sm, 0x74, 0x73));
    w(0x74, Sm);
    w(0x75, S);
    w(0x76, S);
    w(0x10, S);
    w(0x11, Mix(S, 0x10, Sm, 0x12, 0x11));
    w(0x12, Sm);
    w(0x30, S);
    w(0x18, Sd);
    w(0x38, Sd);
    w(0x40, C);
    w(0x41, Mix(C, 0x40, Cm, 0x42, 0x41));
    w(0x42, Cm);
    w(0x44, C);
    w(0x45, Mix(C, 0x44, Cm, 0x46, 0x45));
    w(0x46, Cm);
    w(0x47, Cd);
    w(0x49, C);
    w(0x4A, Mix(C, 0x49, Cm, 0x4B, 0x4A));
    w(0x4B, Cm);
    w(0x4D, C);
    w(0x4E, Mix(C, 0x4D, Cm, 0x4F, 0x4E));
    w(0x4F, Cm);
    w(0x48, Cd);
    w(0x60, C);
    for (int e : {0x61, 0x62, 0x63}) w(e, Mix(C, 0x60, Cm, 0x64, e));
    w(0x64, Cm);
    w(0x68, Cd);
    w(0x50, B);
    w(0x51, B);
    w(0x52, Mix(B, 0x51, Bm, 0x54, 0x52));
    w(0x53, Mix(B, 0x51, Bm, 0x54, 0x53));
    w(0x54, Bm);
    w(0x56, Bm);
    w(0x57, Bd);
    w(0x59, B);
    w(0x5A, B);
    w(0x5B, Mix(B, 0x5A, Bm, 0x5D, 0x5B));
    w(0x5C, Mix(B, 0x5A, Bm, 0x5D, 0x5C));
    w(0x5D, Bm);
    w(0x5F, Bm);
    w(0x58, Bd);
    return pal;
}

std::array<uint16_t, 256> ComposeNumberPalette(const GameBase& base, const Pieces& p) {
    std::array<uint16_t, 256> pal = base.numberPalette;
    const auto& n = base.colors[static_cast<size_t>(p.number & 31)];
    const auto& o = base.colors[static_cast<size_t>(p.outline & 31)];
    const Rgba N{n[0], n[1], n[2], 255}, O{o[0], o[1], o[2], 255}, O0{o[0], o[1], o[2], 0};
    pal[3] = Encode(N);
    for (int i = 4; i < 31; ++i) pal[static_cast<size_t>(i)] = Encode(Mix(N, 3, O, 31, i));     // (i - 3) / 28
    pal[31] = Encode(O);
    pal[32] = Encode(O0);
    for (int i = 33; i < 52; ++i) pal[static_cast<size_t>(i)] = Encode(Mix(O0, 32, O, 52, i));  // (i - 32) / 20
    pal[52] = Encode(O);
    return pal;
}

namespace {
struct Rect { float x0, y0, x1, y1; };
// The game's tables (in MEM1 at 0x810334A8.., 0x810337A8.., 0x810338A8..; atlas / texture pixels).
constexpr Rect kBack3[3] = {{64, 88, 85, 120}, {85, 88, 106, 120}, {106, 88, 127, 120}};
constexpr Rect kBack2[2] = {{64, 88, 96, 120}, {96, 88, 128, 120}};
constexpr Rect kBack1 = {80, 88, 112, 120};
constexpr Rect kBackGlyph3[10] = {{112, 32, 128, 64}, {16, 0, 32, 32}, {32, 0, 48, 32}, {48, 0, 64, 32}, {64, 0, 80, 32},
                                  {80, 0, 96, 32}, {96, 0, 112, 32}, {112, 0, 128, 32}, {80, 32, 96, 64}, {96, 32, 112, 64}};
constexpr Rect kBackGlyph[10] = {{32, 32, 64, 64}, {0, 64, 32, 96}, {32, 64, 64, 96}, {64, 64, 96, 96}, {96, 64, 128, 96},
                                 {0, 96, 32, 128}, {32, 96, 64, 128}, {64, 96, 96, 128}, {96, 96, 128, 128}, {0, 32, 32, 64}};
constexpr Rect kSmallGlyph[10] = {{16, 16, 32, 32}, {0, 32, 16, 48}, {16, 32, 32, 48}, {32, 32, 48, 48}, {48, 32, 64, 48},
                                  {0, 48, 16, 64}, {16, 48, 32, 64}, {32, 48, 48, 64}, {48, 48, 64, 64}, {0, 16, 16, 32}};
constexpr Rect kSmall3[3][8] = {
    {{36, 47, 44, 59}, {4, 28, 12, 40}, {4, 37, 12, 49}, {4, 42, 12, 54}, {2, 28, 10, 40}, {2, 37, 10, 49}, {2, 42, 10, 54}, {38, 47, 46, 59}},
    {{44, 47, 52, 59}, {12, 28, 20, 40}, {12, 37, 20, 49}, {12, 42, 20, 54}, {10, 28, 18, 40}, {10, 37, 18, 49}, {10, 42, 18, 54}, {46, 47, 54, 59}},
    {{52, 47, 60, 59}, {20, 28, 28, 40}, {20, 37, 28, 49}, {20, 42, 28, 54}, {18, 28, 26, 40}, {18, 37, 26, 49}, {18, 42, 26, 54}, {54, 47, 62, 59}}};
constexpr Rect kSmall2[2][8] = {
    {{36, 47, 48, 59}, {4, 28, 16, 40}, {4, 37, 16, 49}, {4, 42, 16, 54}, {2, 28, 14, 40}, {2, 37, 14, 49}, {2, 42, 14, 54}, {38, 47, 50, 59}},
    {{48, 47, 60, 59}, {16, 28, 28, 40}, {16, 37, 28, 49}, {16, 42, 28, 54}, {14, 28, 26, 40}, {14, 37, 26, 49}, {14, 42, 26, 54}, {50, 47, 62, 59}}};
constexpr Rect kSmall1[8] = {{42, 47, 54, 59}, {10, 28, 22, 40}, {10, 37, 22, 49}, {10, 42, 22, 54},
                             {8, 28, 20, 40}, {8, 37, 20, 49}, {8, 42, 20, 54}, {44, 47, 56, 59}};

// One glyph (texture rect) stretched over a body rect and blended over what is there.
void Glyph(tstudio::Image& body, const uint8_t* tex, int tw, const std::array<uint16_t, 256>& pal, const Rect& src,
           Rect dst, float dx, float dy, float scale = 1.0f) {
    if (scale != 1.0f) {  // the back number scales about its block's centre (96, 104), as in the runtime
        dst = {96 + (dst.x0 - 96) * scale, 104 + (dst.y0 - 104) * scale, 96 + (dst.x1 - 96) * scale, 104 + (dst.y1 - 104) * scale};
    }
    dst = {dst.x0 + dx, dst.y0 + dy, dst.x1 + dx, dst.y1 + dy};
    const int x0 = static_cast<int>(dst.x0), y0 = static_cast<int>(dst.y0);
    const int x1 = static_cast<int>(dst.x1), y1 = static_cast<int>(dst.y1);
    for (int y = std::max(0, y0); y < std::min(128, y1); ++y)
        for (int x = std::max(0, x0); x < std::min(128, x1); ++x) {
            const int sx = static_cast<int>(src.x0 + (x - dst.x0 + 0.5f) * (src.x1 - src.x0) / (dst.x1 - dst.x0));
            const int sy = static_cast<int>(src.y0 + (y - dst.y0 + 0.5f) * (src.y1 - src.y0) / (dst.y1 - dst.y0));
            const Rgba c = DecodeRgb5a3(pal[tex[static_cast<size_t>(sy) * tw + sx]]);
            uint8_t* o = &body.rgba[(static_cast<size_t>(y) * 128 + x) * 4];
            const float a = c[3] / 255.0f;
            for (int k = 0; k < 3; ++k) o[k] = static_cast<uint8_t>(c[k] * a + o[k] * (1 - a));
        }
}
}  // namespace

void DrawNumber(tstudio::Image& body, const GameBase& base, const Pieces& p) {
    const int front = p.chest == 0 ? 2 : p.chest == 1 ? -1 : p.chest - 2;  // Arrange's own style 9 = position 2
    const float bs = p.backSize / 100.0f, bx = static_cast<float>(p.backDx), by = static_cast<float>(p.backDy);
    const float cx = static_cast<float>(p.chestDx), cy = static_cast<float>(p.chestDy);
    if (!base.ok || body.width != 128 || body.height != 128 || p.previewNumber < 0) return;
    const auto pal = ComposeNumberPalette(base, p);
    const int v = std::min(p.previewNumber, 999);
    const int d[3] = {v / 100, v / 10 % 10, v % 10};
    const int n = v >= 100 ? 3 : v >= 10 ? 2 : 1;
    const uint8_t* back = base.backDigits.data();
    if (n == 3)
        for (int i = 0; i < 3; ++i) Glyph(body, back, 128, pal, kBackGlyph3[d[i]], kBack3[i], bx, by, bs);
    else if (n == 2)
        for (int i = 0; i < 2; ++i) Glyph(body, back, 128, pal, kBackGlyph[d[i + 1]], kBack2[i], bx, by, bs);
    else
        Glyph(body, back, 128, pal, kBackGlyph[d[2]], kBack1, bx, by, bs);
    if (front < 0 || front > 7) return;
    const uint8_t* small = base.smallDigits.data();
    if (n == 3)
        for (int i = 0; i < 3; ++i) Glyph(body, small, 64, pal, kSmallGlyph[d[i]], kSmall3[i][front], cx, cy);
    else if (n == 2)
        for (int i = 0; i < 2; ++i) Glyph(body, small, 64, pal, kSmallGlyph[d[i + 1]], kSmall2[i][front], cx, cy);
    else
        Glyph(body, small, 64, pal, kSmallGlyph[d[2]], kSmall1[front], cx, cy);
}


tstudio::Image BuildPiecesBase(const GameBase& base, const Pieces& pieces) {
    const auto pal = ComposeBodyPalette(base, pieces);
    tstudio::Image img{128, 128, std::vector<uint8_t>(128 * 128 * 4)};
    for (size_t i = 0; i < base.index.size(); ++i) {
        const Rgba c = DecodeRgb5a3(pal[base.index[i]]);
        std::memcpy(&img.rgba[i * 4], c.data(), 4);
    }
    if (pieces.stripes > 0) {
        // Where: the jersey's entries (not the piping's when the line type is piping, so the trim stays)
        // and, if asked, the trousers'.
        std::array<uint8_t, 256> on{};
        for (int e : {0x80, 0x81, 0x82, 0x88, 0x89, 0x8A, 0xA4}) on[e] = 1;
        if (pieces.lineType != 1)
            for (int e : {0x92, 0x93, 0xA2, 0xA3, 0xA5, 0xA6, 0xA7}) on[e] = 1;
        if (pieces.stripeTrousers)
            for (int e : {0x70, 0x71, 0x72, 0x73, 0x78}) on[e] = 1;
        std::vector<uint8_t> mask(base.index.size());
        for (size_t i = 0; i < mask.size(); ++i) mask[i] = on[base.index[i]];
        auto rgba = [](uint32_t c) { return Rgba8{static_cast<uint8_t>(c >> 16), static_cast<uint8_t>(c >> 8), static_cast<uint8_t>(c), 255}; };
        const Pattern kind = pieces.stripes == 1 ? Pattern::Vertical : pieces.stripes == 2 ? Pattern::Double : Pattern::Braid;
        PaintPattern(img, kind, 1, static_cast<float>(std::clamp(pieces.stripeGap, 3, 32)), 0,
                     rgba(static_cast<uint32_t>(pieces.stripe1)), PaintMask{&mask}, rgba(static_cast<uint32_t>(pieces.stripe2)));
    }
    return img;
}

}  // namespace ustudio
