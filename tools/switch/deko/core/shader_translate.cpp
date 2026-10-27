// SPDX-License-Identifier: MIT
// From SwitchWaker (switch/deko/shader_translate.cpp at 56c0758; LICENSE.SwitchWaker). Modified for MLB
// Power Pros 2008 Recomp (2026-10-27): tev_fast_rewrite (the GX TEV helpers' byte conversions without
// the conversion unit, bit for bit; shader_translate.h). Needs Tint's GLSL writer with dawn-switch-tint-{depth-zero-to-one,position-y-up}.patch
// (workstream A's Dawn build: Options::depth_zero_to_one, Options::disable_position_y_negation).
// WGSL -> uam GLSL (shader_translate.h). The post-pass follows the pattern of SwitchWakerHD's
// runtime/src/gfx/deko/glsl_convert.cpp (MPL-2.0): fix the source line by line where uam's dialect
// differs, keep everything else byte for byte. No code of it is copied; the rules are this
// project's.
#include "shader_translate.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <map>
#include <sstream>
#include <tuple>

#include "src/tint/api/tint.h"
#include "src/tint/lang/core/ir/module.h"
#include "src/tint/lang/glsl/writer/writer.h"
#include "src/tint/lang/wgsl/inspector/inspector.h"
#include "src/tint/lang/wgsl/reader/reader.h"
#include "src/tint/utils/text/styled_text.h"

namespace swdk {
namespace {

using tint::BindingPoint;
using RB = tint::inspector::ResourceBinding;

constexpr uint32_t kPlaceholderSamplerGroup = 1000;  // textureLoad without a sampler

std::string sampler_name(uint32_t slot) { return "cos_tex" + std::to_string(slot); }

bool is_uniform(RB::ResourceType t) { return t == RB::ResourceType::kUniformBuffer; }
bool is_storage(RB::ResourceType t) {
    return t == RB::ResourceType::kStorageBuffer || t == RB::ResourceType::kReadOnlyStorageBuffer;
}
bool is_texture(RB::ResourceType t) {
    return t == RB::ResourceType::kSampledTexture || t == RB::ResourceType::kDepthTexture ||
           t == RB::ResourceType::kMultisampledTexture || t == RB::ResourceType::kDepthMultisampledTexture;
}

}  // namespace

void translate_init() { tint::Initialize(); }

TranslateResult wgsl_to_glsl(const std::string& wgsl, const std::string& entryPoint, ShaderStage stage,
                             BindingScheme scheme) {
    TranslateResult r;
    tint::Source::File file("shader.wgsl", wgsl);
    tint::wgsl::reader::Options readerOptions;
    readerOptions.allowed_features = tint::wgsl::AllowedFeatures::Everything();
    tint::Program program = tint::wgsl::reader::Parse(&file, readerOptions);
    if (!program.IsValid()) {
        r.error = "WGSL: " + program.Diagnostics().Str();
        return r;
    }

    tint::inspector::Inspector inspector(program);
    bool found = false;
    for (const auto& ep : inspector.GetEntryPoints()) {
        if (ep.name == entryPoint) {
            found = true;
            r.immediateSize = ep.immediate_data_size;
            const bool wantVertex = stage == ShaderStage::Vertex;
            if ((ep.stage == tint::inspector::PipelineStage::kVertex) != wantVertex) {
                r.error = "entry point " + entryPoint + " is not of the requested stage";
                return r;
            }
        }
    }
    if (!found) {
        r.error = "no entry point " + entryPoint;
        return r;
    }

    tint::glsl::writer::Options options;
    options.version = tint::glsl::writer::Version(tint::glsl::writer::Version::Standard::kDesktop, 4, 6);
    options.entry_point_name = entryPoint;
    options.disable_robustness = true;
    options.depth_zero_to_one = true;
    options.disable_position_y_negation = true;
    options.use_uniform_buffers = true;
    options.strip_all_names = false;
    options.disable_polyfill_integer_div_mod = false;
    options.placeholder_sampler_bind_point = {kPlaceholderSamplerGroup, 0};

    // Buffers: GX's fixed numbers, or (group, binding) order.
    std::vector<RB> resources = inspector.GetResourceBindings(entryPoint);
    std::sort(resources.begin(), resources.end(), [](const RB& a, const RB& b) {
        return std::tie(a.bind_group, a.binding) < std::tie(b.bind_group, b.binding);
    });
    uint32_t nextUbo = 0, nextSsbo = 0;
    for (const RB& res : resources) {
        const BindingPoint wgslBp{res.bind_group, res.binding};
        if (is_uniform(res.resource_type) || is_storage(res.resource_type)) {
            const bool ubo = is_uniform(res.resource_type);
            uint32_t slot;
            if (scheme == BindingScheme::GX) {
                slot = ubo ? (res.bind_group == 1 && res.binding == 0 ? 0u : 99u)
                           : (res.bind_group == 0 ? res.binding : 99u);
            } else {
                slot = ubo ? nextUbo++ : nextSsbo++;
            }
            if (slot >= kImmediatesUboSlot) {
                r.error = "no slot for buffer @group(" + std::to_string(res.bind_group) + ") @binding(" +
                          std::to_string(res.binding) + ")";
                return r;
            }
            (ubo ? options.bindings.uniform : options.bindings.storage).emplace(wgslBp, BindingPoint{0, slot});
            r.bindings.push_back({ubo ? SlotBinding::Kind::Uniform : SlotBinding::Kind::Storage, uint8_t(slot),
                                  res.bind_group, res.binding});
        } else if (is_texture(res.resource_type)) {
            // the post-remapping number only names the pair below; uam sees the post-pass binding
            options.bindings.texture.emplace(wgslBp, BindingPoint{0, res.bind_group * 64 + res.binding});
        } else if (res.resource_type == RB::ResourceType::kSampler) {
            options.bindings.sampler.emplace(wgslBp, BindingPoint{0, res.bind_group * 64 + res.binding});
        } else {
            r.error = "unsupported resource " + res.variable_name;
            return r;
        }
    }

    // Combined samplers: one slot per (texture, sampler) pair the entry point uses.
    auto pairs = inspector.GetSamplerAndNonSamplerTextureUses(
        entryPoint, BindingPoint{kPlaceholderSamplerGroup, 0});
    std::sort(pairs.begin(), pairs.end(), [](const auto& a, const auto& b) {
        return std::tie(a.texture_binding_point.group, a.texture_binding_point.binding,
                        a.sampler_binding_point.group, a.sampler_binding_point.binding) <
               std::tie(b.texture_binding_point.group, b.texture_binding_point.binding,
                        b.sampler_binding_point.group, b.sampler_binding_point.binding);
    });
    std::vector<SamplerSlot> samplerSlots;
    uint32_t nextSampler = 0;
    for (const auto& pair : pairs) {
        const BindingPoint tex = pair.texture_binding_point, smp = pair.sampler_binding_point;
        uint32_t slot;
        if (scheme == BindingScheme::GX) {
            slot = tex.group == 2 && tex.binding % 2 == 0 ? tex.binding / 2 : 99u;
        } else {
            slot = nextSampler++;
        }
        if (slot >= 32) {
            r.error = "no sampler slot for texture @group(" + std::to_string(tex.group) + ") @binding(" +
                      std::to_string(tex.binding) + ")";
            return r;
        }
        const BindingPoint texRemapped{0, tex.group * 64 + tex.binding};
        const BindingPoint smpRemapped = smp.group == kPlaceholderSamplerGroup
                                             ? options.placeholder_sampler_bind_point
                                             : BindingPoint{0, smp.group * 64 + smp.binding};
        const std::string name = sampler_name(slot);
        options.sampler_texture_to_name.emplace(
            tint::glsl::writer::CombinedTextureSamplerPair{texRemapped, smpRemapped, false}, name);
        samplerSlots.push_back({name, slot});
        r.bindings.push_back({SlotBinding::Kind::Sampler, uint8_t(slot), tex.group, tex.binding});
    }
    if (r.immediateSize) {
        r.bindings.push_back({SlotBinding::Kind::Immediates, uint8_t(kImmediatesUboSlot), 0, 0});
        // every stage declares the whole block: the same layout in both stages
        options.minimum_immediate_size = (r.immediateSize + 15u) & ~15u;
    }

    auto ir = tint::wgsl::reader::ProgramToLoweredIR(program);
    if (ir != tint::Success) {
        r.error = "IR: " + ir.Failure().reason;
        return r;
    }
    auto out = tint::glsl::writer::Generate(ir.Get(), options);
    if (out != tint::Success) {
        r.error = "GLSL writer: " + out.Failure().reason;
        return r;
    }
    r.glsl = std::move(out->glsl);
    if (!post_pass(&r.glsl, samplerSlots, &r.error, &r.std140Rewrites)) {
        r.error = "post-pass: " + r.error;
        return r;
    }
    r.ok = true;
    return r;
}

namespace {

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && isspace(uint8_t(s[a]))) a++;
    while (b > a && isspace(uint8_t(s[b - 1]))) b--;
    return s.substr(a, b - a);
}
bool ident_char(char c) { return isalnum(uint8_t(c)) || c == '_'; }
std::string first_token(const std::string& t) {
    size_t e = 0;
    while (e < t.size() && ident_char(t[e])) e++;
    return t.substr(0, e);
}

}  // namespace

// Tint's GLSL writer with use_uniform_buffers prints a uniform struct member by member with the WGSL
// offsets as if GLSL laid it out the same, and its Std140 transform only fixes matrices. Aurora's GX
// Uniform has `array_start: array<u32, 12>` (WGSL's uniform_buffer_standard_layout: stride 4), which
// Tint prints as `uint array_start[12];`: in a std140 block every array element takes 16 bytes, so the
// array grows from 48 to 192 bytes and every member after it (proj, postex_mtx, nrm_mtx, the TEV
// colours, lights, fog...) is read 144 bytes too far. Here every scalar array of a struct reachable
// from a uniform block becomes a vec4 array of the same bytes (N % 4 == 0), and its constant-index
// reads `.name[K]` become `.name[K / 4].xyzw[K % 4]`; anything else of that kind (a vec2 array, a
// dynamic index, a whole-array read) is refused, so a layout GLSL cannot express fails loudly instead
// of drawing garbage.
bool std140_scalar_arrays(std::string* glsl, std::string* error, uint32_t* rewritten) {
    std::vector<std::string> lines;
    {
        std::istringstream in(*glsl);
        std::string line;
        while (std::getline(in, line)) lines.push_back(line);
    }
    struct Body {
        size_t first = 0, last = 0;  // member lines [first, last)
    };
    std::map<std::string, Body> structs;
    std::vector<Body> blocks;
    for (size_t i = 0; i < lines.size(); i++) {
        const std::string t = trim(lines[i]);
        const bool isStruct = t.rfind("struct ", 0) == 0 && t.size() > 1 && t.back() == '{';
        // "uniform v_ubuf_block_ubo {" (its layout(binding = N, std140) on the line before, or on this one)
        const bool isBlock = !isStruct && !t.empty() && t.back() == '{' &&
                             (t.rfind("uniform ", 0) == 0 || t.find(") uniform ") != std::string::npos);
        if (!isStruct && !isBlock) continue;
        Body b;
        b.first = i + 1;
        size_t j = i + 1;
        while (j < lines.size() && trim(lines[j]).rfind('}', 0) != 0) j++;
        b.last = j;
        if (isStruct) {
            structs[first_token(trim(t.substr(7)))] = b;
        } else {
            blocks.push_back(b);
        }
        i = j;
    }
    // the structs a uniform block reaches
    std::vector<Body> reach = blocks;
    std::map<std::string, bool> seen;
    for (size_t k = 0; k < reach.size(); k++) {
        for (size_t i = reach[k].first; i < reach[k].last; i++) {
            const std::string type = first_token(trim(lines[i]));
            auto it = structs.find(type);
            if (it != structs.end() && !seen[type]) {
                seen[type] = true;
                reach.push_back(it->second);
            }
        }
    }
    std::map<std::string, bool> names;  // rewritten member names
    std::vector<bool> isDecl(lines.size(), false);
    for (const Body& b : reach) {
        for (size_t i = b.first; i < b.last; i++) {
            const std::string t = trim(lines[i]);
            const size_t open = t.find('[');
            if (open == std::string::npos) continue;
            const std::string type = first_token(t);
            const size_t sp = t.find(' ');
            const std::string name = sp == std::string::npos ? std::string() : t.substr(sp + 1, open - sp - 1);
            const uint32_t n = uint32_t(strtoul(t.c_str() + open + 1, nullptr, 10));
            const char* vec = type == "uint" ? "uvec4" : type == "int" ? "ivec4" : type == "float" ? "vec4" : nullptr;
            if (type == "vec2" || type == "uvec2" || type == "ivec2" || type == "bool") {
                *error = "std140: a uniform array of " + type + " has a 16-byte stride in GLSL: " + t;
                return false;
            }
            if (!vec) continue;  // (vec3, vec4, matrices and structs: the same stride in std140)
            if (n == 0 || n % 4 != 0 || name.empty() || t.compare(t.size() - 2, 2, "];") != 0) {
                *error = "std140: a uniform scalar array GLSL cannot lay out as WGSL does: " + t;
                return false;
            }
            const size_t indent = lines[i].find_first_not_of(" \t");
            lines[i] = lines[i].substr(0, indent) + vec + " " + name + "[" + std::to_string(n / 4) + "];";
            names[name] = true;
            isDecl[i] = true;
            if (rewritten) (*rewritten)++;
        }
    }
    if (names.empty()) return true;
    for (size_t i = 0; i < lines.size(); i++) {
        if (isDecl[i]) continue;
        std::string& line = lines[i];
        for (const auto& entry : names) {
            const std::string& name = entry.first;
            const std::string dotted = "." + name;
            std::string out;
            size_t pos = 0, at;
            while ((at = line.find(dotted, pos)) != std::string::npos) {
                const size_t end = at + dotted.size();
                if (end < line.size() && ident_char(line[end])) {  // (another member: .array_start_x)
                    out.append(line, pos, end - pos);
                    pos = end;
                    continue;
                }
                // .name[K], .name[Ku], .name[int(K)], .name[uint(K)]
                size_t j = end;
                bool ok = j < line.size() && line[j] == '[';
                j++;
                bool cast = false;
                for (const char* c : {"uint(", "int("}) {
                    if (ok && line.compare(j, strlen(c), c) == 0) {
                        j += strlen(c);
                        cast = true;
                        break;
                    }
                }
                const size_t digits = j;
                while (ok && j < line.size() && isdigit(uint8_t(line[j]))) j++;
                ok = ok && j > digits;
                const uint32_t index = ok ? uint32_t(strtoul(line.c_str() + digits, nullptr, 10)) : 0;
                if (ok && j < line.size() && line[j] == 'u') j++;
                if (ok && cast) ok = j < line.size() && line[j++] == ')';
                ok = ok && j < line.size() && line[j] == ']';
                if (!ok) {
                    *error = "std140: " + name + " read without a constant index: " + trim(line);
                    return false;
                }
                out.append(line, pos, at - pos);
                out += dotted + "[" + std::to_string(index / 4) + "]." + "xyzw"[index % 4];
                pos = j + 1;
            }
            out.append(line, pos, std::string::npos);
            line = std::move(out);
        }
    }
    std::string joined;
    joined.reserve(glsl->size() + 64);
    for (const std::string& l : lines) {
        joined += l;
        joined += '\n';
    }
    *glsl = std::move(joined);
    return true;
}

bool post_pass(std::string* glsl, const std::vector<SamplerSlot>& samplerSlots, std::string* error,
               uint32_t* std140Rewrites) {
    if (!std140_scalar_arrays(glsl, error, std140Rewrites)) return false;
    std::map<std::string, uint32_t> slots;
    for (const auto& s : samplerSlots) slots[s.name] = s.slot;

    std::istringstream in(*glsl);
    std::string out, line;
    out.reserve(glsl->size() + 256);
    while (std::getline(in, line)) {
        // "uniform highp sampler2D cos_tex0;" (also isampler2D, usampler2D, sampler2DShadow, ...)
        const size_t u = line.find("uniform ");
        const size_t sp = line.find("sampler");
        if (u != std::string::npos && sp != std::string::npos && line.find("layout(") == std::string::npos &&
            line.find('{') == std::string::npos && !line.empty() && line.back() == ';') {
            const size_t nameEnd = line.size() - 1;
            const size_t nameStart = line.find_last_of(' ', nameEnd) + 1;
            const std::string name = line.substr(nameStart, nameEnd - nameStart);
            auto it = slots.find(name);
            if (it == slots.end()) {
                *error = "no slot for sampler " + name;
                return false;
            }
            out += line.substr(0, u) + "layout(binding = " + std::to_string(it->second) + ") " + line.substr(u) +
                   "\n";
            continue;
        }
        // Dual-source blending: Tint asks for the ES extension even for desktop GLSL, where
        // layout(index = 1) is core (GL 3.3); uam refuses the extension name.
        if (line.rfind("#extension GL_EXT_blend_func_extended", 0) == 0) continue;
        // "layout(location = 0) uniform uint tint_immediates[16];": Tint's immediates are an array
        // of u32 in a default-block uniform. In a std140 block an array of uint has a 16-byte
        // stride, so the block holds uvec4s instead and every (constant) index is rewritten below.
        if (line.find(" tint_immediates[") != std::string::npos && line.find("uniform uint ") != std::string::npos) {
            const size_t open = line.find(" tint_immediates[") + 17;
            const uint32_t words = uint32_t(strtoul(line.c_str() + open, nullptr, 10));
            if (words == 0 || words > 64 || line.compare(line.size() - 2, 2, "];") != 0) {
                *error = "unexpected immediates declaration: " + line;
                return false;
            }
            out += "layout(binding = " + std::to_string(kImmediatesUboSlot) +
                   ", std140) uniform tint_immediates_ubo {\n  uvec4 tint_immediates_v[" +
                   std::to_string((words + 3) / 4) + "];\n};\n";
            continue;
        }
        if (line.find("tint_immediates") != std::string::npos) {
            // tint_immediates[5u] -> tint_immediates_v[1].y
            std::string fixed;
            size_t pos = 0, at;
            while ((at = line.find("tint_immediates", pos)) != std::string::npos) {
                fixed.append(line, pos, at - pos);
                size_t i = at + 15;
                if (i >= line.size() || line[i] != '[') {
                    *error = "unexpected use of the immediates: " + line;
                    return false;
                }
                size_t j = i + 1;
                while (j < line.size() && isdigit(uint8_t(line[j]))) j++;
                const bool constant = j > i + 1;
                if (j < line.size() && line[j] == 'u') j++;
                if (!constant || j >= line.size() || line[j] != ']') {
                    *error = "immediates indexed with a non-constant (uam reads .x for a dynamic component "
                             "index into a uniform vector): " + line;
                    return false;
                }
                const uint32_t index = uint32_t(strtoul(line.c_str() + i + 1, nullptr, 10));
                fixed += "tint_immediates_v[" + std::to_string(index / 4) + "]." + "xyzw"[index % 4];
                pos = j + 1;
            }
            fixed.append(line, pos, std::string::npos);
            line = std::move(fixed);
        }
        out += line;
        out += '\n';
    }
    *glsl = std::move(out);
    return true;
}

// shader_translate.h: the helpers exactly as gx/shader.cpp's build_shader_source writes them (after
// fmt's brace unescaping), and what replaces them.
uint32_t tev_fast_rewrite(std::string* wgsl) {
    static const char kByteF32[] =
        "fn tev_byte_f32(in: f32) -> i32 {\n"
        "  return i32(round(in * 255.0));\n"
        "}\n";
    static const char kByteF32New[] =
        "fn tev_byte_f32(in: f32) -> i32 {\n"
        "  return bitcast<i32>(max(in * 255.0, -4194304.0) + 12582912.0) - 0x4B400000;\n"
        "}\n";
    static const char kByteVec3[] =
        "fn tev_byte_vec3f(in: vec3f) -> vec3i {\n"
        "  return vec3i(round(in * 255.0));\n"
        "}\n";
    static const char kByteVec3New[] =
        "fn tev_byte_vec3f(in: vec3f) -> vec3i {\n"
        "  return bitcast<vec3i>(max(in * 255.0, vec3f(-4194304.0)) + 12582912.0) - vec3i(0x4B400000);\n"
        "}\n"
        "\n"
        "fn tev_i2f(v: i32) -> f32 {\n"
        "  return bitcast<f32>(v + 0x4B400000) - 12582912.0;\n"
        "}\n";
    static const char kRegularF32[] = "  return f32(tev_regular_i32(tev_byte_f32(a), tev_byte_f32(b), tev_byte_f32(c), "
                                      "tev_byte_f32(d), bias, scale, is_sub)) /\n";
    static const char kRegularF32New[] = "  return tev_i2f(tev_regular_i32(tev_byte_f32(a), tev_byte_f32(b), "
                                         "tev_byte_f32(c), tev_byte_f32(d), bias, scale, is_sub)) /\n";
    static const char* const kRegularVec3[3] = {
        "    f32(tev_regular_i32(ai.x, bi.x, ci.x, di.x, bias, scale, is_sub)) / 255.0,\n",
        "    f32(tev_regular_i32(ai.y, bi.y, ci.y, di.y, bias, scale, is_sub)) / 255.0,\n",
        "    f32(tev_regular_i32(ai.z, bi.z, ci.z, di.z, bias, scale, is_sub)) / 255.0,\n",
    };
    struct Site {
        const char* from;
        std::string to;
        size_t at;
    };
    std::vector<Site> sites = {{kByteF32, kByteF32New, 0}, {kByteVec3, kByteVec3New, 0}, {kRegularF32, kRegularF32New, 0}};
    for (const char* v : kRegularVec3) {
        std::string to = v;
        to.replace(4, 3, "tev_i2f");  // "    f32(" -> "    tev_i2f("
        sites.push_back({v, to, 0});
    }
    // every site exactly once (a module without TEV regular ops still has the helpers)
    for (Site& s : sites) {
        s.at = wgsl->find(s.from);
        if (s.at == std::string::npos || wgsl->find(s.from, s.at + 1) != std::string::npos) return 0;
    }
    std::sort(sites.begin(), sites.end(), [](const Site& a, const Site& b) { return a.at > b.at; });
    for (const Site& s : sites) wgsl->replace(s.at, strlen(s.from), s.to);
    return uint32_t(sites.size());
}

// ---- tev_unit_rewrite (begin; shader_translate.h. Plain string work, no Tint: H4's host test compiles
// this part on its own, local/tmp/cpu/h4_unit_rewrite_test.cpp)
namespace {

// fs_main's lines as gx/shader.cpp writes them (build_shader_source, the TEV stage loop and after it):
//   var prev: vec4f;  |  var prev = ubuf.tevprev;  |  var tevregN: vec4f;  |  var tevregN = ubuf.tevregN;
//   let tev_stageN_color_a = <colour arg>;
//   REG = vec4f(clamp(<op>, vec3f(0.0), vec3f(1.0)), REG.a);          (or the S10 bounds)
//   REG.a = clamp(<op>, 0.0, 1.0);                                       (or the S10 bounds)
//   prev = vec4f(tevregN.rgb, prev.a);  |  prev.a = tevregN.a;
//   prev = tev_overflow_vec4f(prev);
// A register's channels (rgb, a) are known: 0 nothing, 1 in [0, 1], 2 sat(fl(R) / 255) for an integer R
// (a clamped regular op, or zero). Any other assignment to a register forgets it.
struct UnitState {
    uint8_t rgb[4] = {}, a[4] = {};
    std::map<std::string, uint8_t> lets;
    bool textures = false;
};

std::string ut_trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && isspace(uint8_t(s[a]))) a++;
    while (b > a && isspace(uint8_t(s[b - 1]))) b--;
    return s.substr(a, b - a);
}
bool ut_ident(char c) { return isalnum(uint8_t(c)) || c == '_'; }

// the ')' matching s[open] == '(' (npos: none)
size_t ut_close(const std::string& s, size_t open) {
    int depth = 0;
    for (size_t i = open; i < s.size(); i++) {
        if (s[i] == '(') depth++;
        if (s[i] == ')' && --depth == 0) return i;
    }
    return std::string::npos;
}

// the top-level arguments of the call whose '(' is at open and ')' at close: [begin, end) each
bool ut_args(const std::string& s, size_t open, size_t close, std::vector<std::pair<size_t, size_t>>* out) {
    out->clear();
    int depth = 0;
    size_t start = open + 1;
    for (size_t i = open + 1; i < close; i++) {
        if (s[i] == '(') depth++;
        if (s[i] == ')') depth--;
        if (depth < 0) return false;
        if (s[i] == ',' && depth == 0) {
            out->push_back({start, i});
            start = i + 1;
        }
    }
    out->push_back({start, close});
    return depth == 0;
}

// "name(...)" as the whole of e: the inside, else false
bool ut_call(const std::string& e, const char* name, std::string* inner) {
    const size_t n = strlen(name);
    if (e.size() < n + 2 || e.compare(0, n, name) != 0 || e[n] != '(') return false;
    if (ut_close(e, n) != e.size() - 1) return false;
    *inner = e.substr(n + 1, e.size() - n - 2);
    return true;
}

int ut_reg(const std::string& name) {
    if (name == "prev") return 0;
    if (name == "tevreg0") return 1;
    if (name == "tevreg1") return 2;
    if (name == "tevreg2") return 3;
    return -1;
}

bool ut_swizzle(const std::string& s) {
    if (s.empty() || s.size() > 4) return false;
    for (char c : s)
        if (c != 'r' && c != 'g' && c != 'b' && c != 'a') return false;
    return true;
}

// what is known of an operand expression (0, 1 or 2 as UnitState)
uint8_t ut_level(const UnitState& st, std::string e) {
    e = ut_trim(e);
    std::string inner;
    if (ut_call(e, "", &inner)) return ut_level(st, inner);  // (x)
    if (ut_call(e, "vec3f", &inner)) {
        std::vector<std::pair<size_t, size_t>> args;
        if (!ut_args(e, 5, e.size() - 1, &args) || args.size() != 1) return 0;  // (splats only)
        return ut_level(st, inner);
    }
    if (auto it = st.lets.find(e); it != st.lets.end()) return it->second;
    const size_t dot = e.find('.');
    if (dot == std::string::npos) return 0;
    const std::string base = e.substr(0, dot), sw = e.substr(dot + 1);
    if (const int r = ut_reg(base); r >= 0) {
        if (sw == "rgb") return st.rgb[r];
        if (sw == "a") return st.a[r];
        return 0;
    }
    // sampledN.<swizzle>
    if (base.rfind("sampled", 0) == 0 && base.size() > 7 && ut_swizzle(sw)) {
        for (size_t i = 7; i < base.size(); i++)
            if (!isdigit(uint8_t(base[i]))) return 0;
        return st.textures ? 1 : 0;
    }
    // ubuf.kcolorN.<swizzle>
    if (base == "ubuf") {
        const size_t dot2 = sw.find('.');
        if (dot2 == std::string::npos) return 0;
        const std::string field = sw.substr(0, dot2);
        if (field.size() == 7 && field.compare(0, 6, "kcolor") == 0 && field[6] >= '0' && field[6] <= '3' &&
            ut_swizzle(sw.substr(dot2 + 1)))
            return 1;
    }
    return 0;
}

// tev_regular_vec3f / tev_regular_f32 calls in line: an A, B or C operand tev_overflow_*(X) with X
// known in [0, 1] becomes tev_unit_*(X): X * 255 through a bitcast pair (an opaque value, as the
// overflow's subtraction) / 255. The byte conversion's * 255 meets the same (T * fl(1/255)) * 255 as
// before, so whatever the compiler folds there it folds the same way, on T = y instead of m = y: uam
// folds it (the DKSH is the same as with the bare X: H4's GLSL check); a compiler that does not would
// compute the same thing from the same value. used[0]/[1]: the vec3f / f32 helper is now used.
uint32_t ut_operands(const UnitState& st, std::string* line, bool used[2]) {
    uint32_t n = 0;
    for (const char* fn : {"tev_regular_vec3f(", "tev_regular_f32("}) {
        const bool f32 = strcmp(fn, "tev_regular_f32(") == 0;
        const char* ovf = f32 ? "tev_overflow_f32" : "tev_overflow_vec3f";
        size_t pos = 0;
        while ((pos = line->find(fn, pos)) != std::string::npos) {
            if (pos > 0 && ut_ident((*line)[pos - 1])) {
                pos++;
                continue;
            }
            const size_t open = pos + strlen(fn) - 1;
            const size_t close = ut_close(*line, open);
            std::vector<std::pair<size_t, size_t>> args;
            if (close == std::string::npos || !ut_args(*line, open, close, &args) || args.size() != 7) {
                pos = open + 1;
                continue;
            }
            for (int k = 2; k >= 0; k--) {  // (right to left: the earlier ranges stay valid)
                const std::string arg = line->substr(args[k].first, args[k].second - args[k].first);
                const std::string t = ut_trim(arg);
                std::string x;
                if (!ut_call(t, ovf, &x) || ut_level(st, x) == 0) continue;
                const size_t lead = arg.find_first_not_of(" \t");
                line->replace(args[k].first + lead, t.size(), (f32 ? "tev_unit_f32(" : "tev_unit_vec3f(") + x + ")");
                used[f32 ? 1 : 0] = true;
                n++;
            }
            pos = open + 1;
        }
    }
    return n;
}

// a clamp(<op>, lo, hi): the level its result gets (lo/hi as the unit bounds of the channel kind)
uint8_t ut_clamp_level(const std::string& op, const char* lo, const char* hi, const char* regularFn) {
    std::string inner;
    if (!ut_call(op, "clamp", &inner)) return 0;
    std::vector<std::pair<size_t, size_t>> args;
    if (!ut_args(op, 5, op.size() - 1, &args) || args.size() != 3) return 0;
    const auto arg = [&](int k) { return ut_trim(op.substr(args[k].first, args[k].second - args[k].first)); };
    if (arg(1) != lo || arg(2) != hi) return 0;
    std::string call;
    return ut_call(arg(0), regularFn, &call) ? 2 : 1;
}

// any assignment to a register in line t (REG, REG.x, with =, +=, -=, *=, /=): its channels forgotten
void ut_forget_writes(UnitState* st, const std::string& t) {
    static const char* const kNames[4] = {"prev", "tevreg0", "tevreg1", "tevreg2"};
    for (int r = 0; r < 4; r++) {
        const size_t len = strlen(kNames[r]);
        size_t pos = 0;
        while ((pos = t.find(kNames[r], pos)) != std::string::npos) {
            const size_t start = pos;
            pos += len;
            if ((start > 0 && (ut_ident(t[start - 1]) || t[start - 1] == '.')) || (pos < t.size() && ut_ident(t[pos])))
                continue;
            size_t j = pos;
            if (j < t.size() && t[j] == '.') {
                j++;
                while (j < t.size() && ut_ident(t[j])) j++;
            }
            while (j < t.size() && t[j] == ' ') j++;
            const bool assign = j < t.size() && ((t[j] == '=' && (j + 1 >= t.size() || t[j + 1] != '=')) ||
                                                 (strchr("+-*/", t[j]) && j + 1 < t.size() && t[j + 1] == '='));
            if (assign) st->rgb[r] = st->a[r] = 0;
        }
    }
}

}  // namespace

uint32_t tev_unit_rewrite(std::string* wgsl, bool textures) {
    const size_t fn = wgsl->find("\nfn fs_main(");
    if (fn == std::string::npos) return 0;
    const size_t bodyOpen = wgsl->find('{', fn);
    if (bodyOpen == std::string::npos) return 0;
    size_t bodyClose = std::string::npos;
    {
        int depth = 0;
        for (size_t i = bodyOpen; i < wgsl->size(); i++) {
            if ((*wgsl)[i] == '{') depth++;
            if ((*wgsl)[i] == '}' && --depth == 0) {
                bodyClose = i;
                break;
            }
        }
    }
    if (bodyClose == std::string::npos) return 0;
    UnitState st;
    st.textures = textures;
    std::string body = wgsl->substr(bodyOpen + 1, bodyClose - bodyOpen - 1), out;
    out.reserve(body.size());
    uint32_t n = 0;
    bool finalUnit = false, used[2] = {false, false};
    size_t at = 0;
    while (at <= body.size()) {
        size_t eol = body.find('\n', at);
        if (eol == std::string::npos) eol = body.size();
        std::string line = body.substr(at, eol - at);
        if (line.find("tev_regular_") != std::string::npos) n += ut_operands(st, &line, used);
        const std::string t = ut_trim(line);
        bool handled = true;
        int r;
        if (t == "var prev: vec4f;") {
            st.rgb[0] = st.a[0] = 2;
        } else if (t.rfind("var tevreg", 0) == 0 && t.size() > 11 && (r = ut_reg(t.substr(4, 7))) > 0 &&
                   t.compare(11, std::string::npos, ": vec4f;") == 0) {
            st.rgb[r] = st.a[r] = 2;
        } else if (t.rfind("let tev_stage", 0) == 0 && t.find(" = ") != std::string::npos && t.back() == ';') {
            const size_t eq = t.find(" = ");
            st.lets[t.substr(4, eq - 4)] = ut_level(st, t.substr(eq + 3, t.size() - eq - 4));
        } else if (t == "prev = tev_overflow_vec4f(prev);") {
            if (st.rgb[0] == 2 && st.a[0] == 2) {
                line.replace(line.find("tev_overflow_vec4f"), 18, "tev_unit_vec4f");
                finalUnit = true;
                n++;
            }
            st.rgb[0] = st.a[0] = 0;
        } else {
            handled = false;
            // REG = vec4f(<op>, REG.a);  /  REG.a = <op>;
            const size_t eq = t.find(" = ");
            const int reg = eq == std::string::npos ? -1 : ut_reg(t.substr(0, eq));
            const int regA = eq != std::string::npos && eq > 2 && t.compare(eq - 2, 2, ".a") == 0
                                 ? ut_reg(t.substr(0, eq - 2))
                                 : -1;
            if (reg >= 0 && t.back() == ';') {
                const std::string rhs = t.substr(eq + 3, t.size() - eq - 4);
                const std::string keepA = ", " + t.substr(0, eq) + ".a)";
                std::string inner;
                if (ut_call(rhs, "vec4f", &inner) && inner.size() > keepA.size() - 1 &&
                    rhs.compare(rhs.size() - keepA.size(), keepA.size(), keepA) == 0) {
                    const std::string op = ut_trim(rhs.substr(6, rhs.size() - 6 - keepA.size()));
                    uint8_t level = ut_clamp_level(op, "vec3f(0.0)", "vec3f(1.0)", "tev_regular_vec3f");
                    if (op.size() > 4 && op.compare(op.size() - 4, 4, ".rgb") == 0 &&
                        ut_reg(op.substr(0, op.size() - 4)) >= 0)
                        level = st.rgb[ut_reg(op.substr(0, op.size() - 4))];  // (a copy)
                    // (the operands this line read were checked above, before its write; REG.a is kept)
                    st.rgb[reg] = level;
                    handled = true;
                }
            } else if (regA >= 0 && t.back() == ';') {
                const std::string rhs = t.substr(eq + 3, t.size() - eq - 4);
                uint8_t level = ut_clamp_level(rhs, "0.0", "1.0", "tev_regular_f32");
                if (rhs.size() > 2 && rhs.compare(rhs.size() - 2, 2, ".a") == 0 && ut_reg(rhs.substr(0, rhs.size() - 2)) >= 0)
                    level = st.a[ut_reg(rhs.substr(0, rhs.size() - 2))];  // (a copy)
                st.a[regA] = level;
                handled = true;
            }
        }
        if (!handled) ut_forget_writes(&st, t);
        out += line;
        if (eol < body.size()) out += '\n';
        at = eol + 1;
    }
    if (n == 0) return 0;
    wgsl->replace(bodyOpen + 1, bodyClose - bodyOpen - 1, out);
    std::string helpers;
    if (used[0])
        helpers += "fn tev_unit_vec3f(in: vec3f) -> vec3f {\n"
                   "  return bitcast<vec3f>(bitcast<vec3u>(in * 255.0)) / 255.0;\n"
                   "}\n\n";
    if (used[1])
        helpers += "fn tev_unit_f32(in: f32) -> f32 {\n"
                   "  return bitcast<f32>(bitcast<u32>(in * 255.0)) / 255.0;\n"
                   "}\n\n";
    if (finalUnit)
        helpers += "fn tev_unit_vec4f(in: vec4f) -> vec4f {\n"
                   "  return max(in * 255.0, vec4f(0.0)) / 255.0;\n"
                   "}\n\n";
    const size_t frag = wgsl->find("@fragment\nfn fs_main(");
    if (frag != std::string::npos) wgsl->insert(frag, helpers);  // (found: fs_main was, above)
    return n;
}
// ---- tev_unit_rewrite (end)

}  // namespace swdk
