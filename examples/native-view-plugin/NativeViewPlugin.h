#pragma once

#include "IPlug_include_in_plug_hdr.h"
#include "PulpEmbedEditor.h"

#include <memory>

using namespace iplug;

// A minimal REAL iPlug2 plugin whose editor is a HAND-BUILT compiled Pulp View
// (a DesignFrameView with two param_key'd knobs) mounted over the plugin via
// the pulp_view_embed C ABI — the NATIVE-VIEW path (no importer bundle, no
// DesignIR file). It exercises the full adapter surface end to end in a real
// IPlugAPP / IPlugVST3 target:
//   * PulpEmbedEditor(NativeViewFactory, w, h, delegate, keyToParamIdx) —
//     binds the compiled view's knobs to kGain / kCutoff bidirectionally,
//   * the ABI v8 accessors (setParamDisplayFormatter -> IParam::GetDisplay,
//     setHostActionHandler),
//   * the resize recipe (preferredSize seeds SetEditorSize; constrainSize
//     clamps host resizes to the design's aspect/min/max).
//
// Unlike the framework-neutral file-based example (example/), the native-view
// path DELIBERATELY pulls in a Pulp C++ type (DesignFrameView) — that is the
// whole point of the "hand-built compiled View" ctor. DSP is silent passthrough
// (this example is about the UI, not audio).
class PulpNativeViewIPlug final : public Plugin
{
public:
  enum EParams { kGain = 0, kCutoff, kNumParams };

  PulpNativeViewIPlug(const InstanceInfo& info);

  void* OpenWindow(void* pParent) override;
  void CloseWindow() override;
  void OnParentWindowResize(int width, int height) override;
  void OnIdle() override;

#if IPLUG_DSP
  void ProcessBlock(sample** inputs, sample** outputs, int nFrames) override;
#endif

private:
  void wireEditorAccessors();

  std::unique_ptr<pulp_iplug2::PulpEmbedEditor> mEmbed;
  bool mHostParents = false;
  bool mNotifiedAttached = false;
  int mIdleFrames = 0;
};
