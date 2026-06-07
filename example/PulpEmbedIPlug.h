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
  void CloseWindow() override;
  void OnParentWindowResize(int width, int height) override;
  void OnIdle() override;

#if IPLUG_DSP
  void ProcessBlock(sample** inputs, sample** outputs, int nFrames) override;
#endif

private:
  std::unique_ptr<pulp_iplug2::PulpEmbedEditor> mEmbed;
  // True when the host parents Pulp's child view itself: AUv2's Cocoa view
  // factory calls OpenWindow(nullptr) and parents the returned NSView. In that
  // case we drive the host-parents lifecycle (notify_attached once the view is
  // in a live window) instead of attach(). VST3/CLAP/APP pass a real parent and
  // use the pulp-parents path (attach).
  bool mHostParents = false;
  bool mNotifiedAttached = false;
  int mIdleFrames = 0;
};
