// PGO instrumentation hook for the Switch NRO (switch-arm64). Compiled into the instrumented build only
// (MLB_SWITCH_PGO=gen adds this file and pgo_core.cpp to the WiiCompiled target, pgo-project-include.cmake).
// main.cpp's call site is a weak reference (`if (MlbPgoStart) MlbPgoStart();`), null in the normal and the
// profile-use builds, so RuntimeMain is the same code in all three (a call site that differed between the
// gen and the use build would change its CFG and make GCC discard that function's profile).
//
// What it does:
//  - MlbPgoStart(): points libgcov's baked-in profile file names at sdmc:/switch/mlbpowerpros2008/pgo
//    (pgo_core.cpp) and starts a low-priority worker thread on core 2.
//  - the worker dumps the profile (__gcov_dump + __gcov_reset: merged into the files on the SD card,
//    counters zeroed) every WIICOMPILED_PGO_MINUTES minutes (default 4; 0 = never), when the HOME menu
//    takes the focus, on the exit request, and when L+R+ZL+ZR+Minus are held for one second. The combo
//    (and only the combo, or the exit request when a host is known) also streams the profile files to
//    the PC (pgo_recv.py).
//  - libgcov itself dumps once more at a normal exit (its .fini_array destructor, run by libnx's exit).
// Host for the stream: WIICOMPILED_PGO_HOST=a.b.c.d (argv "WIICOMPILED_PGO_HOST=192.168.1.20" works, see
// main()), else the nxlink host when the game was started with nxlink, else the first line of
// sdmc:/switch/mlbpowerpros2008/pgo/host.txt. Port 28772.
//
// No allocation or logging in any game hot path: everything below runs on the worker or in applet
// callbacks.
#include "pgo_core.h"

#if defined(__SWITCH__) && defined(MLB_PGO_GEN)
#include <arpa/inet.h>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <switch.h>

namespace {
constexpr char kDir[] = "sdmc:/switch/mlbpowerpros2008/pgo";
constexpr u16 kPort = 28772;
constexpr u64 kCombo = HidNpadButton_L | HidNpadButton_R | HidNpadButton_ZL | HidNpadButton_ZR | HidNpadButton_Minus;

std::atomic<bool> g_started{false};
std::atomic<bool> g_stop{false};
std::atomic<bool> g_wantDump{false};
std::atomic<bool> g_wantStream{false};
std::atomic<int> g_dumpsDone{0};
Thread g_thread;
AppletHookCookie g_cookie;
u32 g_hostBe = 0;
u64 g_periodTicks = 0;

u32 FindHost() {
    if (const char* env = std::getenv("WIICOMPILED_PGO_HOST")) {
        if (const u32 ip = MlbPgoParseIpv4(env)) return ip;
    }
    if (__nxlink_host.s_addr != 0) return __nxlink_host.s_addr;
    char path[256];
    std::snprintf(path, sizeof path, "%s/host.txt", kDir);
    if (FILE* f = std::fopen(path, "rb")) {
        char line[64] = {};
        std::fgets(line, sizeof line, f);
        std::fclose(f);
        return MlbPgoParseIpv4(line);
    }
    return 0;
}

void DoDump(bool stream, const char* why) {
    const int n = MlbPgoDumpAndReset();
    std::fprintf(stderr, "[pgo] dump #%d (%s) done\n", n, why);
    g_dumpsDone.store(n, std::memory_order_release);
    if (!stream) return;
    if (g_hostBe == 0) g_hostBe = FindHost();
    if (g_hostBe == 0) {
        std::fprintf(stderr, "[pgo] no PC address (nxlink, WIICOMPILED_PGO_HOST or %s/host.txt): files stay on the SD card\n", kDir);
        return;
    }
    const int files = MlbPgoStream(g_hostBe, kPort, kDir);
    std::fprintf(stderr, "[pgo] stream to the PC: %d files%s\n", files, files < 0 ? " (FAILED: is pgo_recv.py running?)" : "");
}

void Worker(void*) {
    PadState pad;
    padInitializeAny(&pad);
    u64 lastDump = armGetSystemTick();
    int heldPolls = 0;
    bool latched = false;
    while (!g_stop.load(std::memory_order_acquire)) {
        svcSleepThread(50'000'000ull);  // 50 ms
        padUpdate(&pad);
        if ((padGetButtons(&pad) & kCombo) == kCombo) {
            if (++heldPolls >= 20 && !latched) {  // held for 1 s, once per press
                latched = true;
                g_wantStream.store(true, std::memory_order_release);
            }
        } else {
            heldPolls = 0;
            latched = false;
        }
        const bool periodic = g_periodTicks && armGetSystemTick() - lastDump >= g_periodTicks;
        const bool stream = g_wantStream.exchange(false, std::memory_order_acq_rel);
        if (stream || periodic || g_wantDump.exchange(false, std::memory_order_acq_rel)) {
            DoDump(stream, stream ? "combo" : periodic ? "periodic" : "requested");
            lastDump = armGetSystemTick();
        }
    }
}

void AppletCallback(AppletHookType hook, void*) {
    // Runs inside appletMainLoop() on whichever thread polls it (SDL's event pump): only signal.
    if (hook == AppletHookType_OnExitRequest) {
        g_wantStream.store(true, std::memory_order_release);  // streams only when a PC address is known
        const int before = g_dumpsDone.load(std::memory_order_acquire);
        for (int i = 0; i < 400 && g_dumpsDone.load(std::memory_order_acquire) == before; ++i) {
            svcSleepThread(25'000'000ull);  // up to 10 s: the OS kills the process soon after the request
        }
    } else if (hook == AppletHookType_OnFocusState && appletGetFocusState() != AppletFocusState_InFocus) {
        g_wantDump.store(true, std::memory_order_release);
    }
}
}  // namespace

extern "C" void MlbPgoStart() {
    if (g_started.exchange(true)) return;
    const int names = MlbPgoSetProfileDir(kDir);
    if (names < 0) {
        std::fprintf(stderr, "[pgo] libgcov layout is not GCC 16.1's: profile files cannot be redirected, no dumps\n");
        return;
    }
    const char* minutes = std::getenv("WIICOMPILED_PGO_MINUTES");
    const double m = minutes ? std::strtod(minutes, nullptr) : 4.0;
    g_periodTicks = m > 0 ? u64(m * 60.0 * double(armGetSystemTickFreq())) : 0;
    appletHook(&g_cookie, AppletCallback, nullptr);
    if (R_FAILED(threadCreate(&g_thread, Worker, nullptr, nullptr, 0x20000, 0x3A, 2)) || R_FAILED(threadStart(&g_thread))) {
        std::fprintf(stderr, "[pgo] could not start the worker: only the exit dump will happen\n");
        return;
    }
    std::fprintf(stderr, "[pgo] instrumented build: %d profile files -> %s, dump every %.1f min, combo L+R+ZL+ZR+Minus streams to the PC\n",
                 names, kDir, m);
}
#endif
