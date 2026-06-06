#pragma once

#include "IPlug_include_in_plug_hdr.h"
#include "PulpEmbedEditor.h"

#include <memory>

using namespace iplug;

// A minimal iPlug2 plugin whose editor is a Pulp-imported design embedded via
// the pulp_view_embed C ABI (UI NONE — no IGraphics). DSP is silent passthrough.
class PulpEmbedIPlug final : public Plugin
{
public:
  PulpEmbedIPlug(const InstanceInfo& info);

  void* OpenWindow(void* pParent) override;
  void OnParentWindowResize(int width, int height) override;
  void OnIdle() override;

#if IPLUG_DSP
  void ProcessBlock(sample** inputs, sample** outputs, int nFrames) override;
#endif

private:
  std::unique_ptr<pulp_iplug2::PulpEmbedEditor> mEmbed;
  int mIdleFrames = 0;
};
