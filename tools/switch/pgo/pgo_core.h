// PGO profile plumbing shared by the game hook (mlb_pgo_gen.cpp) and the standalone validation builds.
// Plain C++/POSIX, no libnx, no TLS. Everything is extern "C" and safe to call from any thread.
#pragma once
#include <stdint.h>

extern "C" {

// libgcov (GCC 16) writes each translation unit's counters to the file name baked in at compile time:
//   <-fprofile-generate=DIR>\<mangled object path relative to -fprofile-prefix-path>.gcda
// ('\' is the host separator of the Windows-hosted devkitA64). Horizon cannot open such a path, so this
// rewrites every registered name to "<dir>/<mangled name>.gcda" (what follows the last '\' or '/').
// Call once, after the SD card is mounted and before the first dump; creates <dir>, and empties it when it
// holds the profile of another build (MlbPgoNewBuildWipe).
// Returns the number of names rewritten, or -1 when libgcov's gcov_info does not look as expected (then
// nothing is touched; the dump would fail to open its files and the caller must say so).
int MlbPgoSetProfileDir(const char* dir);

// Used by MlbPgoSetProfileDir: deletes the *.gcda in dir unless build-id.txt there already holds id (the
// profile of an earlier instrumented build must not be merged into this one). Returns files removed.
int MlbPgoNewBuildWipe(const char* dir, uint64_t id);

// __gcov_dump() then __gcov_reset(): merges the counters into the files and zeroes them, so a later dump
// (periodic, combo, exit) adds only what happened since. Serialized; returns the milliseconds-free count
// of dumps done so far.
int MlbPgoDumpAndReset(void);

// Sends every *.gcda under dir to ipv4 (network byte order):port, zlib-compressed, framed as
// pgo_recv.py expects. Returns the files sent, or -1 when the connection failed.
int MlbPgoStream(uint32_t ipv4_be, uint16_t port, const char* dir);

// "a.b.c.d" -> network-order address, 0 on error (no libc resolver needed).
uint32_t MlbPgoParseIpv4(const char* text);

}  // extern "C"
