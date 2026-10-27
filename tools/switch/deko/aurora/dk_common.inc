// SPDX-License-Identifier: MIT
// MLB Power Pros 2008 Recomp (tools/switch/deko, 2026-10-26).
// The deko3d renderer's walk over a frame's recorded passes: render_impl's loop (gfx/common.cpp) with
// dk_encode.cpp in place of Dawn's encoder. Included by phase 24 into gfx/common.cpp, inside namespace
// aurora::gfx after render_impl, because RenderPass, Command and recycle_render_passes are private to
// that file. One call is one batch: the passes of a sealed frame (frame worker) or of a staging split
// (producer), read from Aurora's staging slot `stagingSlot` in place.
//
// As render_impl: palette conversions before their pass, empty intermediate passes skipped, load-op
// clears, the pass's commands, the EFB copy after it (exact copies by the copy engine; conversions and
// shader-sampled copies are workstream C's: dk::copy_convert), the depth peek snapshot after the last
// pass. Not done: interpolated replays (interpolatedFrame >= 0 draws the native frame's uniforms;
// aurora.cpp's deko3d encode never asks for them), the resolve source snapshot of interpolation.

static void dk_render(std::vector<RenderPass>& renderPasses, uint32_t stagingSlot, int32_t interpolatedFrame,
                      bool finalize, DebugFrameData& debugFrame, const depth_peek::FrameMapping& depthMapping) {
  ZoneScopedN("deko3d render");
  const bool encodeTextureBakes = interpolatedFrame < 0;
  if (!encodeTextureBakes) dk::note_unsupported("interpolated frame replays (frame interpolation; workstream C)");
  dk::batch_begin(stagingSlot);
  for (u32 i = 0; i < renderPasses.size(); ++i) {
    const auto& passInfo = renderPasses[i];
    dk::pass_prologue();  // (WIICOMPILED_DK_PASS_TIMES: the palette conversions belong to this pass)
    if (encodeTextureBakes) {
      for (const auto& conv : passInfo.paletteConvs) {
        dk::palette_convert(conv);
      }
    }
    const bool hasRenderWork = passInfo.clearColor || passInfo.clearDepth || !passInfo.commands.empty();
    if (i + 1 != renderPasses.size() && !(passInfo.resolveTarget && encodeTextureBakes) && !hasRenderWork) {
      continue;
    }
    dk::PassTargets targets{
        .color = passInfo.colorView.Get(),
        .depth = passInfo.depthView.Get(),
        .width = passInfo.targetSize.width,
        .height = passInfo.targetSize.height,
        .clearColor = passInfo.clearColor,
        .clearDepth = passInfo.clearDepth,
        .clearColorValue = {passInfo.clearColorValue.x(), passInfo.clearColorValue.y(), passInfo.clearColorValue.z(),
                            passInfo.clearColorValue.w()},
        .clearDepthValue = passInfo.clearDepthValue,
        .requireReadyPipelines = passInfo.requireReadyPipelines,
    };
    if (passInfo.msaaSamples > 1) dk::note_unsupported("MSAA (passes drawn single-sampled; workstream C)");
    if (dk::pass_begin(targets)) {
      for (const auto& cmd : passInfo.commands) {
        switch (cmd.type) {
        case CommandType::SetViewport:
          dk::pass_viewport(cmd.data.setViewport);
          break;
        case CommandType::SetScissor:
          dk::pass_scissor(cmd.data.setScissor);
          break;
        case CommandType::Draw: {
          const auto& draw = cmd.data.draw;
          switch (draw.type) {
          case ShaderType::GX: {
            const gfx::Range* uniformOverride = nullptr;
            if (interpolatedFrame >= 0 &&
                static_cast<size_t>(interpolatedFrame) < draw.gx.interpolatedUniformRanges.size() &&
                draw.gx.interpolatedUniformRanges[interpolatedFrame].size != 0) {
              uniformOverride = &draw.gx.interpolatedUniformRanges[interpolatedFrame];
            }
            dk::draw_gx(draw.gx, uniformOverride);
          } break;
          case ShaderType::Clear:
            dk::draw_clear(draw.clear);
            break;
          }
        } break;
        case CommandType::DebugMarker:
          break;
        }
      }
      dk::pass_end();
    }

    // (depth_peek::encode_frame_snapshot's own test: GXPeekZ asked for a snapshot)
    if (finalize && i + 1 == renderPasses.size() && depth_peek::testing::snapshot_requested()) {
      dk::depth_peek_snapshot(passInfo.copySourceDepthView.Get(), passInfo.targetSize.width,
                              passInfo.targetSize.height, passInfo.msaaSamples, depthMapping);
    }

    if (passInfo.resolveTarget && encodeTextureBakes) {
      const bool isDepth = gx::is_depth_format(passInfo.resolveFormat);
      if (passInfo.snapshotColorResolveSource) {
        dk::note_unsupported("the resolve source snapshot of frame interpolation (the EFB is read directly)");
      }
      g_frameWaits.efbTextureCopies.fetch_add(1, std::memory_order_relaxed);
      g_frameWaits.efbCopySourcePixels.fetch_add(
          static_cast<uint64_t>(passInfo.resolveRect.width) * passInfo.resolveRect.height, std::memory_order_relaxed);
      if (passInfo.resolveNeedsConversion || passInfo.resolveNeedsShaderSampling) {
        const tex_copy_conv::ConvRequest convReq{
            .fmt = passInfo.resolveFormat,
            .srcView = isDepth ? passInfo.copySourceDepthView : passInfo.copySourceView,
            .uniformRange = passInfo.resolveUniformRange,
            .dst = passInfo.resolveTarget,
            .sampleFilter = passInfo.resolveLinearSampling ? tex_copy_conv::SampleFilter::Linear
                                                            : tex_copy_conv::SampleFilter::Nearest,
            .forceOpaqueAlpha = passInfo.resolveForceOpaqueAlpha,
        };
        dk::copy_convert(convReq, !passInfo.resolveNeedsConversion);
        dk::note_pass_copy(passInfo.resolveNeedsConversion ? 1 : 2, passInfo.resolveTarget->size.width,
                           passInfo.resolveTarget->size.height, passInfo.resolveRect);
      } else {
        dk::note_pass_copy(3, static_cast<uint32_t>(passInfo.resolveRect.width),
                           static_cast<uint32_t>(passInfo.resolveRect.height), passInfo.resolveRect);
        dk::copy_texture(passInfo.copySourceTexture.Get(), static_cast<uint32_t>(passInfo.resolveRect.x),
                         static_cast<uint32_t>(passInfo.resolveRect.y), passInfo.resolveTarget->texture.Get(),
                         static_cast<uint32_t>(passInfo.resolveRect.width),
                         static_cast<uint32_t>(passInfo.resolveRect.height));
      }
    }
  }
  dk::batch_end(stagingSlot);
  if (finalize) {
    recycle_render_passes(renderPasses);
  }
#if defined(AURORA_GFX_DEBUG_GROUPS)
  if (finalize) {
    debugFrame.groups.clear();
    debugFrame.markers.clear();
  }
#else
  (void)debugFrame;
#endif
}
