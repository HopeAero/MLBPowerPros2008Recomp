// SPDX-License-Identifier: GPL-3.0-or-later
#include "render3d.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace ustudio {
using tstudio::Image;

namespace {
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
                          float yaw, float pitch, float zoom) {
    Image img{static_cast<uint32_t>(w), static_cast<uint32_t>(h),
              std::vector<uint8_t>(static_cast<size_t>(w) * h * 4, 0)};
    std::vector<float> zbuf(static_cast<size_t>(w) * h, 1e30f);
    const float cy = 3.7f;                        // model centre for rotation
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
        out[0] = w * 0.5f + f * x1 / depth;
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
                if (A.mat == 1) {            // skin
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
                o[0] = static_cast<uint8_t>(cr * shade);
                o[1] = static_cast<uint8_t>(cg * shade);
                o[2] = static_cast<uint8_t>(cb * shade);
                o[3] = 255;
            }
        }
    }
    return img;
}

} // namespace ustudio
