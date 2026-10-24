// SPDX-License-Identifier: GPL-3.0-or-later
// TextureStudio.exe --selftest: synthetic data only (no game content). Prints the first failures.
#include "capture.h"
#include "catalog.h"
#include "categories.h"
#include "config.h"
#include "core.h"
#include "gx.h"
#include "images.h"
#include "lang.h"
#include "modfolder.h"
#include "names.h"
#include "pack.h"
#include "projectbin.h"
#include "transfer.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>

namespace tstudio {
namespace {
int g_checks = 0, g_failures = 0;
#define CHECK(cond)                                                                       \
    do {                                                                                  \
        ++g_checks;                                                                       \
        if (!(cond)) {                                                                    \
            ++g_failures;                                                                 \
            std::fprintf(stderr, "selftest: %s:%d: %s\n", __FILE__, __LINE__, #cond);     \
        }                                                                                 \
    } while (0)

uint32_t Pixel(const Image& img, uint32_t x, uint32_t y) {  // 0xRRGGBBAA
    const uint8_t* p = &img.rgba[(static_cast<size_t>(y) * img.width + x) * 4];
    return static_cast<uint32_t>(p[0]) << 24 | p[1] << 16 | p[2] << 8 | p[3];
}

void TestGx() {
    CHECK(TextureBytes(8, 4, 1) == 32);       // I8: one 8x4 block
    CHECK(TextureBytes(4, 4, 9) == 32);       // C8 4x4 pads to one 8x4 block
    CHECK(TextureBytes(8, 8, 8) == 32);       // C4: one 8x8 block
    CHECK(TextureBytes(4, 4, 6) == 64);       // RGBA8: one 4x4 block, two 32-byte halves
    CHECK(TextureBytes(512, 512, 9) == 512 * 512);
    CHECK(TextureBytes(16, 16, 7) == 0);      // unknown format

    // I8 16x4 = two blocks side by side: block 0 holds x 0..7, block 1 holds x 8..15.
    std::vector<uint8_t> i8(64);
    for (size_t i = 0; i < i8.size(); ++i) i8[i] = static_cast<uint8_t>(i);
    const auto v = Values(i8, 16, 4, 1);
    CHECK(v.size() == 64);
    CHECK(v[0] == 0 && v[7] == 7 && v[8] == 32 && v[16] == 8);  // (8,0) starts block 1; (0,1) = 8
    const Image grey = Decode(i8, 16, 4, 1, {});
    CHECK(grey.width == 16 && grey.height == 4 && Pixel(grey, 8, 0) == 0x202020FFu);

    // C4 8x8: high nibble first.
    std::vector<uint8_t> c4(32, 0x12);
    const auto n = Values(c4, 8, 8, 8);
    CHECK(n[0] == 1 && n[1] == 2);
    const uint8_t pal[] = {0x80, 0x00, 0xFC, 0x00, 0x83, 0xE0};  // black, red, green (RGB5A3 opaque)
    const Image c4img = Decode(c4, 8, 8, 8, pal);
    CHECK(Pixel(c4img, 0, 0) == 0xFF0000FFu && Pixel(c4img, 1, 0) == 0x00FF00FFu);

    // RGB5A3: opaque white, and translucent 0x7F00 (a=7 -> 255, r=15 -> 255).
    const uint8_t rgb5a3[32] = {0xFF, 0xFF, 0x7F, 0x00};
    const Image c = Decode(rgb5a3, 4, 4, 5, {});
    CHECK(Pixel(c, 0, 0) == 0xFFFFFFFFu && Pixel(c, 1, 0) == 0xFF0000FFu);

    // RGBA8 4x4: AR pairs in the first 32 bytes, GB pairs in the next 32.
    std::vector<uint8_t> rgba8(64);
    rgba8[0] = 0x80; rgba8[1] = 0x11; rgba8[32] = 0x22; rgba8[33] = 0x33;
    CHECK(Pixel(Decode(rgba8, 4, 4, 6, {}), 0, 0) == 0x11223380u);

    CHECK(Decode(std::span<const uint8_t>(i8.data(), 10), 16, 4, 1, {}).Empty());  // short data
    CHECK(Decode(i8, 16, 4, 3, {}).Empty());                                        // IA8: not decodable
}
// A synthetic PACKWII: header, one layout C8 texture with its palette pointer, one model C4 texture
// whose palette follows it (+0x24), and one texture whose pixels run past the end (ignored).
std::vector<uint8_t> SyntheticPack() {
    std::vector<uint8_t> d(0x400, 0);
    std::memcpy(d.data(), "PACKWII ", 8);
    d[0x17] = 0x20;
    auto put16 = [&](size_t o, uint16_t v) { d[o] = v >> 8; d[o + 1] = v & 0xFF; };
    auto put32 = [&](size_t o, uint32_t v) { for (int i = 0; i < 4; ++i) d[o + i] = (v >> (24 - 8 * i)) & 0xFF; };
    // layout texture at 0x40: 8 wide, 4 high C8 (header: u16 height, u16 width), pixels at 0x100,
    // palette (256 entries) at 0x200 via +0x2C
    put16(0x40, 4); put16(0x42, 8); put32(0x44, 9); put32(0x48, 0x40000100);
    put32(0x54, 1); put32(0x58, 1); put16(0x64, 256); put32(0x68, 2); put32(0x6C, 0x40000200);
    // model texture at 0x80: 8x8 C4 (type 0x84), pixels at 0x140, palette header at 0xA4 -> data 0x160
    put16(0x80, 8); put16(0x82, 8); d[0x84] = 0x84; put32(0x88, 0x40000140);
    put16(0xA4, 16); d[0xA8] = 0x81; put32(0xAC, 0x40000160);
    // broken texture at 0xC0: 1024x1024 RGBA8 whose pixels would end past the file
    put16(0xC0, 1024); put16(0xC2, 1024); put32(0xC4, 6); put32(0xC8, 0x40000180);
    return d;
}

void TestPack() {
    const auto d = SyntheticPack();
    const auto t = FindTextures(d);
    CHECK(t.size() == 2);
    if (t.size() != 2) return;
    CHECK(t[0].offset == 0x40 && t[0].format == 9 && t[0].data == 0x100 && !t[0].model);
    CHECK(t[0].width == 8 && t[0].height == 4);
    CHECK(t[0].paletteEntries == 256 && t[0].paletteData == 0x200 && !t[0].paletteByPosition);
    CHECK(t[1].offset == 0x80 && t[1].format == 8 && t[1].model);
    CHECK(t[1].paletteEntries == 16 && t[1].paletteData == 0x160 && t[1].paletteByPosition);
    CHECK(FindTextures(std::span<const uint8_t>(d.data(), 0x30)).empty());   // truncated
    std::vector<uint8_t> other(d);
    other[0] = 'X';
    CHECK(FindTextures(other).empty());                                       // not PACKWII
}
// A synthetic project.bin with two entries, laid out like the real one (tools/projectbin.py).
std::vector<uint8_t> SyntheticProject() {
    std::vector<uint8_t> d(0x3000, 0);
    auto put32 = [&](size_t o, uint32_t v) { for (int i = 0; i < 4; ++i) d[o + i] = (v >> (24 - 8 * i)) & 0xFF; };
    put32(0x00, 2); put32(0x04, 0x800);
    put32(0x14, 0x100);   // records
    put32(0x28, 0x200);   // order
    put32(0x2C, 0x300);   // names
    put32(0x34, 0x800);   // data
    const char names[] = "\0/a.pack\0/b.dat\0";
    std::memcpy(&d[0x300], names, sizeof(names));
    // record 1: name 1, 1 sector, size 5; record 2: name 9, 1 sector, size 3
    put32(0x100 + 0x1C + 4, 1); put32(0x100 + 0x1C + 8, 1); put32(0x100 + 0x1C + 24, 5);
    put32(0x100 + 0x38 + 4, 9); put32(0x100 + 0x38 + 8, 1); put32(0x100 + 0x38 + 24, 3);
    // order: pair 0 unused, (1, sector 0), (2, sector 1), (0, 0)
    put32(0x208, 1); put32(0x20C, 0); put32(0x210, 2); put32(0x214, 1);
    std::memcpy(&d[0x800], "HELLO", 5);
    std::memcpy(&d[0x1000], "abc", 3);
    return d;
}

void TestProjectAndNames(const std::filesystem::path& temp) {
    const auto bytes = SyntheticProject();
    const auto file = temp / "project.bin";
    std::ofstream(file, std::ios::binary).write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    std::string error;
    const auto index = ReadProjectIndex(file, &error);
    CHECK(index && index->size() == 2);
    CHECK(index && (*index)[0].name == "a.pack" && (*index)[0].offset == 0x800 && (*index)[0].size == 5);
    CHECK(index && (*index)[1].name == "b.dat" && (*index)[1].offset == 0x1000);
    CHECK(index && std::string(reinterpret_cast<const char*>(ReadEntry(file, (*index)[0]).data()), 5) == "HELLO");
    const auto cut = temp / "cut.bin";
    std::ofstream(cut, std::ios::binary).write(reinterpret_cast<const char*>(bytes.data()), 0x20);
    CHECK(!ReadProjectIndex(cut, &error) && !error.empty());

    // Hashes (reference values from Python's xxhash, seed 0).
    std::vector<uint8_t> i8(32);
    for (size_t i = 0; i < i8.size(); ++i) i8[i] = static_cast<uint8_t>(i);
    CHECK(TextureHash(i8, 8, 4, 1) == 0xcbf59c5116ff32b4ull);
    const std::vector<uint8_t> c4(32, 0x12);
    CHECK(TextureHash(c4, 8, 8, 8) == 0xc2e5e11911ded5deull);
    const uint8_t pal[] = {0x80, 0x00, 0x81, 0x11, 0x82, 0x22, 0x83, 0x33};
    CHECK(PaletteHash(c4, 8, 8, 8, pal) == 0x9ceb41977776c426ull);              // indices 1..2 only
    CHECK(!PaletteHash(c4, 8, 8, 8, std::span<const uint8_t>(pal, 4)));          // palette too short

    ParsedName n{256, 128, 9, 0x0123456789abcdefull, true, false, 0xfedcba9876543210ull};
    CHECK(ReplacementName(n) == "tex1_256x128_0123456789abcdef_fedcba9876543210_9");
    n.paletteWildcard = true;
    CHECK(ReplacementName(n) == "tex1_256x128_0123456789abcdef_$_9");
    n.hasPalette = false;
    CHECK(ReplacementName(n) == "tex1_256x128_0123456789abcdef_9");
    const auto p = ParseName("tex1_64x32_00000000000000aa_0000000000000bbb_8");
    CHECK(p && p->width == 64 && p->height == 32 && p->textureHash == 0xaa && p->hasPalette && p->paletteHash == 0xbbb && p->format == 8);
    const auto q = ParseName("tex1_64x32_00000000000000aa_$_9");
    CHECK(q && q->paletteWildcard);
    CHECK(!ParseName("hola_64x32_aa_9") && !ParseName("tex1_64x32_zz_9") && !ParseName("tex1_64_aa_9"));
}
void TestImages(const std::filesystem::path& temp) {
    Image img{3, 2, {}};
    for (int i = 0; i < 6; ++i) img.rgba.insert(img.rgba.end(), {uint8_t(i * 40), 7, 200, uint8_t(255 - i)});
    const auto png = temp / PathFromUtf8("año.png");   // non-ASCII file name (Review Focus 1)
    std::string error;
    CHECK(SavePng(png, img, &error));
    const auto back = LoadImage(png, &error);
    CHECK(back && back->width == 3 && back->height == 2 && back->rgba == img.rgba);

    // RGBA8 DDS exactly as aurora's encode_rgba8_dds writes it.
    std::vector<uint8_t> dds(128 + 4 * 2 * 1, 0);
    std::memcpy(dds.data(), "DDS ", 4);
    auto le = [&](size_t o, uint32_t v) { for (int i = 0; i < 4; ++i) dds[o + i] = (v >> (8 * i)) & 0xFF; };
    le(4, 124); le(12, 1); le(16, 2); le(76, 32); le(80, 0x41); le(88, 32);
    le(92, 0xFF); le(96, 0xFF00); le(100, 0xFF0000); le(104, 0xFF000000u);
    const uint8_t px[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    std::memcpy(dds.data() + 128, px, 8);
    const auto ddsFile = temp / "t.dds";
    std::ofstream(ddsFile, std::ios::binary).write(reinterpret_cast<const char*>(dds.data()), dds.size());
    const auto d = LoadImage(ddsFile, &error);
    CHECK(d && d->width == 2 && d->height == 1 && d->rgba == std::vector<uint8_t>(px, px + 8));

    std::ofstream(temp / "roto.png", std::ios::binary) << "not a png";
    CHECK(!LoadImage(temp / "roto.png", &error) && !error.empty());

    CHECK(CheckReplacementSize(256, 128, 1024, 512) == SizeCheck::Ok);
    CHECK(CheckReplacementSize(256, 128, 256, 128) == SizeCheck::Ok);
    CHECK(CheckReplacementSize(256, 128, 600, 300) == SizeCheck::NotMultiple);
    CHECK(CheckReplacementSize(256, 128, 1024, 1024) == SizeCheck::WrongAspect);
    CHECK(CheckReplacementSize(1024, 1024, 8192, 8192) == SizeCheck::TooLarge);
}
void TestCategories() {
    auto is = [](std::string_view pack, const char* g, const char* s) {
        const Category c = Categorize(pack);
        return c.group == g && c.subgroup == s;
    };
    CHECK(is("stadium_nyy.pack", "Estadios", "Yankees (NYY)"));
    CHECK(is("stadium_nyytbl.pack", "Estadios", "Yankees (NYY)"));
    CHECK(is("stadium_oka.pack", "Estadios", "Athletics (OAK)"));
    CHECK(is("stadium_sky_rainy.pack", "Estadios", "Cielos"));
    CHECK(is("stadium_org03.pack", "Estadios", "Original 3"));
    CHECK(is("PWM_mdl_uniform_MLB_CHC_00.pack", "Uniformes", "Cubs (CHC)"));
    CHECK(is("PWM_mdl_uniform_AAA_ARI_AAA_01.pack", "Uniformes", "Diamondbacks (ARI) AAA"));
    CHECK(is("env_player_card_03NYY_YANKEES.pack", "Cartas de jugador", "Yankees (NYY)"));
    CHECK(is("env_mainmenu.pack", "Menús", "Menú principal"));
    CHECK(is("env_shop.pack", "Menús", "shop"));
    CHECK(is("env_teamlogo_aaa01.pack", "Logos", "teamlogo_aaa01"));
    CHECK(is("mylife_bg_house01.pack", "MLB Life", "bg_house01"));
    CHECK(is("season_main_bg.pack", "Temporada", "main_bg"));
    CHECK(is("ScsBG__01.pack", "Success", "ScsBG__01"));
    CHECK(is("PWM_mdl_face_001.pack", "Caras", "face_001"));
    CHECK(is("raro.pack", "Otros", "raro"));
    CHECK(GroupOrder().front() == "Menús");
}
// Fills `c` from a synthetic project.bin (Catalog holds a mutex: not movable, so filled in place):
// stadium_nyy.pack and stadium_season.pack (same synthetic pack) and env_mainmenu.pack (C8 pixels changed).
void SyntheticCatalog(const std::filesystem::path& temp, Catalog& c) {
    const auto pack = SyntheticPack();
    std::vector<uint8_t> d(0x800, 0);
    auto put32 = [&](size_t o, uint32_t v) { for (int i = 0; i < 4; ++i) d[o + i] = (v >> (24 - 8 * i)) & 0xFF; };
    put32(0x14, 0x100); put32(0x28, 0x200); put32(0x2C, 0x300); put32(0x34, 0x800);
    const char names[] = "\0/stadium_nyy.pack\0/stadium_season.pack\0/env_mainmenu.pack\0";
    std::memcpy(&d[0x300], names, sizeof(names));
    const uint32_t nameOff[] = {1, 19, 40};
    for (uint32_t i = 0; i < 3; ++i) {
        const size_t rec = 0x100 + 0x1C * (i + 1);
        put32(rec + 4, nameOff[i]); put32(rec + 8, 1); put32(rec + 24, static_cast<uint32_t>(pack.size()));
        put32(0x208 + 8 * i, i + 1); put32(0x20C + 8 * i, i);
    }
    for (int i = 0; i < 3; ++i) d.insert(d.end(), pack.begin(), pack.end()), d.resize(0x800 + 0x800 * (i + 1), 0);
    d[0x800 + 2 * 0x800 + 0x100] = 0x55;  // env_mainmenu: different pixels for the C8 texture...
    for (int i = 0; i < 4; ++i) d[0x800 + 2 * 0x800 + 0x6C + i] = 0;  // ...and no palette (exports in grey)
    const auto file = temp / "catalog.bin";
    std::ofstream(file, std::ios::binary).write(reinterpret_cast<const char*>(d.data()), d.size());
    std::string error;
    std::atomic<float> progress{0};
    CHECK(c.Build(file, &progress, &error));
    CHECK(progress == 1.0f);
}

void TestCatalog(const std::filesystem::path& temp) {
    Catalog c;
    SyntheticCatalog(temp, c);
    CHECK(c.Textures().size() == 3);   // C8 (nyy+season), C4 (all three), C8 (mainmenu)
    if (c.Textures().size() != 3) return;
    const int shared = c.Find(c.Textures()[0].name);
    CHECK(shared == 0 && c.Textures()[0].locations.size() == 2 && c.Textures()[0].group == "Estadios");
    const auto& c4 = c.Textures()[1];
    CHECK(c4.key.format == 8 && c4.locations.size() == 3 && c4.key.hasPalette && !c4.key.paletteWildcard);
    const Image img = c.Original(c4);
    CHECK(img.width == 8 && img.height == 8);

    // A capture: one exact match, one palette variant of the C4 texture, one game-only texture, one video frame.
    const auto session = temp / "captures" / "1";
    std::filesystem::create_directories(session);
    ParsedName variant = c4.key;
    variant.paletteHash ^= 1;
    ParsedName only{16, 16, 5, 0x1234};
    ParsedName video{512, 448, 1, 0x99};
    const std::vector<std::filesystem::path> files{session / (c4.name + ".dds"), session / (ReplacementName(variant) + ".dds"),
                                                   session / (ReplacementName(only) + ".dds"), session / (ReplacementName(video) + ".dds")};
    c.AddCapture(1, files);
    CHECK(c.Textures()[1].seenIn == std::vector<int>{1});
    CHECK(c.Textures()[1].paletteVariants.size() == 1);
    const int game = c.Find(ReplacementName(only));
    CHECK(game >= 0 && c.Textures()[game].group == "Solo vistas en juego" && !c.Textures()[game].captureFile.empty());
    CHECK(c.Find(ReplacementName(video)) == -1);
    ParsedName any = c.Textures()[1].key;
    any.paletteWildcard = true;
    CHECK(c.FindTexture(any) == 1);
    CHECK(IsVideoFrame(video) && !IsVideoFrame(only));
}
void TestConfigAndMods(const std::filesystem::path& temp) {
    const auto config = temp / "Config.toml";
    std::ofstream(config) << R"([paths]
dvd_root = 'D:\Juegos\MLB\Data'

[video]
show_fps = true
)";
    CHECK(ReadSetting(config, "paths", "dvd_root") == std::optional<std::string>(R"(D:\Juegos\MLB\Data)"));
    CHECK(ReadEnabledMods(config).empty());
    CHECK(SetModEnabled(config, "HD Menús", true));
    CHECK(SetModEnabled(config, "Otro", true));
    CHECK((ReadEnabledMods(config) == std::vector<std::string>{"HD Menús", "Otro"}));
    CHECK(SetModEnabled(config, "Otro", false));
    CHECK((ReadEnabledMods(config) == std::vector<std::string>{"HD Menús"}));
    CHECK(ReadSetting(config, "video", "show_fps") == std::optional<std::string>("true"));  // untouched

    std::string error;
    CHECK(ValidModName("  HD Menús ñ  ", &error) == std::optional<std::string>("HD Menús ñ"));
    for (const char* bad : {"", "   ", "a/b", "a\b", "CON", "x:y", "nombre.", "..", "a*b"})
        CHECK(!ValidModName(bad, &error));
    const auto mods = temp / "mods";
    CHECK(CreateMod(mods, "HD Menús ñ", &error));
    CHECK(!CreateMod(mods, "HD Menús ñ", &error) && error.find("existe") != std::string::npos);
    CHECK(std::filesystem::exists(mods / PathFromUtf8("HD Menús ñ") / "mod.toml"));
    const auto list = ListMods(mods, {"HD Menús ñ"});
    CHECK(list.size() == 1 && list[0].enabled);

    CatalogTexture t{"tex1_8x8_00000000000000aa_9", {}, "Estadios", "Yankees (NYY)"};
    const auto root = mods / PathFromUtf8("HD Menús ñ");
    const auto path = ReplacementPath(root, t, t.name);
    const auto rel = path.lexically_relative(root).generic_u8string();
    CHECK(std::string(rel.begin(), rel.end()) == "textures/Estadios/Yankees (NYY)/tex1_8x8_00000000000000aa_9.png");
    std::filesystem::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << "png";
    std::ofstream(root / "textures" / "nota.txt") << "x";
    const auto found = FindReplacements(root);
    CHECK(found.size() == 1 && found.count(t.name));

    const auto zip = temp / "mod.zip";
    CHECK(WriteZip(root, zip, &error));
    std::ifstream z(zip, std::ios::binary);
    const std::string bytes((std::istreambuf_iterator<char>(z)), {});
    CHECK(bytes.substr(0, 4) == std::string("PK\x03\x04", 4));
    CHECK(bytes.find("HD Menús ñ/mod.toml") != std::string::npos);
    CHECK(bytes.find("tex1_8x8_00000000000000aa_9.png") != std::string::npos);
    CHECK(bytes.find("nota.txt") == std::string::npos);    // only mod.toml and replacement images
    CHECK(bytes.find(std::string("PK\x05\x06", 4)) != std::string::npos);
}
void TestTransfer(const std::filesystem::path& temp) {
    Catalog c;
    SyntheticCatalog(temp, c);
    if (c.Textures().size() != 3) return;
    const auto modRoot = temp / "mods" / "Prueba";
    std::filesystem::create_directories(modRoot / "textures");
    const CatalogTexture& t = c.Textures()[1];                     // 8x8 C4
    const auto exportDir = temp / PathFromUtf8("exportación");
    std::vector<std::string> problems;
    CHECK(Export(c, {0, 1, 2}, exportDir, &problems) == 3 && problems.empty());
    const auto exported = exportDir / PathFromUtf8(t.group) / PathFromUtf8(t.subgroup) / PathFromUtf8(t.name + ".png");
    CHECK(std::filesystem::exists(exported));

    // x4 upscale of the exported image: accepted and written under the mod.
    Image big{32, 32, std::vector<uint8_t>(32 * 32 * 4, 200)};
    const auto bigFile = temp / "big.png";
    CHECK(SavePng(bigFile, big, nullptr));
    const auto r = ImportOne(c, 1, bigFile, modRoot, false);
    CHECK(r.ok && std::filesystem::exists(ReplacementPath(modRoot, t, t.name)));
    // Wrong aspect (32x16 onto the 8x8 texture): rejected.
    Image wide{32, 16, std::vector<uint8_t>(32 * 16 * 4, 1)};
    CHECK(SavePng(temp / "wide.png", wide, nullptr));
    const auto bad = ImportOne(c, 1, temp / "wide.png", modRoot, false);
    CHECK(!bad.ok && bad.message.find("proporción") != std::string::npos);

    // Folder import: one good file, one unknown tex1 name, one random file, a .png/.dds pair, a "$" name.
    const auto in = temp / "chainner";
    std::filesystem::create_directories(in / "sub");
    const ParsedName& k2 = c.Textures()[2].key;
    Image ok2{k2.width * 2, k2.height * 2, std::vector<uint8_t>(static_cast<size_t>(k2.width) * k2.height * 16, 9)};
    CHECK(SavePng(in / "sub" / PathFromUtf8(c.Textures()[2].name + ".png"), ok2, nullptr));
    std::ofstream(in / PathFromUtf8(c.Textures()[2].name + ".dds"), std::ios::binary) << "dup";
    CHECK(SavePng(in / "tex1_8x8_0000000000000001_5.png", Image{8, 8, std::vector<uint8_t>(256, 1)}, nullptr));
    std::ofstream(in / "foto.png", std::ios::binary) << "x";
    ParsedName any = t.key;                                         // "$" name: every palette of texture 1
    any.paletteWildcard = true;
    CHECK(SavePng(in / PathFromUtf8(ReplacementName(any) + ".png"), Image{16, 16, std::vector<uint8_t>(16 * 16 * 4, 3)}, nullptr));
    const auto report = ImportFolder(c, in, modRoot);
    int good = 0, unknown = 0, unnamed = 0, duplicate = 0;
    for (const auto& x : report) {
        good += x.ok;
        unknown += x.message.find("ninguna textura") != std::string::npos;
        unnamed += x.message.find("no reconocido") != std::string::npos;
        duplicate += x.message.find("Repetido") != std::string::npos;
    }
    CHECK(good == 2 && unknown == 1 && unnamed == 1 && duplicate == 1);
    CHECK(FindReplacements(modRoot).count(ReplacementName(any)) == 1);
}
void TestCapture(const std::filesystem::path& temp) {
    const auto dumps = temp / "texture_dumps";
    const auto captures = temp / "captures-test";
    std::filesystem::create_directories(dumps);
    std::ofstream(dumps / "tex1_8x8_0000000000000001_5.dds") << "old";   // existed before the capture
    Capture cap;
    cap.SeedForTest(dumps);                                                // records "before" without a process
    std::ofstream(dumps / "tex1_8x8_0000000000000002_5.dds") << "new";
    auto [n, files] = cap.Collect(captures);
    CHECK(n == 1 && files.size() == 1 && files[0].filename() == "tex1_8x8_0000000000000002_5.dds");
    CHECK(std::filesystem::exists(dumps / "tex1_8x8_0000000000000001_5.dds"));
    Capture empty;
    empty.SeedForTest(temp / "no-existe");                                 // Review Focus 5
    CHECK(empty.Collect(captures).first == 0);
    const auto sessions = LoadSessions(captures);
    CHECK(sessions.size() == 1 && sessions[0].first == 1 && sessions[0].second.size() == 1);
}
// Final review fixes.
void TestReviewFixes(const std::filesystem::path& temp) {
    // ParseName: 0 or > 65535 dimensions are not texture names (they divided by zero on import).
    CHECK(!ParseName("tex1_0x0_00000000000000aa_5") && !ParseName("tex1_70000x8_00000000000000aa_5"));

    // ReadEntry: an entry larger than the file is refused without allocating it.
    const auto small = temp / "small.bin";
    std::ofstream(small, std::ios::binary) << "abc";
    CHECK(ReadEntry(small, ProjectEntry{"x.pack", 0, 0xFFFFFFF0u}).empty());
    CHECK(ReadEntry(small, ProjectEntry{"x.pack", 1, 2}).size() == 2);

    Catalog c;
    SyntheticCatalog(temp, c);
    if (c.Textures().size() != 3) return;
    const auto root = temp / "mods" / "Revision";
    std::filesystem::create_directories(root / "textures");
    const CatalogTexture& t = c.Textures()[1];  // 8x8 C4 with a known palette
    Image img{16, 16, std::vector<uint8_t>(16 * 16 * 4, 7)};
    const auto png = temp / "rev.png";
    CHECK(SavePng(png, img, nullptr));

    // "Aplicar a todas las paletas": the $ file is the texture's replacement too, and removal clears it.
    CHECK(ImportOne(c, 1, png, root, true).ok);
    auto found = FindReplacements(root);
    CHECK(ReplacementsOf(t, found).size() == 1);
    CHECK(RemoveReplacements(t, found) == 1);
    CHECK(ReplacementsOf(t, FindReplacements(root)).empty());

    // A failed save keeps the earlier replacement: put a file where the target folder must go.
    CHECK(ImportOne(c, 1, png, root, false).ok);
    const auto before = ReplacementsOf(t, FindReplacements(root));
    CHECK(before.size() == 1);
    const auto blockedDir = ReplacementPath(root, c.Textures()[0], c.Textures()[0].name).parent_path();
    const auto moved = root / "moved.png";
    std::filesystem::rename(before[0], moved);
    std::filesystem::remove_all(blockedDir);
    std::ofstream(blockedDir, std::ios::binary) << "not a folder";
    const auto elsewhere = root / "textures" / "Otro" / (t.name + ".png");
    std::filesystem::create_directories(elsewhere.parent_path());
    std::filesystem::rename(moved, elsewhere);
    CHECK(!ImportOne(c, 1, png, root, false).ok);           // save fails...
    CHECK(std::filesystem::exists(elsewhere));               // ...and the old replacement survives

    // Folder import accepts a captured palette variant name and keeps that name.
    ParsedName variant = t.key;
    variant.paletteHash ^= 0x5;
    const auto in = temp / "variants";
    std::filesystem::create_directories(in);
    CHECK(SavePng(in / PathFromUtf8(ReplacementName(variant) + ".png"), img, nullptr));
    std::filesystem::remove(blockedDir);
    Progress progress;
    const auto report = ImportFolder(c, in, root, &progress);
    CHECK(report.size() == 1 && report[0].ok);
    CHECK(FindReplacements(root).count(ReplacementName(variant)) == 1);
    CHECK(progress.total == 1 && progress.done == 1);

    // Export reports progress too.
    Progress exported;
    CHECK(Export(c, {0, 1, 2}, temp / "rev-export", nullptr, &exported) == 3 && exported.done == 3 && exported.total == 3);
}
// Polish before calling the tool finished.
void TestPolish(const std::filesystem::path& temp) {
    std::string error;
    for (const char* bad : {"COM7", "lpt9", "con.txt", "Nul.png", "AUX.mod"}) CHECK(!ValidModName(bad, &error));
    CHECK(ValidModName("Console", &error) && ValidModName("COM10", &error));

    // Config.toml values: basic strings with escapes and " #" inside quotes, trailing comments.
    const auto config = temp / "Polish.toml";
    std::ofstream(config, std::ios::binary) << R"([paths]
dvd_root = "D:\\Juegos #1\\Data" # comentario
other = 'C:\Lit #2' # otro
[mods]
enabled = ["A \"B\"", 'C']
)";
    CHECK(ReadSetting(config, "paths", "dvd_root") == std::optional<std::string>(R"(D:\Juegos #1\Data)"));
    CHECK(ReadSetting(config, "paths", "other") == std::optional<std::string>(R"(C:\Lit #2)"));
    CHECK((ReadEnabledMods(config) == std::vector<std::string>{"A \"B\"", "C"}));
    CHECK(SetModEnabled(config, "D", true));
    CHECK((ReadEnabledMods(config) == std::vector<std::string>{"A \"B\"", "C", "D"}));
    CHECK(!std::filesystem::exists(PathFromUtf8(Utf8(config) + ".tmp")));

    // Category folders are cleaned the same way for exports and mods.
    CatalogTexture odd{"tex1_8x8_00000000000000aa_5", {}, "Me:nús", "a/b"};
    const auto rel = CategoryPath(odd).generic_u8string();
    CHECK(std::string(rel.begin(), rel.end()) == "Me_nús/a_b");

    // Thumbnails of big replacements are scaled down (averaging), small ones stay as they are.
    Image big{512, 256, std::vector<uint8_t>(512 * 256 * 4, 0)};
    for (size_t i = 0; i < big.rgba.size(); i += 8) big.rgba[i] = 200;  // every other pixel red 200
    const Image small = Downscale(big, 256);
    CHECK(small.width == 256 && small.height == 128 && small.rgba[0] == 100);
    CHECK(Downscale(small, 256).width == 256);

    // Export says how many textures come out grey (palette in another pack).
    Catalog c;
    SyntheticCatalog(temp, c);
    size_t grey = 0;
    CHECK(Export(c, {0, 1, 2}, temp / "grey-export", nullptr, nullptr, &grey) == 3 && grey == 1);

    // Duplicates in two subfolders: the message names the file that was used.
    const auto in = temp / "dups";
    std::filesystem::create_directories(in / "a");
    std::filesystem::create_directories(in / "b");
    const std::string name = c.Textures()[1].name;
    for (const char* sub : {"a", "b"}) std::ofstream(in / sub / PathFromUtf8(name + ".dds"), std::ios::binary) << "x";
    const auto report = ImportFolder(c, in, temp / "mods" / "Dups");
    bool named = false;
    for (const auto& r : report) named |= r.message.find("Repetido") != std::string::npos && r.message.find(".dds") != std::string::npos;
    CHECK(named);
}
// Spanish / English: Tr, the shown names of the (Spanish) category keys, and the [ui] language setting.
void TestLanguage(const std::filesystem::path& temp) {
    using power_pros_ui::Lang;
    power_pros_ui::g_lang = Lang::En;
    CHECK(std::strcmp(Tr("Hola", "Hello"), "Hello") == 0);
    for (const auto& group : GroupOrder()) CHECK(GroupLabelEn(group) != nullptr);
    CHECK(GroupLabel("Todo") == "All" && GroupLabel("Estadios") == "Stadiums" && GroupLabel("Solo vistas en juego") == "Seen in game only");
    CHECK(GroupLabel("teamlogo_aaa01") == "teamlogo_aaa01");                    // not a key: shown as it is
    // Every Spanish subgroup Categorize makes has an English name; team and pack names stay.
    for (const char* pack : {"stadium_sky_rainy.pack", "stadium_common.pack", "stadium_season.pack", "stadium_hanabi.pack",
                             "stadium_debug.pack", "env_playercard_x.pack", "env_mainmenu.pack", "env_title_logo.pack",
                             "env_common.pack", "env_credit.pack", "env_vs.pack", "env_stadium_menu.pack"}) {
        const Category c = Categorize(pack);
        CHECK(SubgroupLabel(c.subgroup) != c.subgroup);
    }
    CHECK(SubgroupLabel("Menú principal") == "Main menu" && SubgroupLabel("Sesión 12") == "Session 12");
    CHECK(SubgroupLabel("Diamondbacks (ARI) AAA") == "Diamondbacks (ARI) AAA" && SubgroupLabel("shop") == "shop");
    CHECK(Categorize("env_mainmenu.pack").group == "Menús");                    // keys never change with the language
    std::string error;
    CHECK(!ValidModName("", &error) && error == "Type a name for the mod.");
    CHECK(std::strcmp(SizeCheckMessage(SizeCheck::Ok), "Correct size.") == 0);

    CHECK(power_pros_ui::LangFromSetting("en") == Lang::En && power_pros_ui::LangFromSetting("es") == Lang::Es);
    CHECK(power_pros_ui::LangFromSetting("auto") == power_pros_ui::SystemLang());
    // Saving the language: line by line, every other line kept, [ui] added once at the end.
    const auto config = temp / "Lang.toml";
    std::ofstream(config, std::ios::binary) << "# top\r\n[paths]\r\ndvd_root = 'D:\\x' # keep\r\n";
    CHECK(WriteSetting(config, "ui", "language", "\"en\""));
    CHECK(ReadSetting(config, "ui", "language") == std::optional<std::string>("en"));
    CHECK(WriteSetting(config, "ui", "language", "\"es\""));
    CHECK(ReadSetting(config, "ui", "language") == std::optional<std::string>("es"));
    std::ifstream in(config, std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(in)), {});
    CHECK(text == "# top\r\n[paths]\r\ndvd_root = 'D:\\x' # keep\r\n\r\n[ui]\r\nlanguage = \"es\"\r\n");

    power_pros_ui::g_lang = Lang::Es;
    CHECK(std::strcmp(Tr("Hola", "Hello"), "Hola") == 0);
    CHECK(GroupLabel("Estadios") == "Estadios" && SubgroupLabel("Sesión 12") == "Sesión 12");
    CHECK(!ValidModName("", &error) && error == "Escribe un nombre para el mod.");
}
void TestDolphinPack(const std::filesystem::path& temp) {
    const std::string base = "tex1_64x32_00000000000000aa_00000000000000bb_9";
    CHECK(DolphinBaseName(base) == base);
    CHECK(DolphinBaseName(base + "_mip2") == base);
    CHECK(DolphinBaseName(base + "_arb") == base && DolphinBaseName(base + "_arb_mip1") == base);
    CHECK(DolphinBaseName("tex1_64x32_m_00000000000000aa_00000000000000bb_9") == base);
    CHECK(DolphinBaseName("tex1_64x32_00000000000000aa_$_9") == "tex1_64x32_00000000000000aa_$_9");
    CHECK(DolphinBaseName("readme").empty() && DolphinBaseName("tex1_bad").empty());
    // A pack copies tex1_ files as they are, subfolders kept; other files are reported, not copied.
    const auto pack = temp / "dolphin-pack" / "RL8E54", mod = temp / "dolphin-mod";
    std::filesystem::create_directories(pack / "menus");
    for (const auto& name : {base + ".png", base + "_mip1.png", std::string("notes.png")}) std::ofstream(pack / "menus" / name) << "x";
    Catalog empty;
    const auto report = ImportDolphinPack(empty, pack.parent_path(), mod, "MyPack");
    int copied = 0;
    for (const auto& r : report) copied += r.ok;
    CHECK(report.size() == 3 && copied == 2);
    CHECK(std::filesystem::exists(mod / "textures" / "MyPack" / "RL8E54" / "menus" / (base + "_mip1.png")));
    CHECK(!std::filesystem::exists(mod / "textures" / "MyPack" / "RL8E54" / "menus" / "notes.png"));
}
} // namespace

int RunSelftest() {
    const auto temp = std::filesystem::temp_directory_path() / PathFromUtf8("texture-studio-selftest-ñ");
    std::filesystem::remove_all(temp);
    std::filesystem::create_directories(temp);
    const power_pros_ui::Lang lang = power_pros_ui::g_lang;
    power_pros_ui::g_lang = power_pros_ui::Lang::Es;  // the checks below look for words of the Spanish messages
    TestGx();
    TestPack();
    TestProjectAndNames(temp);
    TestImages(temp);
    TestCategories();
    TestCatalog(temp);
    TestConfigAndMods(temp);
    TestTransfer(temp);
    TestCapture(temp);
    TestReviewFixes(temp);
    TestPolish(temp);
    TestLanguage(temp);
    TestDolphinPack(temp);
    power_pros_ui::g_lang = lang;
    std::printf("selftest %s (%d checks)\n", g_failures ? "FAILED" : "ok", g_checks);
    return g_failures ? 1 : 0;
}

} // namespace tstudio
