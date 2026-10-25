// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
#include <thread>
#include <zlib.h>

int main() {
#if !defined(__aarch64__)
    return 1;
#endif
    int value = 0;
    std::thread([&] { value = 1; }).join();
    if (value != 1 || zlibVersion()[0] != '1') return 2;
    std::puts("aarch64 ok");
    return 0;
}
