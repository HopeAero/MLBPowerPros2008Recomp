# SPDX-License-Identifier: MIT
"""Host check of the real TEV rewrite and alpha-byte conversion (no game data).

python tools/switch/deko/test_tev_alpha.py <host clang++>
"""
import ast
import json
from pathlib import Path
import re
import subprocess
import sys


root = Path(__file__).resolve().parents[3]
source = (root / "tools/switch/deko/core/shader_translate.cpp").read_text(encoding="utf-8")
start = source.index("uint32_t tev_fast_rewrite(")
part = source[start:source.index("// ---- tev_unit_rewrite (begin", start)]


def strings(text):
    return [ast.literal_eval(s) for s in re.findall(r'"(?:[^"\\]|\\.)*"', text)]


fixture = ""
for name in ("kByteF32", "kByteVec3", "kRegularF32"):
    match = re.search(r"static const char " + name + r"\[\]\s*=\s*((?:\"(?:[^\"\\]|\\.)*\"\s*)+);", part)
    assert match, name
    fixture += "".join(strings(match[1]))
fixture += "".join(strings(re.search(r"kRegularVec3\[3\] = \{(.*?)\};", part, re.S)[1]))
alpha = "u32(round(clamp(prev.a, 0.0, 1.0) * 255.0))"
replacement = "u32(tev_byte_f32(clamp(prev.a, 0.0, 1.0)))"
test = r'''
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
''' + part + r'''
static float as_float(uint32_t bits) {
    float value;
    memcpy(&value, &bits, 4);
    return value;
}
static uint32_t fast_alpha(float value) {
    float scaled = std::clamp(value, 0.f, 1.f) * 255.f;
    float rounded = std::max(scaled, -4194304.f) + 12582912.f;
    uint32_t bits;
    memcpy(&bits, &rounded, 4);
    return bits - 0x4B400000u;
}
int main() {
    const std::string fixture = FIXTURE;
    const std::string alpha = ALPHA, replacement = REPLACEMENT;
    std::string module = fixture + "\nlet alphaCompare = " + alpha + ";\n";
    assert(tev_fast_rewrite(&module) == 7);
    assert(module.find(replacement) != std::string::npos);
    assert(module.find(alpha) == std::string::npos);
    std::string once = module;
    assert(tev_fast_rewrite(&module) == 0 && module == once);
    module = fixture;
    assert(tev_fast_rewrite(&module) == 6);
    assert(module.find(replacement) == std::string::npos);
    module = alpha;
    assert(tev_fast_rewrite(&module) == 0 && module == alpha);
    for (uint32_t bits = 0; bits <= 0x3F800000u; ++bits) {
        float value = as_float(bits);
        uint32_t expected = uint32_t(std::nearbyint(value * 255.f));
        if (fast_alpha(value) != expected) {
            std::printf("alpha mismatch at %08x\n", bits);
            return 1;
        }
    }
    for (float value : {-INFINITY, -10.f, -0.f, 0.f, 1.f, 10.f, INFINITY})
        assert(fast_alpha(value) == uint32_t(std::nearbyint(std::clamp(value, 0.f, 1.f) * 255.f)));
    std::puts("PASS: rewrite/fallback/idempotence; 1,065,353,217 alpha floats and clamp edges");
}
'''
for key, value in (("FIXTURE", fixture), ("ALPHA", alpha), ("REPLACEMENT", replacement)):
    test = test.replace(key, json.dumps(value))
out = root / "local/tmp/cpu/P31-test"
out.mkdir(parents=True, exist_ok=True)
cpp, exe = out / "alpha.cpp", out / "alpha.exe"
cpp.write_text(test, encoding="utf-8")
subprocess.run([sys.argv[1], "-std=c++17", "-O2", "-static", "-ffp-contract=off", str(cpp), "-o", str(exe)], check=True)
subprocess.run([str(exe)], check=True)
header = (root / "tools/switch/deko/core/shader_translate.h").read_text(encoding="utf-8")
seed = int(re.search(r"kTevFastSeed = (0x[0-9a-fA-F]+)ull", header)[1], 16)
assert seed != 0x7465765F66617374, "new shader source must not reuse the previous cached modules"
