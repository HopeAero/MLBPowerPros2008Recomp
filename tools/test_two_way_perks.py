# SPDX-License-Identifier: GPL-3.0-or-later
"""Run the creation panel/growth hooks against synthetic memory and original-body stubs."""
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
patch = (ROOT / "tools/patches/wiicompiled-two-way-player.patch").read_text()
source = "\n".join(line[1:] for line in patch.splitlines() if line.startswith("+"))
start = source.index("// Original panel bodies:")
end = source.index("// The drill list's help bar", start)
hooks = source[start:end]
harness = r'''
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <map>
struct CpuContext { uint32_t gpr[32]{}; uint32_t lr = 0; };
#define REGISTER_NATIVE_FUNCTION_AS(...)
namespace Memory {
std::map<uint32_t, uint8_t> bytes;
uint8_t Read8(uint32_t a) { return bytes[a]; }
void Write8(uint32_t a, uint8_t v) { bytes[a] = v; }
void Write16(uint32_t a, uint16_t v) { Write8(a, v>>8); Write8(a+1, v); }
uint32_t Read32(uint32_t a) { return uint32_t(Read8(a))<<24 | uint32_t(Read8(a+1))<<16 | uint32_t(Read8(a+2))<<8 | Read8(a+3); }
void Write32(uint32_t a, uint32_t v) { for(int i=3;i>=0;--i) {Write8(a+i, v); v>>=8;} }
bool Contains(uint32_t a, uint32_t) { return a != 0; }
}
namespace RuntimeConfigFile { bool enabled = true; bool TwoWayPlayer(bool) { return enabled; } }
constexpr uint32_t header = 0x81200000, record = 0x81210000, panel = 0x81220000;
int action = 0, growthCalls = 0;
bool originalWasBatter = false;
namespace two_way {
constexpr uint32_t kNewPlayer = 0x81048B00, kPitcher = 1;
uint32_t g_second = 9;
bool g_creationBatter = false;
std::array<uint8_t,3> g_pitchPerks{}, g_batPerks{};
bool g_perksReady[2]{}, g_perkBatter[2]{};
uint32_t LifeRecord(CpuContext*) { return record; }
template<uint32_t address> void InvokeDirectCpu(CpuContext*) {
    assert(g_creationBatter);
    assert(Memory::Read8(header+154)==g_batPerks[0]);
    if constexpr (address == 0x80A219F4) {
        for (int i=0;i<21;++i) Memory::Write8(header+4532+i, 100+i);
    }
    if constexpr (address == 0x80A233E8) {
        Memory::Write32(record+0x78, 0xDEADBEEF);
        for(int i=0;i<3;++i) Memory::Write32(record+0x5C+4*i, 0);
        Memory::Write32(record+0x6C, 0x12345678);
    }
}
void Call(CpuContext* ctx, uint32_t, void (*f)(CpuContext*)) { f(ctx); }
}
void init(CpuContext* ctx) {
    originalWasBatter = two_way::g_creationBatter;
    for(int i=292;i<=294;++i) Memory::Write8(ctx->gpr[3]+i,0);
    Memory::Write32(ctx->gpr[3]+296,2);
}
void update(CpuContext* ctx) {
    originalWasBatter = two_way::g_creationBatter;
    if (two_way::g_perksReady[0]) {
        const auto& strengths = originalWasBatter ? two_way::g_batPerks : two_way::g_pitchPerks;
        assert(Memory::Read8(header+154)==strengths[0]);
        assert(Memory::Read8(header+155)==strengths[1]);
    }
    if(action) {
        Memory::Write32(ctx->gpr[3]+296,3);
        Memory::Write8(ctx->gpr[3]+(action==1?292:293),1);
    }
}
extern "C" void func_80977244(CpuContext* c) { init(c); }
extern "C" void func_809772CC(CpuContext* c) { update(c); }
extern "C" void func_80A53244(CpuContext* c) { init(c); }
extern "C" void func_80A532A8(CpuContext* c) { update(c); }
extern "C" void func_80A21160(CpuContext*) { ++growthCalls; }
'''
cases = r'''
int main() {
    using namespace two_way;
    Memory::Write32(0x8104B5C0,header);
    Memory::Write8(0x81048D84,0); // The live creator leaves this flag clear.
    Memory::Write32(kNewPlayer+0x5C,1<<6);
    CpuContext c; c.gpr[3]=panel; c.lr=77;
    PerkPanel(&c,false,true);
    Memory::Write8(header+154,25); Memory::Write8(header+155,28);
    action=1; PerkPanel(&c,false,false);
    assert(g_pitchPerks[0]==25 && g_pitchPerks[1]==28);
    assert(g_perkBatter[0] && originalWasBatter && !g_creationBatter);
    assert(Memory::Read32(panel+2086)==0xFFFFFFFF);
    assert(Memory::Read32(panel+296)==2 && !g_perksReady[0]);
    // Back from the second role returns to the first role, retaining its choices.
    action=2; PerkPanel(&c,false,false);
    assert(!g_perkBatter[0] && Memory::Read8(header+154)==25);
    action=1; PerkPanel(&c,false,false);
    Memory::Write8(header+154,3); Memory::Write8(header+155,6);
    PerkPanel(&c,false,false);
    assert(g_perksReady[0] && g_batPerks[0]==3 && g_batPerks[1]==6);
    assert(Memory::Read8(header+154)==25 && Memory::Read8(header+155)==28);
    PerkPanel(&c,true,true);
    Memory::Write8(header+156,12); PerkPanel(&c,true,false);
    assert(g_perkBatter[1] && !g_perksReady[1]);
    assert(Memory::Read32(panel+2082)==0xFFFFFFFF);
    assert(Memory::Read8(header+154)==25 && Memory::Read8(header+155)==28);
    Memory::Write8(header+156,18); PerkPanel(&c,true,false);
    assert(g_perksReady[1] && g_pitchPerks[2]==12 && g_batPerks[2]==18);
    assert(Memory::Read8(header+156)==12);
    for(int i=0;i<21;++i) Memory::Write8(header+4532+i,i);
    Memory::Write32(record+0x78,0xAABBCCDD);
    for(int i=0;i<3;++i) Memory::Write32(record+0x5C+4*i,0x87654321+i);
    MLB_CreationGrowth_80A21160(&c);
    assert(growthCalls==1 && c.gpr[3]==panel && c.lr==77 && !g_creationBatter);
    assert(Memory::Read32(record+0x78)==0xAABBCCDD);
    assert(Memory::Read32(record+0x6C)==0x12345678);
    for(int i=0;i<3;++i) assert(Memory::Read32(record+0x5C+4*i)==0x87654321u+i);
    for(int i=0;i<21;++i) assert(Memory::Read8(header+4532+i)==((i<7 || (i>=10 && i<17))?100+i:i));
    assert(Memory::Read8(header+154)==25 && Memory::Read8(header+156)==12);
    RuntimeConfigFile::enabled=false;
    originalWasBatter=true; action=0; PerkPanel(&c,false,true);
    assert(!originalWasBatter && !g_perkBatter[0]);
    MLB_CreationGrowth_80A21160(&c); assert(growthCalls==2);
}
'''
compiler = shutil.which("clang++") or str(
    ROOT / "local/tools/llvm-mingw-20260616-ucrt-x86_64/bin/clang++.exe")
with tempfile.TemporaryDirectory(prefix="two-way-perks-") as tmp:
    cpp, exe = Path(tmp) / "test.cpp", Path(tmp) / "test.exe"
    cpp.write_text(harness + hooks + cases)
    subprocess.run([compiler, "-std=c++17", "-static", str(cpp), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print("PASS: both perk rounds, back, separate choices and preservation of pitching stats/limits")
