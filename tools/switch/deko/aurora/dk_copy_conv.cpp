// SPDX-License-Identifier: MIT
// MLB Power Pros 2008 Recomp (tools/switch/deko, 2026-10-26; workstream C). The shaders are GLSL
// translations of Aurora's fixed WGSL (MIT, Copyright (c) 2022 Luke Street; our Wiicompiled fork's
// lib/gfx/tex_copy_conv.cpp, tex_copy_format_contract.hpp and tex_palette_conv.cpp); the glue follows
// SwitchWaker's fixed draws (switch/deko/aurora/dk_encode.cpp at 56c0758; LICENSE.SwitchWaker).
// The deko3d renderer's EFB copy conversions and palette conversions (dk_gfx.hpp copy_convert,
// palette_convert): what tex_copy_conv::run / blit and tex_palette_conv::run record into Dawn, as one
// full-target triangle drawn into the request's dst attachment view, on the command buffer owner
// between passes (dk_common.inc) or in efb_ram's readback batch (dk_efb_ram.cpp).
//
// Shaders. Aurora's WGSL is written out in GLSL for uam line by line: the same functions in the same
// order, the same constants and the same operations, so the results round as Dawn's do:
//   - textureSample -> texture, textureLoad -> texelFetch, textureDimensions -> textureSize;
//   - WGSL's clamp(e, lo, hi) is min(max(e, lo), hi) by definition: written so (GLSL's clamp is
//     undefined when lo > hi, which the copy clamp rows can give);
//   - WGSL's round() rounds half to even: roundEven;
//   - no robustness clamps of our own on texelFetch: Dawn on NVK skips Tint's texture robustness
//     (VulkanUseImageRobustAccess2: the hardware's out-of-bounds fetch returns 0) and deko3d's fetch
//     is the same hardware instruction; the depth copy clamps its coordinates itself, as the WGSL does.
// The copy conversions share one vertex shader (the uv transform from the uniform); the palette
// conversions use the renderer's own blit vertex shader (core/fixed_shaders.cpp), whose positions and
// uvs are tex_palette_conv's. Positions are WebGPU's clip space, drawn with the game passes' viewport
// swizzle (swdk::C.flipY), so row 0 of the source lands on row 0 of the target as with Dawn.
// GLSL bindings: uniform block 0 (the UV transform, both stages), textures 0 (the source) and 1 (the
// TLUT). Each shader is compiled on its first use (uam, or the SD card's DKSH cache) and kept.
//
// State. Dawn's pass for these clears the target and draws one triangle over all of it with no
// blending, no depth and no culling; the triangle writes every pixel and no shader discards, so the
// clear is left out. After the draw a Fragments barrier with the texture cache invalidated (later
// passes and copies sample the result; WIICOMPILED_DK_LEAN_BARRIERS: the encoder's conversion_barrier),
// and the encoder's state cache is reset.
#include "dk_encode.hpp"
#include "dk_gfx.hpp"

#include "gfx/common.hpp"
#include "gfx/tex_copy_conv.hpp"
#include "gfx/tex_palette_conv.hpp"
#include "gfx/texture.hpp"
#include "gx/gx.hpp"

#include <algorithm>
#include <iterator>
#include <mutex>
#include <string>
#include <unordered_map>

namespace aurora::gfx::dk {
using swdk::R;
using swdk::ShaderStage;

namespace {

// ---- tex_copy_conv.cpp

constexpr const char* kCopyVs = R"(#version 460
layout(std140, binding = 0) uniform UVTransform {
    vec2 offset;
    vec2 scale;
    vec4 copy_filter;
    vec4 flags;
} uv_xf;
layout(location = 0) out vec2 outUv;
void main() {
    const vec2 positions[3] = vec2[3](vec2(-1.0, 1.0), vec2(-1.0, -3.0), vec2(3.0, 1.0));
    const vec2 uvs[3] = vec2[3](vec2(0.0, 0.0), vec2(0.0, 2.0), vec2(2.0, 0.0));
    gl_Position = vec4(positions[gl_VertexID], 0.0, 1.0);
    outUv = uvs[gl_VertexID] * uv_xf.scale + uv_xf.offset;
}
)";

// ShaderPreamble (colour copies): sampler 0 + texture 1 of the WGSL are texture 0 here
constexpr const char* kColorPreamble = R"(#version 460
layout(location = 0) in vec2 inUv;
layout(location = 0) out vec4 outColor;
layout(binding = 0) uniform sampler2D src;
layout(std140, binding = 0) uniform UVTransform {
    vec2 offset;
    vec2 scale;
    vec4 copy_filter;
    vec4 flags;
} uv_xf;

float intensity(vec3 rgb) {
    // ITU-R BT.601 luma coefficients
    return dot(rgb, vec3(0.257, 0.504, 0.098)) + 16.0 / 255.0;
}

float quantize4(float v) {
    return floor(v * 16.0) / 15.0;
}

vec4 apply_opaque_alpha(vec4 c) {
    if (uv_xf.flags.x != 0.0) {
        return vec4(c.rgb, 1.0);
    }
    return c;
}

vec2 clamp_copy_uv(vec2 uv) {
    return vec2(uv.x, min(max(uv.y, uv_xf.flags.z), uv_xf.flags.w));
}

vec4 sample_copy(vec2 uv) {
    vec4 current = texture(src, clamp_copy_uv(uv));
    if (uv_xf.copy_filter.w == 0.0) {
        return apply_opaque_alpha(current);
    }

    vec2 tex_size = vec2(textureSize(src, 0));
    vec2 pixel_size = vec2(1.0, 1.0) / tex_size;
    float row_stride = max(uv_xf.flags.y, 1.0);
    vec4 prev = texture(src, clamp_copy_uv(uv - vec2(0.0, pixel_size.y * row_stride)));
    vec4 next = texture(src, clamp_copy_uv(uv + vec2(0.0, pixel_size.y * row_stride)));
    vec3 prev_rgb = floor(prev.rgb * 255.0 + vec3(0.5));
    vec3 current_rgb = floor(current.rgb * 255.0 + vec3(0.5));
    vec3 next_rgb = floor(next.rgb * 255.0 + vec3(0.5));
    vec3 filtered_rgb = min(
        floor((prev_rgb * uv_xf.copy_filter.x +
               current_rgb * uv_xf.copy_filter.y +
               next_rgb * uv_xf.copy_filter.z) / 64.0),
        vec3(255.0));
    vec4 filtered = vec4(filtered_rgb / 255.0, current.a);
    return apply_opaque_alpha(filtered);
}
)";

// DepthShaderPreamble: texture 0 (texture_depth_2d) is the depth view; texelFetch reads the stored
// depth in .r, as textureLoad of a depth texture
constexpr const char* kDepthPreambleHead = R"(#version 460
layout(location = 0) in vec2 inUv;
layout(location = 0) out vec4 outColor;
layout(binding = 0) uniform sampler2D src;
layout(std140, binding = 0) uniform UVTransform {
    vec2 offset;
    vec2 scale;
    vec4 copy_filter;
    vec4 flags;
} uv_xf;
)";

constexpr const char* kDepthZ24Reversed = R"(
uint gx_z24_at_coord(ivec2 unclamped_coord) {
    ivec2 tex_size = textureSize(src, 0);
    ivec2 coord = min(max(unclamped_coord, ivec2(0)), tex_size - ivec2(1));
    float depth = texelFetch(src, coord, 0).r;
    return min(uint(min(max(1.0 - depth, 0.0), 1.0) * 16777215.0 + 0.5), 0x00ffffffu);
}
)";

constexpr const char* kDepthZ24Forward = R"(
uint gx_z24_at_coord(ivec2 unclamped_coord) {
    ivec2 tex_size = textureSize(src, 0);
    ivec2 coord = min(max(unclamped_coord, ivec2(0)), tex_size - ivec2(1));
    float depth = texelFetch(src, coord, 0).r;
    return min(uint(min(max(depth, 0.0), 1.0) * 16777215.0 + 0.5), 0x00ffffffu);
}
)";

constexpr const char* kDepthPreambleTail = R"(
uvec3 gx_depth_bytes(uint z24) {
    return uvec3((z24 >> 16u) & 0xffu, (z24 >> 8u) & 0xffu, z24 & 0xffu);
}

ivec2 clamp_copy_coord(ivec2 coord, ivec2 tex_size) {
    int top = int(floor(uv_xf.flags.z * float(tex_size.y)));
    int bottom = int(floor(uv_xf.flags.w * float(tex_size.y)));
    return ivec2(coord.x, min(max(coord.y, top), bottom));
}

uvec4 sample_depth_copy(vec2 uv) {
    ivec2 tex_size = textureSize(src, 0);
    ivec2 current_coord = clamp_copy_coord(ivec2(floor(uv * vec2(tex_size))), tex_size);
    uvec3 current = gx_depth_bytes(gx_z24_at_coord(current_coord));
    if (uv_xf.copy_filter.w == 0.0) {
        return uvec4(current, 255u);
    }

    // GX applies its vertical copy filter to depth copies too, filtering the high/middle/low Z bytes
    // independently before the destination format picks bytes. Alpha is unfiltered and 255.
    int row_stride = max(int(roundEven(uv_xf.flags.y)), 1);
    uvec3 prev = gx_depth_bytes(gx_z24_at_coord(
        clamp_copy_coord(current_coord - ivec2(0, row_stride), tex_size)));
    uvec3 next = gx_depth_bytes(gx_z24_at_coord(
        clamp_copy_coord(current_coord + ivec2(0, row_stride), tex_size)));
    uvec3 coefficients = uvec3(uv_xf.copy_filter.xyz);
    uvec3 combined = prev * coefficients.x + current * coefficients.y + next * coefficients.z;
    uvec3 filtered = combined >> uvec3(6u);

    // The copy-filter accumulator wraps to nine bits when coefficients can
    // produce values at or above 512, before saturating to an eight-bit byte.
    if (coefficients.x + coefficients.y + coefficients.z >= 128u) {
        filtered = filtered & uvec3(0x1ffu);
    }
    filtered = min(filtered, uvec3(255u));
    return uvec4(filtered, 255u);
}
)";

// FragPassthrough (tex_copy_conv::blit, RGBA8Unorm or BGRA8Unorm targets)
constexpr const char* kFragPassthrough = R"(
void main() {
    outColor = sample_copy(inUv);
}
)";

struct ConvShader {
    GXTexFmt fmt;
    const char* name;
    const char* body;
};

// ConvPipelines: every colour conversion writes RGBA8Unorm
constexpr ConvShader kColorConvs[] = {
    {GX_TF_I4, "tex_copy_conv_i4", R"(
void main() {
    vec3 rgb = sample_copy(inUv).rgb;
    float i = quantize4(intensity(rgb));
    outColor = vec4(i, i, i, i);
}
)"},
    {GX_TF_I8, "tex_copy_conv_i8", R"(
void main() {
    vec3 rgb = sample_copy(inUv).rgb;
    float i = intensity(rgb);
    outColor = vec4(i, i, i, i);
}
)"},
    {GX_TF_IA4, "tex_copy_conv_ia4", R"(
void main() {
    vec4 c = sample_copy(inUv);
    float i = quantize4(intensity(c.rgb));
    float a = quantize4(c.a);
    outColor = vec4(i, i, i, a);
}
)"},
    {GX_TF_IA8, "tex_copy_conv_ia8", R"(
void main() {
    vec4 c = sample_copy(inUv);
    float i = intensity(c.rgb);
    outColor = vec4(i, i, i, c.a);
}
)"},
    {GX_TF_RGB565, "tex_copy_conv_rgb565", R"(
void main() {
    vec4 c = sample_copy(inUv);
    outColor = vec4(c.rgb, 1.0);
}
)"},
    {GX_CTF_R4, "tex_copy_conv_r4", R"(
void main() {
    float r = quantize4(sample_copy(inUv).r);
    outColor = vec4(r, r, r, r);
}
)"},
    {GX_CTF_RA4, "tex_copy_conv_ra4", R"(
void main() {
    vec4 c = sample_copy(inUv);
    float r = quantize4(c.r);
    outColor = vec4(r, r, r, quantize4(c.a));
}
)"},
    {GX_CTF_RA8, "tex_copy_conv_ra8", R"(
void main() {
    vec4 c = sample_copy(inUv);
    outColor = vec4(c.r, c.r, c.r, c.a);
}
)"},
    {GX_CTF_A8, "tex_copy_conv_a8", R"(
void main() {
    float a = sample_copy(inUv).a;
    outColor = vec4(a, a, a, a);
}
)"},
    {GX_CTF_R8, "tex_copy_conv_r8", R"(
void main() {
    float r = sample_copy(inUv).r;
    outColor = vec4(r, r, r, r);
}
)"},
    {GX_CTF_G8, "tex_copy_conv_g8", R"(
void main() {
    float g = sample_copy(inUv).g;
    outColor = vec4(g, g, g, g);
}
)"},
    {GX_CTF_B8, "tex_copy_conv_b8", R"(
void main() {
    float b = sample_copy(inUv).b;
    outColor = vec4(b, b, b, b);
}
)"},
    {GX_CTF_RG8, "tex_copy_conv_rg8", R"(
void main() {
    vec4 c = sample_copy(inUv);
    outColor = vec4(c.r, c.r, c.r, c.g);
}
)"},
    {GX_CTF_GB8, "tex_copy_conv_gb8", R"(
void main() {
    vec4 c = sample_copy(inUv);
    outColor = vec4(c.g, c.g, c.g, c.b);
}
)"},
};

// DepthConvPipelines (FragZ16 is tex_copy_format_contract.hpp's Z16FragmentShader)
constexpr ConvShader kDepthConvs[] = {
    {GX_TF_Z8, "tex_copy_conv_z8", R"(
void main() {
    uvec4 depth_bytes = sample_depth_copy(inUv);
    float z8 = float(depth_bytes.r) / 255.0;
    outColor = vec4(z8, z8, z8, z8);
}
)"},
    {GX_TF_Z16, "tex_copy_conv_z16", R"(
void main() {
    uvec4 depth_bytes = sample_depth_copy(inUv);
    float i = float(depth_bytes.r) / 255.0;
    outColor = vec4(i, i, i, 1.0);
}
)"},
    {GX_TF_Z24X8, "tex_copy_conv_z24x8", R"(
void main() {
    uvec4 depth_bytes = sample_depth_copy(inUv);
    float r = float(depth_bytes.r) / 255.0;
    float g = float(depth_bytes.g) / 255.0;
    float b = float(depth_bytes.b) / 255.0;
    outColor = vec4(r, g, b, 1.0);
}
)"},
};

// ---- tex_palette_conv.cpp (the vertex shader: swdk::kBlitVs). The WGSL's sampler (binding 0) is
// unused; its src (binding 1) and tlut (binding 2) are textures 0 and 1 here.

constexpr const char* kPaletteDirect = R"(#version 460
layout(location = 0) in vec2 inUv;
layout(location = 0) out vec4 outColor;
layout(binding = 0) uniform isampler2D src;
layout(binding = 1) uniform sampler2D tlut;
void main() {
    vec2 texSize = vec2(textureSize(src, 0));
    ivec2 coord = ivec2(floor(inUv * texSize));
    int idx = texelFetch(src, coord, 0).r;
    outColor = texelFetch(tlut, ivec2(idx, 0), 0);
}
)";

constexpr const char* kPaletteFromFloat8 = R"(#version 460
layout(location = 0) in vec2 inUv;
layout(location = 0) out vec4 outColor;
layout(binding = 0) uniform sampler2D src;
layout(binding = 1) uniform sampler2D tlut;
void main() {
    vec2 texSize = vec2(textureSize(src, 0));
    ivec2 coord = ivec2(floor(inUv * texSize));
    float r = texelFetch(src, coord, 0).r;
    outColor = texelFetch(tlut, ivec2(int(r * 255.0), 0), 0);
}
)";

constexpr const char* kPaletteFromFloat4 = R"(#version 460
layout(location = 0) in vec2 inUv;
layout(location = 0) out vec4 outColor;
layout(binding = 0) uniform sampler2D src;
layout(binding = 1) uniform sampler2D tlut;
void main() {
    vec2 texSize = vec2(textureSize(src, 0));
    ivec2 coord = ivec2(floor(inUv * texSize));
    float r = texelFetch(src, coord, 0).r;
    outColor = texelFetch(tlut, ivec2(int(r * 15.0), 0), 0);
}
)";

// ---- shaders, compiled on first use and kept (by name; command buffer owner, under a mutex anyway:
// swdk::fixed_shader must not run twice at once for the same GLSL)
struct ShaderSlot {
    bool tried = false;
    const DkShader* shader = nullptr;
};
std::mutex g_shaderMutex;
std::unordered_map<std::string, ShaderSlot> g_shaders;

const DkShader* shader(ShaderStage stage, const char* name, const char* part0, const char* part1 = "",
                       const char* part2 = "", const char* part3 = "") {
    std::lock_guard<std::mutex> lock(g_shaderMutex);
    ShaderSlot& s = g_shaders[name];
    if (!s.tried) {
        s.tried = true;
        std::string glsl = part0;
        glsl += part1;
        glsl += part2;
        glsl += part3;
        const uint64_t t0 = swdk::now_ns();
        s.shader = swdk::fixed_shader(stage, glsl.c_str(), name);  // (logs a failure)
        swdk::dklog("conversions: shader %s %s in %.1f ms", name, s.shader ? "ready" : "FAILED",
                    double(swdk::now_ns() - t0) / 1e6);
    }
    return s.shader;
}

const DkShader* depth_conv_shader(const ConvShader& c) {
    return shader(ShaderStage::Fragment, c.name, kDepthPreambleHead,
                  gx::UseReversedZ ? kDepthZ24Reversed : kDepthZ24Forward, kDepthPreambleTail, c.body);
}

const ConvShader* find_conv(const ConvShader* table, size_t count, GXTexFmt fmt) {
    for (size_t i = 0; i < count; i++)
        if (table[i].fmt == fmt) return &table[i];
    return nullptr;
}

struct TexBind {
    uint32_t binding;
    DkResHandle handle;
};

// One full-target triangle into `out` (its view's level), as Dawn's conversion pass draws it
void conv_draw(View* out, const DkShader* vs, const DkShader* fs, const TexBind* tex, uint32_t texCount,
               DkGpuAddr ubo, uint32_t uboSize) {
    Tex* t = out->tex;
    const uint32_t w = std::max(t->width >> out->baseMip, 1u), h = std::max(t->height >> out->baseMip, 1u);
    // (a shader loaded since the last bind: the GPU's shader caches see the new code, as bind_pipe does)
    if (swdk::code_take_written()) dkCmdBufBarrier(R.cmd, DkBarrier_None, DkInvalidateFlags_Shader);
    const DkImageView target = level_view(t, out->baseMip);
    const DkImageView* targets[] = {&target};
    swdk::tiled_cache_flush();
    dkCmdBufBindRenderTargets(R.cmd, targets, 1, nullptr);
    const DkViewportSwizzle sw = {DkSwizzle_PositiveX, swdk::C.flipY ? DkSwizzle_NegativeY : DkSwizzle_PositiveY,
                                  DkSwizzle_PositiveZ, DkSwizzle_PositiveW};
    dkCmdBufSetViewportSwizzles(R.cmd, 0, &sw, 1);
    swdk::set_view(0, 0, w, h);
    const DkShader* shaders[] = {vs, fs};
    dkCmdBufBindShaders(R.cmd, DkStageFlag_GraphicsMask, shaders, 2);
    swdk::bind_2d_state(false);
    dkCmdBufBindVtxAttribState(R.cmd, nullptr, 0);
    dkCmdBufBindVtxBufferState(R.cmd, nullptr, 0);
    if (ubo != 0) {
        dkCmdBufBindUniformBuffer(R.cmd, DkStage_Vertex, 0, ubo, uboSize);
        dkCmdBufBindUniformBuffer(R.cmd, DkStage_Fragment, 0, ubo, uboSize);
    }
    for (uint32_t i = 0; i < texCount; i++) dkCmdBufBindTexture(R.cmd, DkStage_Fragment, tex[i].binding, tex[i].handle);
    dkCmdBufDraw(R.cmd, DkPrimitive_Triangles, 3, 1, 0, 0);
    // later passes, conversions and copies read what this wrote (and may overwrite what it sampled)
    conversion_barrier(t);
    EncodeStats& s = encode_stats();
    s.conversions++;
    invalidate_state();
    note_work();
    gpu_mark(GpuWork::Conversion);
}

bool target_ok(View* out) {
    if (out && out->tex && out->tex->valid && out->tex->render && out->tex->samples == 1) return true;
    note_unsupported("a conversion whose target has no deko3d render image (skipped)");
    return false;
}

}  // namespace

bool copy_convert(const tex_copy_conv::ConvRequest& req, bool shaderSampled) {
    if (!req.dst) return false;
    const bool depth = !shaderSampled && gx::is_depth_format(req.fmt);
    const ConvShader* conv = nullptr;
    if (shaderSampled) {
        // tex_copy_conv::blit: the blit pipelines exist for these two target formats only (Dawn: fatal)
        if (req.dst->format != wgpu::TextureFormat::RGBA8Unorm && req.dst->format != wgpu::TextureFormat::BGRA8Unorm) {
            note_unsupported("a shader-sampled EFB copy into a format tex_copy_conv has no blit for (skipped)");
            return false;
        }
    } else {
        conv = depth ? find_conv(kDepthConvs, std::size(kDepthConvs), req.fmt)
                     : find_conv(kColorConvs, std::size(kColorConvs), req.fmt);
        if (!conv) {
            note_unsupported("an EFB copy conversion of a format tex_copy_conv has no pipeline for (skipped)");
            return false;
        }
    }
    View* src = find_view(req.srcView.Get());
    View* out = find_view(req.dst->attachmentTextureView.Get());
    if (!src || !src->tex || !src->tex->valid) {
        note_unsupported("an EFB copy conversion whose source has no deko3d image (skipped)");
        return false;
    }
    if (!target_ok(out)) return false;
    const DkShader* vs = shader(ShaderStage::Vertex, "tex_copy_conv_vs", kCopyVs);
    const DkShader* fs = shaderSampled ? shader(ShaderStage::Fragment, "tex_copy_conv_blit", kColorPreamble, kFragPassthrough)
                         : depth       ? depth_conv_shader(*conv)
                                       : shader(ShaderStage::Fragment, conv->name, kColorPreamble, conv->body);
    if (!vs || !fs) {
        note_unsupported("an EFB copy conversion whose shader did not compile (skipped)");
        return false;
    }
    // the colour copies' Nearest / Linear sampler (clamp to edge); the depth copies only fetch
    const uint32_t sampler = !depth && req.sampleFilter == tex_copy_conv::SampleFilter::Linear
                                 ? uint32_t(swdk::kSamplerLinearClamp)
                                 : uint32_t(swdk::kSamplerNearestClamp);
    before_sampling(src->tex);
    const TexBind tex = {0, dkMakeTextureHandle(view_slot(src), sampler)};
    const uint32_t uboSize = std::max<uint32_t>((req.uniformRange.size + 15u) & ~15u, 16u);
    conv_draw(out, vs, fs, &tex, 1, uniform_address(req.uniformRange.offset), uboSize);
    return true;
}

bool palette_convert(const tex_palette_conv::ConvRequest& req) {
    if (!req.src || !req.dst || !req.tlut) return false;
    View* src = find_view(req.src->sampleTextureView.Get());
    View* tlut = find_view(req.tlut->sampleTextureView.Get());
    View* out = find_view(req.dst->attachmentTextureView.Get());
    if (!src || !src->tex || !src->tex->valid || !tlut || !tlut->tex || !tlut->tex->valid) {
        note_unsupported("a palette conversion whose source or TLUT has no deko3d image (skipped)");
        return false;
    }
    if (!target_ok(out)) return false;
    const DkShader* vs = swdk::builtin_shader(swdk::kBlitVs);
    const DkShader* fs = nullptr;
    switch (req.variant) {
    case tex_palette_conv::Variant::Direct:
        fs = shader(ShaderStage::Fragment, "tex_palette_conv_direct", kPaletteDirect);
        break;
    case tex_palette_conv::Variant::FromFloat8:
        fs = shader(ShaderStage::Fragment, "tex_palette_conv_fromfloat8", kPaletteFromFloat8);
        break;
    case tex_palette_conv::Variant::FromFloat4:
        fs = shader(ShaderStage::Fragment, "tex_palette_conv_fromfloat4", kPaletteFromFloat4);
        break;
    }
    if (!vs || !fs) {
        note_unsupported("a palette conversion whose shader did not compile (skipped)");
        return false;
    }
    before_sampling(src->tex);
    before_sampling(tlut->tex);
    const TexBind tex[] = {
        {0, dkMakeTextureHandle(view_slot(src), swdk::kSamplerNearestClamp)},
        {1, dkMakeTextureHandle(view_slot(tlut), swdk::kSamplerNearestClamp)},
    };
    conv_draw(out, vs, fs, tex, 2, 0, 0);
    return true;
}

}  // namespace aurora::gfx::dk
