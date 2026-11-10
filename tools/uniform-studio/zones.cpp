// SPDX-License-Identifier: GPL-3.0-or-later
#include "zones.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace ustudio {
namespace {
enum Region : uint8_t { kNone = 0, kShirt, kTrousers, kSleeveZone, kCapCrown, kCapBrim };

// Which piece each palette entry paints (the entries 0x806BDE24 writes for it, pieces.cpp).
std::array<uint8_t, 256> RegionOfIndex() {
    std::array<uint8_t, 256> r{};
    for (int e : {0x80, 0x81, 0x82, 0x88, 0x89, 0x8A, 0x92, 0x93, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7}) r[e] = kShirt;
    for (int e : {0x70, 0x71, 0x72, 0x73, 0x78}) r[e] = kTrousers;
    for (int e : {0x74, 0x75, 0x76, 0x10, 0x11, 0x12, 0x18, 0x30, 0x38}) r[e] = kSleeveZone;
    for (int e : {0x40, 0x41, 0x42, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4A, 0x4B, 0x4D, 0x4E, 0x4F, 0x60, 0x61, 0x62,
                  0x63, 0x64, 0x68})
        r[e] = kCapCrown;
    for (int e : {0x50, 0x51, 0x52, 0x53, 0x54, 0x56, 0x57, 0x58, 0x59, 0x5A, 0x5B, 0x5C, 0x5D, 0x5F}) r[e] = kCapBrim;
    return r;
}

// Front (+1) / back (-1) of each body atlas pixel: the figure's body triangles drawn in UV space, each
// marked by which side of the figure (z, the face looks to +z) its centre is on.
std::vector<int8_t> FacingMap(const GameDoll& doll) {
    std::vector<int8_t> side(128 * 128, 0);
    for (size_t i = 0; i + 2 < doll.mesh.size(); i += 3) {
        const Vtx& a = doll.mesh[i];
        const Vtx& b = doll.mesh[i + 1];
        const Vtx& c = doll.mesh[i + 2];
        if (a.mat != 0) continue;  // the body atlas only
        const float cz = (a.z + b.z + c.z) / 3.0f;
        const int8_t s = cz >= 0 ? 1 : -1;
        const float u[3] = {a.u * 128, b.u * 128, c.u * 128}, v[3] = {a.v * 128, b.v * 128, c.v * 128};
        const float area = (u[1] - u[0]) * (v[2] - v[0]) - (v[1] - v[0]) * (u[2] - u[0]);
        if (std::fabs(area) < 1e-6f) continue;
        const int x0 = std::max(0, static_cast<int>(std::floor(std::min({u[0], u[1], u[2]}))));
        const int x1 = std::min(127, static_cast<int>(std::ceil(std::max({u[0], u[1], u[2]}))));
        const int y0 = std::max(0, static_cast<int>(std::floor(std::min({v[0], v[1], v[2]}))));
        const int y1 = std::min(127, static_cast<int>(std::ceil(std::max({v[0], v[1], v[2]}))));
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x) {
                const float px = x + 0.5f, py = y + 0.5f;
                const float w0 = ((u[1] - px) * (v[2] - py) - (v[1] - py) * (u[2] - px)) / area;
                const float w1 = ((u[2] - px) * (v[0] - py) - (v[2] - py) * (u[0] - px)) / area;
                const float w2 = 1.0f - w0 - w1;
                if (w0 < -0.02f || w1 < -0.02f || w2 < -0.02f) continue;
                side[static_cast<size_t>(y) * 128 + x] = s;
            }
    }
    return side;
}

Zone FromMask(const char* es, const char* en, std::vector<uint8_t> mask) {
    int x0 = 128, y0 = 128, x1 = -1, y1 = -1;
    for (int y = 0; y < 128; ++y)
        for (int x = 0; x < 128; ++x)
            if (mask[static_cast<size_t>(y) * 128 + x]) {
                x0 = std::min(x0, x); y0 = std::min(y0, y); x1 = std::max(x1, x); y1 = std::max(y1, y);
            }
    if (x1 < 0) return {es, en, Kind::Body, 0, 0, 0, 0, {}};
    return {es, en, Kind::Body, static_cast<float>(x0), static_cast<float>(y0), static_cast<float>(x1 + 1),
            static_cast<float>(y1 + 1), std::move(mask)};
}
}  // namespace

std::vector<Zone> BuildZones(const GameBase* base, const GameDoll* doll) {
    std::vector<Zone> zones;
    zones.push_back({"Todo el uniforme", "Whole uniform", Kind::Body, 0, 0, 128, 128, {}});
    if (base && base->ok) {
        const auto region = RegionOfIndex();
        const std::vector<int8_t> side = doll && doll->ok ? FacingMap(*doll) : std::vector<int8_t>(128 * 128, 0);
        auto mask = [&](uint8_t want, int8_t facing) {
            std::vector<uint8_t> m(128 * 128, 0);
            for (size_t i = 0; i < m.size(); ++i)
                m[i] = region[base->index[i]] == want && (facing == 0 || side[i] == facing);
            return m;
        };
        const bool sides = doll && doll->ok;
        std::vector<Zone> body;
        if (sides) {
            body.push_back(FromMask("Pecho (frente)", "Chest (front)", mask(kShirt, 1)));
            body.push_back(FromMask("Espalda", "Back", mask(kShirt, -1)));
        } else {
            body.push_back(FromMask("Camiseta", "Jersey", mask(kShirt, 0)));
        }
        body.push_back(FromMask("Mangas / etc.", "Sleeves / etc.", mask(kSleeveZone, 0)));
        body.push_back(FromMask("Pantalón", "Trousers", mask(kTrousers, 0)));
        body.push_back(FromMask("Gorra", "Cap", mask(kCapCrown, 0)));
        body.push_back(FromMask("Visera", "Brim", mask(kCapBrim, 0)));
        for (Zone& z : body)
            if (!z.mask.empty()) zones.push_back(std::move(z));
    }
    zones.push_back({"Gorra: logo del frente", "Cap: front logo", Kind::Cap, 0, 0, 64, 32, {}});
    zones.push_back({"Casco", "Helmet", Kind::Helmet, 0, 0, 64, 32, {}});
    zones.push_back({"Número", "Number", Kind::Number, 0, 0, 64, 32, {}});
    return zones;
}

}  // namespace ustudio
