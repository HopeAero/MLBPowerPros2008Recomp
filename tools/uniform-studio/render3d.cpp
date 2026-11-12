// SPDX-License-Identifier: GPL-3.0-or-later
#include "render3d.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <array>
#include <iterator>
#include <map>

namespace ustudio {
using tstudio::Image;

namespace {
// The figure without its wristbands: their triangles (the body model's only 32x16 textures, materials
// 5 + i) and their black outline hulls, the hull pieces (triangles joined by shared corners) that lie
// wholly around a wristband. Measured on game_doll.bin: 2 x 96 hull triangles, as many as the bands.
// Under them the forearm is the uniform's (one atlas pixel, the sleeves colour).
std::vector<Vtx> WithoutBands(const GameDoll& d) {
    struct Box { float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f}; };
    auto grow = [](Box& b, const Vtx& v) {
        const float p[3] = {v.x, v.y, v.z};
        for (int k = 0; k < 3; ++k) { b.lo[k] = std::min(b.lo[k], p[k]); b.hi[k] = std::max(b.hi[k], p[k]); }
    };
    std::map<int, Box> bands;  // material -> its box
    for (size_t i = 0; i < d.textures.size(); ++i)
        if (d.textures[i].width == 32 && d.textures[i].height == 16) bands[5 + static_cast<int>(i)];
    for (const Vtx& v : d.mesh)
        if (bands.count(v.mat)) grow(bands[v.mat], v);
    // Hull triangles joined into pieces by shared corners (union-find over rounded positions).
    std::map<std::array<int, 3>, int> corner;
    std::vector<int> parent;
    auto find = [&](int x) { while (parent[x] != x) x = parent[x] = parent[parent[x]]; return x; };
    auto id = [&](const Vtx& v) {
        const std::array<int, 3> k{static_cast<int>(std::lround(v.x * 1000)), static_cast<int>(std::lround(v.y * 1000)),
                                   static_cast<int>(std::lround(v.z * 1000))};
        auto [it, fresh] = corner.try_emplace(k, static_cast<int>(parent.size()));
        if (fresh) parent.push_back(it->second);
        return it->second;
    };
    const size_t n = d.mesh.size() / 3;
    for (size_t t = 0; t < n; ++t)
        if (d.mesh[t * 3].mat == 3) {
            const int a = find(id(d.mesh[t * 3]));
            parent[find(id(d.mesh[t * 3 + 1]))] = a;
            parent[find(id(d.mesh[t * 3 + 2]))] = find(a);
        }
    std::map<int, Box> piece;
    for (size_t t = 0; t < n; ++t)
        if (d.mesh[t * 3].mat == 3)
            for (int k = 0; k < 3; ++k) grow(piece[find(id(d.mesh[t * 3]))], d.mesh[t * 3 + k]);
    constexpr float kMargin = 0.25f;  // the hull stands ~0.12 off the band (figure 8 units tall)
    auto aroundBand = [&](const Box& p) {
        for (const auto& [mat, b] : bands) {
            bool in = true;
            for (int k = 0; k < 3; ++k) in &= p.lo[k] >= b.lo[k] - kMargin && p.hi[k] <= b.hi[k] + kMargin;
            if (in) return true;
        }
        return false;
    };
    std::vector<Vtx> out;
    for (size_t t = 0; t < n; ++t) {
        const Vtx& v = d.mesh[t * 3];
        if (bands.count(v.mat) || (v.mat == 3 && aroundBand(piece[find(id(v))]))) continue;
        out.insert(out.end(), d.mesh.begin() + t * 3, d.mesh.begin() + t * 3 + 3);
    }
    return out;
}

struct UV { float u0, v0, u1, v1; };

// A textured quad p0..p3 (counter-clockwise), UV corners TL,TR,BR,BL mapped to `r`.
void Quad(std::vector<Vtx>& m, float p[4][3], UV r, int mat) {
    const Vtx a{p[0][0], p[0][1], p[0][2], r.u0, r.v0, mat};
    const Vtx b{p[1][0], p[1][1], p[1][2], r.u1, r.v0, mat};
    const Vtx c{p[2][0], p[2][1], p[2][2], r.u1, r.v1, mat};
    const Vtx d{p[3][0], p[3][1], p[3][2], r.u0, r.v1, mat};
    m.push_back(a); m.push_back(b); m.push_back(c);
    m.push_back(a); m.push_back(c); m.push_back(d);
}

// Axis-aligned box centred at (cx,cy,cz) with half-sizes (hx,hy,hz). Front (+z) and back (-z) faces take
// uvF/uvB; the four side faces take uvS.
void Box(std::vector<Vtx>& m, float cx, float cy, float cz, float hx, float hy, float hz,
         UV uvF, UV uvB, UV uvS, int mat) {
    const float x0 = cx - hx, x1 = cx + hx, y0 = cy - hy, y1 = cy + hy, z0 = cz - hz, z1 = cz + hz;
    float f[4][3] = {{x0, y1, z1}, {x1, y1, z1}, {x1, y0, z1}, {x0, y0, z1}};  // front +z
    float b[4][3] = {{x1, y1, z0}, {x0, y1, z0}, {x0, y0, z0}, {x1, y0, z0}};  // back  -z
    float r[4][3] = {{x1, y1, z1}, {x1, y1, z0}, {x1, y0, z0}, {x1, y0, z1}};  // right +x
    float l[4][3] = {{x0, y1, z0}, {x0, y1, z1}, {x0, y0, z1}, {x0, y0, z0}};  // left  -x
    float t[4][3] = {{x0, y1, z0}, {x1, y1, z0}, {x1, y1, z1}, {x0, y1, z1}};  // top   +y
    float o[4][3] = {{x0, y0, z1}, {x1, y0, z1}, {x1, y0, z0}, {x0, y0, z0}};  // bottom-y
    Quad(m, f, uvF, mat);
    Quad(m, b, uvB, mat);
    Quad(m, r, uvS, mat);
    Quad(m, l, uvS, mat);
    Quad(m, t, uvS, mat);
    Quad(m, o, uvS, mat);
}
} // namespace

std::vector<Vtx> BuildMannequin() {
    std::vector<Vtx> m;
    // Atlas regions (normalised /128), matching BuildMould's schematic zones.
    const UV chestF{0.00f, 0.00f, 0.656f, 0.30f};   // jersey front (top of the chest zone)
    const UV chestB{0.00f, 0.30f, 0.656f, 0.469f};  // jersey back (lower part of the zone)
    const UV sleeve{0.00f, 0.859f, 0.656f, 1.00f};
    const UV pants{0.00f, 0.469f, 0.4375f, 0.859f};
    const UV cap{0.672f, 0.00f, 1.00f, 0.50f};
    const UV none{0, 0, 0, 0};
    const int TEX = 0, SKIN = 1, ACCENT = 2;

    // Legs (hips at y~3.2 down to feet at 0). Slightly apart.
    Box(m, -0.55f, 1.6f, 0.0f, 0.42f, 1.6f, 0.5f, pants, pants, pants, TEX);
    Box(m, 0.55f, 1.6f, 0.0f, 0.42f, 1.6f, 0.5f, pants, pants, pants, TEX);
    // Torso (tapered-ish box). Front/back get jersey, sides jersey too.
    Box(m, 0.0f, 4.5f, 0.0f, 1.15f, 1.35f, 0.62f, chestF, chestB, chestF, TEX);
    // Shoulders slight.
    Box(m, 0.0f, 5.75f, 0.0f, 1.2f, 0.25f, 0.62f, chestF, chestB, chestF, TEX);
    // Arms (down the sides).
    Box(m, -1.5f, 4.55f, 0.0f, 0.33f, 1.25f, 0.42f, sleeve, sleeve, sleeve, TEX);
    Box(m, 1.5f, 4.55f, 0.0f, 0.33f, 1.25f, 0.42f, sleeve, sleeve, sleeve, TEX);
    // Neck + head (small, normal-ish proportions — clearly not a big-head chibi).
    Box(m, 0.0f, 6.2f, 0.0f, 0.28f, 0.3f, 0.28f, none, none, none, SKIN);
    Box(m, 0.0f, 7.0f, 0.05f, 0.62f, 0.6f, 0.6f, none, none, none, SKIN);
    // Cap: crown over the head + a short bill at the front.
    Box(m, 0.0f, 7.55f, 0.02f, 0.66f, 0.28f, 0.64f, cap, cap, cap, TEX);
    Box(m, 0.0f, 7.45f, 0.72f, 0.5f, 0.07f, 0.22f, cap, cap, cap, ACCENT);
    return m;
}

tstudio::Image RenderMesh(const std::vector<Vtx>& mesh, const Image& tex, int w, int h,
                          float yaw, float pitch, float zoom, float centerY,
                          const std::vector<const Image*>* extras, float panX) {
    Image img{static_cast<uint32_t>(w), static_cast<uint32_t>(h),
              std::vector<uint8_t>(static_cast<size_t>(w) * h * 4, 0)};
    std::vector<float> zbuf(static_cast<size_t>(w) * h, 1e30f);
    const float cy = centerY;                     // model centre for rotation (whole figure: 3.7)
    const float dist = 12.0f / std::max(0.2f, zoom);
    const float f = h * 0.95f;                    // focal length in pixels
    const float cyaw = std::cos(yaw), syaw = std::sin(yaw);
    const float cpit = std::cos(pitch), spit = std::sin(pitch);
    const float L[3] = {0.35f, 0.45f, 0.82f};     // light direction (view space)

    auto project = [&](const Vtx& v, float out[3], float& depth) {
        float x = v.x, y = v.y - cy, z = v.z;
        float x1 = cyaw * x + syaw * z, z1 = -syaw * x + cyaw * z;          // yaw about Y
        float y2 = cpit * y - spit * z1, z2 = spit * y + cpit * z1;          // pitch about X
        depth = dist - z2;                                                   // camera looks down +z at origin
        if (depth < 0.1f) depth = 0.1f;
        out[0] = w * 0.5f + f * (x1 - panX) / depth;
        out[1] = h * 0.5f - f * y2 / depth;
        out[2] = depth;
    };

    const int tw = static_cast<int>(tex.width), th = static_cast<int>(tex.height);
    for (size_t i = 0; i + 2 < mesh.size(); i += 3) {
        const Vtx& A = mesh[i];
        const Vtx& B = mesh[i + 1];
        const Vtx& C = mesh[i + 2];
        float sa[3], sb[3], sc[3], da, db, dc;
        project(A, sa, da);
        project(B, sb, db);
        project(C, sc, dc);
        // View-space triangle normal for flat shading (magnitude only -> both sides lit).
        float e1[3] = {B.x - A.x, B.y - A.y, B.z - A.z}, e2[3] = {C.x - A.x, C.y - A.y, C.z - A.z};
        float nx = e1[1] * e2[2] - e1[2] * e2[1], ny = e1[2] * e2[0] - e1[0] * e2[2], nz = e1[0] * e2[1] - e1[1] * e2[0];
        // rotate normal the same way as vertices for a stable light
        float rx = cyaw * nx + syaw * nz, rz = -syaw * nx + cyaw * nz;
        float ry = cpit * ny - spit * rz, rz2 = spit * ny + cpit * rz;
        float nlen = std::sqrt(rx * rx + ry * ry + rz2 * rz2) + 1e-6f;
        float ndl = std::fabs((rx * L[0] + ry * L[1] + rz2 * L[2]) / nlen);
        const float shade = std::min(1.0f, 0.5f + 0.55f * ndl);

        const float minx = std::floor(std::min({sa[0], sb[0], sc[0]})), maxx = std::ceil(std::max({sa[0], sb[0], sc[0]}));
        const float miny = std::floor(std::min({sa[1], sb[1], sc[1]})), maxy = std::ceil(std::max({sa[1], sb[1], sc[1]}));
        const int x0 = std::max(0, static_cast<int>(minx)), x1 = std::min(w - 1, static_cast<int>(maxx));
        const int y0 = std::max(0, static_cast<int>(miny)), y1 = std::min(h - 1, static_cast<int>(maxy));
        const float area = (sb[0] - sa[0]) * (sc[1] - sa[1]) - (sb[1] - sa[1]) * (sc[0] - sa[0]);
        if (std::fabs(area) < 1e-4f) continue;
        if (A.mat == 3 && area < 0) continue;  // a hull's front faces would cover the figure (GX winding, seen)
        for (int y = y0; y <= y1; ++y) {
            for (int x = x0; x <= x1; ++x) {
                const float px = x + 0.5f, py = y + 0.5f;
                float wa = ((sb[0] - px) * (sc[1] - py) - (sb[1] - py) * (sc[0] - px)) / area;
                float wb = ((sc[0] - px) * (sa[1] - py) - (sc[1] - py) * (sa[0] - px)) / area;
                float wc = 1.0f - wa - wb;
                if (wa < 0 || wb < 0 || wc < 0) continue;
                const float depth = wa * da + wb * db + wc * dc;
                const size_t zi = static_cast<size_t>(y) * w + x;
                if (depth >= zbuf[zi]) continue;
                uint8_t cr, cg, cb;
                bool lit = true;
                if (A.mat == 3) {            // outline hull: the game draws it black
                    cr = cg = cb = 0;
                    lit = false;
                } else if (A.mat >= 4) {     // the figure's own textures / the cap letter, alpha-tested
                    const size_t k = static_cast<size_t>(A.mat - 4);
                    const Image* e = extras && k < extras->size() ? (*extras)[k] : nullptr;
                    if (!e || e->Empty()) continue;
                    float u = wa * A.u + wb * B.u + wc * C.u, vv = wa * A.v + wb * B.v + wc * C.v;
                    const int ew = static_cast<int>(e->width), eh = static_cast<int>(e->height);
                    int sx = std::clamp(static_cast<int>(u * ew), 0, ew - 1);
                    int sy = std::clamp(static_cast<int>(vv * eh), 0, eh - 1);
                    const uint8_t* t = &e->rgba[(static_cast<size_t>(sy) * ew + sx) * 4];
                    if (t[3] < 128) continue;
                    cr = t[0]; cg = t[1]; cb = t[2];
                } else if (A.mat == 1) {            // skin
                    cr = 236; cg = 198; cb = 166;
                } else if (A.mat == 2) {     // accent (cap bill): a dark neutral
                    cr = 40; cg = 46; cb = 64;
                } else {                      // textured uniform
                    float u = wa * A.u + wb * B.u + wc * C.u, vv = wa * A.v + wb * B.v + wc * C.v;
                    int sx = std::clamp(static_cast<int>(u * tw), 0, std::max(0, tw - 1));
                    int sy = std::clamp(static_cast<int>(vv * th), 0, std::max(0, th - 1));
                    const uint8_t* t = &tex.rgba[(static_cast<size_t>(sy) * tw + sx) * 4];
                    if (t[3] < 128) { cr = 205; cg = 210; cb = 220; }  // no uniform there -> neutral mannequin
                    else { cr = t[0]; cg = t[1]; cb = t[2]; }
                }
                zbuf[zi] = depth;
                uint8_t* o = &img.rgba[zi * 4];
                const float sh = lit ? shade : 1.0f;
                o[0] = static_cast<uint8_t>(cr * sh);
                o[1] = static_cast<uint8_t>(cg * sh);
                o[2] = static_cast<uint8_t>(cb * sh);
                o[3] = 255;
            }
        }
    }
    return img;
}

bool LoadGameDoll(const std::filesystem::path& file, GameDoll* out) {
    std::ifstream f(file, std::ios::binary);
    if (!f) return false;
    const std::vector<uint8_t> d{std::istreambuf_iterator<char>(f), {}};
    size_t o = 0;
    auto take = [&](void* dst, size_t n) {
        if (o + n > d.size()) return false;
        std::memcpy(dst, d.data() + o, n);
        o += n;
        return true;
    };
    char magic[4];
    uint32_t version = 0, count = 0;
    if (!take(magic, 4) || std::memcmp(magic, "PPDL", 4) || !take(&version, 4) || version != 1 || !take(&count, 4) ||
        count > 64)
        return false;
    GameDoll doll;
    for (uint32_t i = 0; i < count; ++i) {
        uint16_t w = 0, h = 0;
        if (!take(&w, 2) || !take(&h, 2) || w > 1024 || h > 1024) return false;
        Image img{w, h, std::vector<uint8_t>(static_cast<size_t>(w) * h * 4)};
        if (!take(img.rgba.data(), img.rgba.size())) return false;
        doll.textures.push_back(std::move(img));
    }
    uint32_t tris = 0;
    if (!take(&tris, 4) || tris > 200000) return false;
    float lo = 1e30f, hi = -1e30f;
    for (uint32_t t = 0; t < tris; ++t) {
        float v[15];
        uint8_t mat = 0;
        if (!take(v, sizeof v) || !take(&mat, 1)) return false;
        for (int k = 0; k < 3; ++k) {
            doll.mesh.push_back({v[k * 5], v[k * 5 + 1], v[k * 5 + 2], v[k * 5 + 3], v[k * 5 + 4], mat});
            lo = std::min(lo, v[k * 5 + 1]);
            hi = std::max(hi, v[k * 5 + 1]);
        }
    }
    // Same frame as the mannequin: feet at 0, 8 units tall, so the camera settings fit both.
    const float scale = hi > lo ? 8.0f / (hi - lo) : 1.0f;
    for (Vtx& p : doll.mesh) {
        p.x *= scale;
        p.y = (p.y - lo) * scale;
        p.z *= scale;
    }
    doll.meshNoBands = WithoutBands(doll);
    doll.ok = !doll.mesh.empty();
    *out = std::move(doll);
    return out->ok;
}

} // namespace ustudio
