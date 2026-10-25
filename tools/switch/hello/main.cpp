// SPDX-License-Identifier: GPL-3.0-or-later
// First console test for the MLB Power Pros 2008 Switch build: proves one of our NROs launches on the
// user's firmware and records what the game will have to work with (firmware, applet or full memory,
// memory size) to sdmc:/switch/mlbpowerpros2008/logs/hello.log. Every line is flushed as written.
#include <switch.h>
#include <arpa/inet.h>  // struct in_addr, for __nxlink_host

#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <sys/stat.h>

namespace {

FILE* g_log = nullptr;

void Line(const char* format, ...) {
    char text[256];
    va_list args;
    va_start(args, format);
    std::vsnprintf(text, sizeof text, format, args);
    va_end(args);
    std::printf("%s\n", text);
    if (g_log) {
        std::fprintf(g_log, "%s\n", text);
        std::fflush(g_log);
    }
}

const char* AppletTypeName(AppletType type) {
    switch (type) {
    case AppletType_Application: return "application (full memory)";
    case AppletType_SystemApplication: return "system application";
    case AppletType_LibraryApplet: return "library applet (album: limited memory)";
    case AppletType_OverlayApplet: return "overlay applet";
    case AppletType_SystemApplet: return "system applet";
    default: return "unknown";
    }
}

} // namespace

int main() {
    consoleInit(nullptr);
    // Started by nxlink (tools/switch/nxlink.sh): stdout also goes back to the PC over Wi-Fi.
    if (__nxlink_host.s_addr != 0 && R_SUCCEEDED(socketInitializeDefault())) {
        nxlinkStdio();
    }
    mkdir("sdmc:/switch", 0777);
    mkdir("sdmc:/switch/mlbpowerpros2008", 0777);
    mkdir("sdmc:/switch/mlbpowerpros2008/logs", 0777);
    g_log = std::fopen("sdmc:/switch/mlbpowerpros2008/logs/hello.log", "a");

    const std::time_t now = std::time(nullptr);
    Line("MLB Power Pros 2008 - Switch test 1 (hello)");
    Line("time: %s", std::asctime(std::gmtime(&now)));

    if (R_SUCCEEDED(setsysInitialize())) {
        SetSysFirmwareVersion firmware{};
        if (R_SUCCEEDED(setsysGetFirmwareVersion(&firmware))) {
            Line("firmware: %u.%u.%u (%s)", firmware.major, firmware.minor, firmware.micro, firmware.display_version);
        }
        setsysExit();
    }
    Line("atmosphere: %s", hosversionIsAtmosphere() ? "yes" : "no");

    const AppletType type = appletGetAppletType();
    Line("mode: %s", AppletTypeName(type));

    u64 total = 0, used = 0;
    svcGetInfo(&total, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0);
    svcGetInfo(&used, InfoType_UsedMemorySize, CUR_PROCESS_HANDLE, 0);
    Line("memory: total %llu MiB, used %llu MiB, free %llu MiB", (unsigned long long)(total >> 20),
         (unsigned long long)(used >> 20), (unsigned long long)((total - used) >> 20));
    Line("log: sdmc:/switch/mlbpowerpros2008/logs/hello.log");
    Line("");
    Line("Press + to exit.");

    PadState pad;
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&pad);
    while (appletMainLoop()) {
        padUpdate(&pad);
        if (padGetButtonsDown(&pad) & HidNpadButton_Plus) break;
        consoleUpdate(nullptr);
    }
    if (g_log) std::fclose(g_log);
    if (__nxlink_host.s_addr != 0) socketExit();
    consoleExit(nullptr);
    return 0;
}
