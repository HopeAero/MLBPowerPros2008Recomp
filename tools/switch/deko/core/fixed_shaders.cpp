// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of
// the MPL was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
// The ImGui shaders are from SwitchWakerHD (https://github.com/centollOS/SwitchWakerHD) at df8fbde:
// runtime/src/gfx/deko/shaders/imgui_{vsh,fsh}.glsl, by way of SwitchWaker (switch/deko/shaders/ at
// 56c0758). The rest is MLB Power Pros 2008 Recomp's (tools/switch/deko, 2026-10-26).
// The renderer's own shaders, GLSL for uam compiled at start on the uam worker (or taken from the SD
// card's cache: core/shaders.cpp) instead of SwitchWaker's build-time DKSH, so the NRO's build needs no
// uam tool:
//   imgui   ImGui's vertices (overlay.cpp): pixel coordinates to normalized ones with y down; deko3d's
//           clip-space y points up (dk.h), so y is negated
//   clear   Aurora's partial clears (gfx/clear.cpp): a full-target triangle, the colour from the blend
//           constant (factors Constant, Zero), the depth from uniform buffer 0 (the clear's uniform)
//   blit    the present (aurora/dk_present.cpp): the EFB through a linear sampler, alpha 1, as Aurora's
//           XFB copy pipeline (webgpu/gpu.cpp create_copy_pipeline)
// Positions in WebGPU's clip space (y up, the device's convention): (-1, 1) is the top-left corner.
#include "dk.h"

namespace swdk {
namespace {

constexpr const char* kImguiVsGlsl = R"(#version 460
layout(location = 0) in vec2 inPos;
layout(location = 1) in vec2 inUv;
layout(location = 2) in vec4 inColor;
layout(location = 0) out vec2 outUv;
layout(location = 1) out vec4 outColor;
layout(std140, binding = 0) uniform Xform {
    vec4 xform;  // scale.xy, translate.xy
};
void main() {
    outUv = inUv;
    outColor = inColor;
    vec2 p = inPos * xform.xy + xform.zw;
    gl_Position = vec4(p.x, -p.y, 0.0, 1.0);
}
)";

constexpr const char* kImguiFsGlsl = R"(#version 460
layout(location = 0) in vec2 inUv;
layout(location = 1) in vec4 inColor;
layout(location = 0) out vec4 outColor;
layout(binding = 0) uniform sampler2D image;
void main() { outColor = inColor * texture(image, inUv); }
)";

constexpr const char* kClearVsGlsl = R"(#version 460
void main() {
    const vec2 pos[3] = vec2[3](vec2(-1.0, 1.0), vec2(-1.0, -3.0), vec2(3.0, 1.0));
    gl_Position = vec4(pos[gl_VertexID], 0.0, 1.0);
}
)";

constexpr const char* kClearFsGlsl = R"(#version 460
layout(std140, binding = 0) uniform ClearUniform {
    vec4 clearDepth;  // x: the depth written
};
layout(location = 0) out vec4 outColor;
void main() {
    outColor = vec4(1.0);
    gl_FragDepth = clearDepth.x;
}
)";

constexpr const char* kBlitVsGlsl = R"(#version 460
layout(location = 0) out vec2 outUv;
void main() {
    const vec2 pos[3] = vec2[3](vec2(-1.0, 1.0), vec2(-1.0, -3.0), vec2(3.0, 1.0));
    const vec2 uvs[3] = vec2[3](vec2(0.0, 0.0), vec2(0.0, 2.0), vec2(2.0, 0.0));
    outUv = uvs[gl_VertexID];
    gl_Position = vec4(pos[gl_VertexID], 0.0, 1.0);
}
)";

constexpr const char* kBlitFsGlsl = R"(#version 460
layout(location = 0) in vec2 inUv;
layout(location = 0) out vec4 outColor;
layout(binding = 0) uniform sampler2D image;
void main() { outColor = vec4(texture(image, inUv).rgb, 1.0); }
)";

const DkShader* g_fixed[kFixedCount] = {};

}  // namespace

void fixed_shaders_init() {
    static const struct {
        ShaderStage stage;
        const char* glsl;
        const char* name;
    } kAll[kFixedCount] = {
        {ShaderStage::Vertex, kImguiVsGlsl, "imgui_vsh"}, {ShaderStage::Fragment, kImguiFsGlsl, "imgui_fsh"},
        {ShaderStage::Vertex, kClearVsGlsl, "clear_vsh"}, {ShaderStage::Fragment, kClearFsGlsl, "clear_fsh"},
        {ShaderStage::Vertex, kBlitVsGlsl, "blit_vsh"},   {ShaderStage::Fragment, kBlitFsGlsl, "blit_fsh"},
    };
    const uint64_t t0 = now_ns();
    int ok = 0;
    for (int i = 0; i < kFixedCount; i++) {
        g_fixed[i] = fixed_shader(kAll[i].stage, kAll[i].glsl, kAll[i].name);
        ok += g_fixed[i] != nullptr;
    }
    dklog("the renderer's own shaders (ImGui, clear, present): %d of %d ready in %.0f ms%s", ok, int(kFixedCount),
          double(now_ns() - t0) / 1e6, ok == kFixedCount ? "" : ": what needs the missing ones is not drawn");
}

const DkShader* builtin_shader(FixedId id) { return g_fixed[id]; }

}  // namespace swdk
