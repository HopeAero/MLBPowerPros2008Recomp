# SPDX-License-Identifier: GPL-3.0-or-later
"""Exercise the patched role hook with synthetic memory; no game data required."""
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
patch = (ROOT / "tools/patches/wiicompiled-two-way-player.patch").read_text()
source = "\n".join(line[1:] for line in patch.splitlines() if line.startswith("+"))
start = source.index('extern "C" void MLB_LifeIsPitcher_80A2F88C(')
hook = source[start:source.index("\nPPC_NATIVE_OVERRIDE_VOID", start)]
harness = r'''
#include <cstdint>
#include <cstdio>
struct CpuContext { uint32_t gpr[32]{}; uint32_t lr = 0; };
namespace two_way {
bool creating, enabled = true, batter = true, g_creationBatter = false;
uint32_t position = 1, second = 9;
constexpr uint32_t kDrillListAsk = 0x80A37330;
bool g_inPractice, g_drillOpen, g_practiceBatter;
uint32_t g_drillList;
namespace Memory { uint8_t Read8(uint32_t) { return creating; } }
template<uint32_t address> void InvokeDirectCpu(CpuContext* ctx) {
    if constexpr (address == 0x80A2F738) ctx->gpr[3] = 0x81234560;
    if constexpr (address == 0x8057BD3C) ctx->gpr[3] = position;
    if constexpr (address == 0x80A16F38) ctx->gpr[3] = 100;
    if constexpr (address == 0x80B06888) ctx->gpr[3] = position == 1;
}
void Call(CpuContext* ctx, uint32_t, void (*f)(CpuContext*)) { f(ctx); }
bool LifeMode() { return enabled; }
uint32_t LifeRecord(CpuContext*) { return 0x81234560; }
uint32_t SecondPosition(CpuContext*, uint32_t) { return second; }
bool Batter() { return batter; }
}
'''
cases = r'''
int main() {
    using namespace two_way;
    CpuContext ctx;
    auto check = [&](bool make, uint32_t pos, uint32_t aptitude, bool role, bool rule, uint32_t want) {
        creating = make; position = pos; second = aptitude; batter = role; enabled = rule;
        MLB_LifeIsPitcher_80A2F88C(&ctx);
        if (ctx.gpr[3] != want) {
            std::fprintf(stderr, "creation=%d position=%u second=%u batter=%d rule=%d: got %u expected %u\n",
                         make, pos, aptitude, role, rule, ctx.gpr[3], want);
            return false;
        }
        return true;
    };
    if (!check(true, 1, 9, true, true, 1)) return 1;
    g_creationBatter = true;
    if (!check(true, 1, 9, true, true, 0)) return 1;
    g_creationBatter = false;
    if (!check(true, 9, 0, true, true, 0)) return 1;
    if (!check(true, 1, 0, true, true, 1)) return 1;
    if (!check(true, 1, 9, true, false, 1)) return 1;
    if (!check(false, 1, 9, true, true, 0)) return 1;
    if (!check(false, 1, 9, false, true, 1)) return 1;
    if (!check(false, 1, 0, true, true, 1)) return 1;
    ctx.lr = 0x80A210D8;
    if (!check(false, 1, 9, true, true, 1)) return 1;
    g_creationBatter = true;
    if (!check(false, 1, 9, true, true, 0)) return 1;
    g_creationBatter = false;
    ctx.lr = kDrillListAsk; ctx.gpr[31] = 0x81234000;
    if (!check(false, 1, 9, true, true, 0)) return 1;
    if (!g_inPractice || !g_drillOpen || g_drillList != 0x81234000) return 1;
}
'''
compiler = shutil.which("clang++") or str(
    ROOT / "local/tools/llvm-mingw-20260616-ucrt-x86_64/bin/clang++.exe")
with tempfile.TemporaryDirectory(prefix="two-way-creation-") as tmp:
    cpp, exe = Path(tmp) / "test.cpp", Path(tmp) / "test.exe"
    cpp.write_text(harness + hook + cases)
    subprocess.run([compiler, "-std=c++17", str(cpp), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print("PASS: creation keeps the main position; career and drill roles still switch")
