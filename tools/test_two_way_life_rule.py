# SPDX-License-Identifier: GPL-3.0-or-later
"""Who may be two-way in MLB Life (the manager's role talk, the lineups): the patched rules with
synthetic memory; no game data required. A field aptitude of 3 or some bat (contact 4 or power 60)
is enough; an ordinary pitcher (aptitudes under 3, contact 1, power 16: all G) is not."""
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
patch = (ROOT / "tools/patches/wiicompiled-two-way-player.patch").read_text()
source = "\n".join(line[1:] for line in patch.splitlines() if line.startswith("+"))


def cut(first, last):
    start = source.index(first)
    return source[start:source.index(last, start)]


rule = cut("constexpr uint32_t kLifeAptitude", "// The role the MLB Life player chose")
life = cut("int LifeTwoWay(uint32_t players, uint8_t index) {", "\n// The manager talk")
harness = r'''
#include <cstdint>
#include <cstdio>
struct CpuContext { uint32_t gpr[32]{}; uint32_t lr = 0; };
namespace two_way {
uint8_t mem[0x1000];                      // 0x81000000.. : players at +0, records at +0x400 (packed) / +0x800
uint8_t aptitude[10];                     // the packed record's aptitudes, as 0x80A5B160 reads them
bool enabled = true;
constexpr uint8_t kPitcher = 1;
namespace Memory {
uint8_t Read8(uint32_t a) { return mem[a - 0x81000000u]; }
uint16_t Read16(uint32_t a) { return mem[a - 0x81000000u] << 8 | mem[a - 0x81000000u + 1]; }
uint32_t Read32(uint32_t a) { return uint32_t(Read16(a)) << 16 | Read16(a + 2); }
bool Contains(uint32_t a, uint32_t n) { return a >= 0x81000000u && a + n <= 0x81001000u; }
}
template<uint32_t address> void InvokeDirectCpu(CpuContext* ctx) {
    if constexpr (address == 0x80A5B160) ctx->gpr[3] = aptitude[ctx->gpr[4]];
    if constexpr (address == 0x80862314) ctx->gpr[3] = ctx->gpr[3] == 9001 ? 0x81000800u : 0;
}
void Call(CpuContext* ctx, uint32_t, void (*f)(CpuContext*)) { f(ctx); }
bool LifeMode() { return enabled; }
uint16_t LifeId() { return 9001; }
uint32_t LifeRecord(CpuContext*) { return 0x81000400u; }   // packed: word +0x5C holds the main position
'''
cases = r'''
}
int main() {
    using namespace two_way;
    CpuContext ctx;
    int failures = 0;
    auto expect = [&](const char* what, uint32_t got, uint32_t want) {
        if (got != want) std::fprintf(stderr, "FAIL %s: got %u, want %u\n", what, got, want), ++failures;
    };
    auto player = [&](uint8_t contact, uint8_t power, uint8_t fieldAptitude) {
        for (auto& a : aptitude) a = 1;
        aptitude[1] = 7;                                  // P, his main position
        mem[0x800 + 0x72] = contact, mem[0x800 + 0x73] = power;
        mem[0x800 + 0x62] = 1, mem[0x800 + 0x63] = 7;    // unpacked: main position P, P aptitude 7
        for (int p = 1; p < 9; ++p) mem[0x800 + 0x64 + p - 1] = 1;
        mem[0] = 9001 >> 8, mem[1] = 9001 & 0xFF;         // players[0]: the MLB Life player, record +0xC
        mem[0xC] = 0x81, mem[0xD] = 0x00, mem[0xE] = 0x08, mem[0xF] = 0x00;
        if (fieldAptitude) aptitude[9] = fieldAptitude, mem[0x800 + 0x64 + 7] = fieldAptitude;   // RF
    };
    player(1, 16, 0);
    expect("all G pitcher: no batting position", SecondPosition(&ctx, 0x81000400u), 0);
    expect("all G pitcher: not two-way in lineups", LifeTwoWay(0x81000000u, 0), 0);
    player(1, 16, 3);
    expect("RF aptitude 3: bats at RF", SecondPosition(&ctx, 0x81000400u), 9);
    expect("RF aptitude 3: two-way in lineups", LifeTwoWay(0x81000000u, 0), 1);
    player(1, 16, 2);
    expect("RF aptitude 2: not enough", SecondPosition(&ctx, 0x81000400u), 0);
    player(4, 16, 0);
    expect("contact 4, no position: bats (DH)", SecondPosition(&ctx, 0x81000400u) != 0, 1);
    expect("contact 4: two-way in lineups", LifeTwoWay(0x81000000u, 0), 1);
    player(1, 60, 0);
    expect("power 60: bats", SecondPosition(&ctx, 0x81000400u) != 0, 1);
    player(3, 59, 0);
    expect("contact 3, power 59: not yet", SecondPosition(&ctx, 0x81000400u), 0);
    auto mainPosition = [&](uint32_t p) {               // packed +0x5C bits 6..9, unpacked +0x62
        const uint32_t w = p << 6;
        mem[0x400 + 0x5C] = w >> 24, mem[0x400 + 0x5D] = w >> 16, mem[0x400 + 0x5E] = w >> 8, mem[0x400 + 0x5F] = w;
        mem[0x800 + 0x62] = p;
    };
    mainPosition(1);
    player(1, 16, 3);
    expect("pitcher, RF 3: two-way", LifeAbility(&ctx).TwoWay(), 1);
    expect("pitcher, RF 3: bats at RF", LifeAbility(&ctx).bat, 9);
    player(1, 16, 0);
    expect("all G pitcher: not two-way", LifeAbility(&ctx).TwoWay(), 0);
    player(6, 120, 0);
    mainPosition(9);                                      // a hitter (RF)
    aptitude[1] = 0, mem[0x800 + 0x63] = 0;
    expect("RF hitter without P aptitude: not two-way", LifeAbility(&ctx).TwoWay(), 0);
    expect("RF hitter without P aptitude: not in lineups", LifeTwoWay(0x81000000u, 0), 0);
    aptitude[1] = 3, mem[0x800 + 0x63] = 3;               // Success gave him P as a second position
    expect("RF hitter, P 3: two-way", LifeAbility(&ctx).TwoWay(), 1);
    expect("RF hitter, P 3: bats at his position", LifeAbility(&ctx).bat, 9);
    expect("RF hitter, P 3: two-way in lineups", LifeTwoWay(0x81000000u, 0), 1);
    aptitude[1] = 2, mem[0x800 + 0x63] = 2;
    expect("RF hitter, P 2: not enough", LifeTwoWay(0x81000000u, 0), 0);
    mainPosition(1);
    mem[1] = 1;                                           // another player: the roster rule decides
    expect("not the MLB Life player", static_cast<uint32_t>(LifeTwoWay(0x81000000u, 0) + 1), 0);
    enabled = false;
    player(9, 200, 5);
    expect("rule off", static_cast<uint32_t>(LifeTwoWay(0x81000000u, 0) + 1), 0);
    std::puts(failures ? "FAIL" : "PASS: MLB Life two-way rule (pitchers: aptitude 3, or contact 4 / power 60; hitters: P aptitude 3)");
    return failures != 0;
}
'''
compiler = shutil.which("clang++") or shutil.which("g++") or str(ROOT / "local/tools/llvm-mingw-20260616-ucrt-x86_64/bin/clang++.exe")
with tempfile.TemporaryDirectory() as tmp:
    cpp, exe = Path(tmp) / "t.cpp", Path(tmp) / "t.exe"
    cpp.write_text(harness + rule + "\n" + life + cases)
    subprocess.run([compiler, "-std=c++17", str(cpp), "-o", str(exe)], check=True)
    raise SystemExit(subprocess.run([str(exe)]).returncode)
