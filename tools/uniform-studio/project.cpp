// SPDX-License-Identifier: GPL-3.0-or-later
#include "project.h"

#include "images.h"  // tstudio::LoadImage / SavePng
#undef LoadImage     // windows.h macro, if it leaked in

#include <algorithm>
#include <cstring>
#include <iterator>
#include <fstream>
#include <sstream>

namespace ustudio {
namespace fs = std::filesystem;

namespace {
// Fields are tab-separated, one record per line: keep text free of both.
std::string Clean(std::string s) {
    for (char& c : s)
        if (c == '\t' || c == '\n' || c == '\r') c = ' ';
    return s;
}
std::vector<std::string> Split(const std::string& line) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : line) {
        if (c == '\t') { out.push_back(cur); cur.clear(); }
        else cur += c;
    }
    out.push_back(cur);
    return out;
}
// The thumbnail atlas (kind 4, 64x64 C8 GX-tiled, RGB5A3 palette) of a uniform.ppu, empty if it has none
// (format: tools/uniform-studio/bake.py).
tstudio::Image PpuThumb(const fs::path& ppu) {
    std::ifstream f(ppu, std::ios::binary);
    const std::vector<uint8_t> d{std::istreambuf_iterator<char>(f), {}};
    if (d.size() < 26 || std::string(d.begin(), d.begin() + 6) != "PPUNIF") return {};
    const uint32_t version = static_cast<uint32_t>(d[8] << 24 | d[9] << 16 | d[10] << 8 | d[11]);
    size_t o = 16 + 10;
    if (version == 2) {
        const uint8_t flags = d[o];
        o += 1 + ((flags & 2) ? 6 : 0);
    }
    while (o + 5 <= d.size()) {
        const uint8_t kind = d[o];
        const int w = d[o + 1] | d[o + 2] << 8, h = d[o + 3] | d[o + 4] << 8;
        o += 5;
        if (o + 512 + static_cast<size_t>(w) * h > d.size()) return {};
        if (kind == 4 && w == 64 && h == 64) {
            tstudio::Image img{64, 64, std::vector<uint8_t>(64 * 64 * 4)};
            const uint8_t* px = d.data() + o + 512;
            size_t k = 0;
            for (int by = 0; by < 64; by += 4)
                for (int bx = 0; bx < 64; bx += 8)
                    for (int y = 0; y < 4; ++y)
                        for (int x = 0; x < 8; ++x) {
                            const uint8_t i = px[k++];
                            const uint16_t v = static_cast<uint16_t>(d[o + i * 2] << 8 | d[o + i * 2 + 1]);
                            const auto c = DecodeRgb5a3(v);
                            std::memcpy(&img.rgba[(static_cast<size_t>(by + y) * 64 + bx + x) * 4], c.data(), 4);
                        }
            return img;
        }
        o += 512 + static_cast<size_t>(w) * h;
    }
    return {};
}
}  // namespace

bool SaveProject(const fs::path& dir, const Project& p, std::string* error) {
    std::error_code ec;
    fs::create_directories(dir, ec);
    for (const auto& e : fs::directory_iterator(dir, ec))  // drop the previous save's layer images
        if (e.path().extension() == ".png" && e.path().filename() != "thumb.png") fs::remove(e.path(), ec);
    std::ostringstream o;
    o << "PPSTUDIO\t1\n";
    o << "name\t" << Clean(p.name) << "\n";
    o << "pieces\t" << (p.piecesOn ? 1 : 0);
    for (int c : p.pieces.color) o << "\t" << c;
    o << "\t" << p.pieces.lineType << "\t" << p.pieces.number << "\t" << p.pieces.outline << "\t" << p.pieces.previewNumber
      << "\t" << p.pieces.chest << "\t" << p.pieces.chestDx << "\t" << p.pieces.chestDy << "\t" << p.pieces.backDx << "\t"
      << p.pieces.backDy << "\t" << p.pieces.backSize << "\n";
    o << "rgb";
    for (int c : p.pieces.rgb) o << "\t" << c;
    o << "\n";
    o << "stripes\t" << p.pieces.stripes << "\t" << p.pieces.stripeGap << "\t" << p.pieces.stripe2 << "\t"
      << (p.pieces.stripeTrousers ? 1 : 0) << "\t" << p.pieces.stripe1 << "\n";
    o << "backup";
    for (int b : p.backup) o << "\t" << b;
    o << "\n";
    for (int k = 0; k < kKindCount; ++k) {
        const Canvas& c = p.canvases[static_cast<size_t>(k)];
        for (size_t i = 0; i < c.layers.size(); ++i) {
            const Layer& L = c.layers[i];
            std::string png = "-";
            if (!L.isText && !L.src.Empty()) {
                png = "layer_" + std::to_string(k) + "_" + std::to_string(i) + ".png";
                if (!tstudio::SavePng(dir / png, L.src, error)) return false;
            }
            o << "layer\t" << k << "\t" << L.visible << "\t" << L.isText << "\t" << L.isBase << "\t" << L.x << "\t" << L.y
              << "\t" << L.scale << "\t" << L.sx << "\t" << L.sy << "\t" << L.fontPx << "\t" << int(L.color[0]) << "\t"
              << int(L.color[1]) << "\t" << int(L.color[2]) << "\t" << int(L.color[3]) << "\t" << png << "\t"
              << Clean(L.name) << "\t" << Clean(L.text) << "\t" << L.isPaint << "\t" << L.rot << "\n";
        }
    }
    std::ofstream f(dir / "project.txt", std::ios::binary);
    f << o.str();
    if (!f) {
        if (error) *error = "project.txt";
        return false;
    }
    return true;
}

bool LoadProject(const fs::path& dir, Project* out, std::string* error) {
    std::ifstream f(dir / "project.txt", std::ios::binary);
    std::string line;
    if (!f || !std::getline(f, line) || line.rfind("PPSTUDIO\t1", 0) != 0) {
        if (error) *error = "project.txt";
        return false;
    }
    Project p;
    for (int k = 0; k < kKindCount; ++k) p.canvases[static_cast<size_t>(k)].kind = static_cast<Kind>(k);
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const auto v = Split(line);
        auto num = [&](size_t i) { return i < v.size() ? std::atof(v[i].c_str()) : 0.0; };
        if (v[0] == "name" && v.size() > 1) {
            p.name = v[1];
        } else if (v[0] == "pieces" && v.size() >= 19) {
            p.piecesOn = num(1) != 0;
            for (size_t i = 0; i < p.pieces.color.size(); ++i) p.pieces.color[i] = std::clamp(static_cast<int>(num(2 + i)), 0, 31);
            p.pieces.lineType = std::clamp(static_cast<int>(num(8)), 0, 2);
            p.pieces.number = std::clamp(static_cast<int>(num(9)), 0, 31);
            p.pieces.outline = std::clamp(static_cast<int>(num(10)), 0, 31);
            p.pieces.previewNumber = std::clamp(static_cast<int>(num(11)), 0, 999);
            p.pieces.chest = std::clamp(static_cast<int>(num(12)), 0, 9);
            p.pieces.chestDx = static_cast<int>(num(13));
            p.pieces.chestDy = static_cast<int>(num(14));
            p.pieces.backDx = static_cast<int>(num(15));
            p.pieces.backDy = static_cast<int>(num(16));
            p.pieces.backSize = std::clamp(static_cast<int>(num(17)), 25, 200);
        } else if (v[0] == "rgb") {  // free colours per piece (-1 = the game colour)
            for (size_t i = 0; i < p.pieces.rgb.size() && i + 1 < v.size(); ++i)
                p.pieces.rgb[i] = std::clamp(static_cast<int>(num(1 + i)), -1, 0xFFFFFF);
        } else if (v[0] == "stripes" && v.size() >= 5) {  // the Studio's own stripes (pieces.h)
            p.pieces.stripes = std::clamp(static_cast<int>(num(1)), 0, 3);
            p.pieces.stripeGap = std::clamp(static_cast<int>(num(2)), 3, 32);
            p.pieces.stripe2 = std::clamp(static_cast<int>(num(3)), 0, 0xFFFFFF);
            p.pieces.stripeTrousers = num(4) != 0;
            if (v.size() > 5) p.pieces.stripe1 = std::clamp(static_cast<int>(num(5)), 0, 0xFFFFFF);
        } else if (v[0] == "backup" && v.size() >= 11) {
            for (size_t i = 0; i < 10; ++i) p.backup[i] = std::clamp(static_cast<int>(num(1 + i)), 0, 31);
        } else if (v[0] == "layer" && v.size() >= 18) {
            const int k = static_cast<int>(num(1));
            if (k < 0 || k >= kKindCount) continue;
            Layer L;
            L.visible = num(2) != 0;
            L.isText = num(3) != 0;
            L.isBase = num(4) != 0;
            L.x = static_cast<float>(num(5));
            L.y = static_cast<float>(num(6));
            L.scale = static_cast<float>(num(7));
            L.sx = static_cast<float>(num(8));
            L.sy = static_cast<float>(num(9));
            L.fontPx = static_cast<float>(num(10));
            for (int c = 0; c < 4; ++c) L.color[c] = static_cast<unsigned char>(std::clamp(static_cast<int>(num(11 + c)), 0, 255));
            if (v[15] != "-") {
                auto img = tstudio::LoadImage(dir / v[15], error);
                if (!img) return false;
                L.src = std::move(*img);
            }
            L.name = v[16];
            L.text = v[17];
            L.isPaint = num(18) != 0;  // older projects: no column, not a drawing
            L.rot = static_cast<float>(num(19));  // older projects: not turned
            p.canvases[static_cast<size_t>(k)].layers.push_back(std::move(L));
        }
    }
    *out = std::move(p);
    return true;
}

std::vector<SavedUniform> ListProjects(const fs::path& root) {
    std::vector<SavedUniform> out;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(root, ec)) {
        const fs::path studio = e.path() / "studio";
        if (!e.is_directory(ec)) continue;
        if (fs::exists(studio / "project.txt", ec)) {
            SavedUniform u{tstudio::Utf8(e.path().filename()), studio, {}};
            std::string err;
            if (auto img = tstudio::LoadImage(studio / "thumb.png", &err)) u.thumb = std::move(*img);
            out.push_back(std::move(u));
        } else if (fs::exists(e.path() / "uniform.ppu", ec)) {
            SavedUniform u{tstudio::Utf8(e.path().filename()), {}, {}};
            u.legacy = true;
            u.ppu = e.path() / "uniform.ppu";
            u.thumb = PpuThumb(u.ppu);
            out.push_back(std::move(u));
        }
    }
    std::sort(out.begin(), out.end(), [](const SavedUniform& a, const SavedUniform& b) { return a.name < b.name; });
    return out;
}

}  // namespace ustudio
