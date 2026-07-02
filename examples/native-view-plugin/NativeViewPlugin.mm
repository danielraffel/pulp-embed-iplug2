#include "NativeViewPlugin.h"
#include "IPlug_include_in_plug_src.h"

// The native-view path mounts a compiled Pulp View, so — unlike the file-based
// example — this TU DOES include a Pulp C++ type. That is intentional for the
// "hand-built compiled View" ctor.
#include <pulp/view/design_frame_view.hpp>

#import <AppKit/AppKit.h>
#include <cstdlib>
#include <cstring>

using pulp::view::DesignFrameElement;

namespace {

// Build the compiled root view: a dark panel with two param_key'd knobs
// ("gain", "cutoff") plus one keyless (visual-only) knob. This is the same
// substrate the adapter's binding-test drives headlessly, here mounted in a
// real plugin editor.
std::unique_ptr<pulp::view::View> makeRootView()
{
  auto knob = [](float cx, std::string key) {
    DesignFrameElement e;
    e.kind = DesignFrameElement::Kind::knob;
    e.cx = cx; e.cy = 40.0f; e.hit_radius = 18.0f;
    e.needle_d = "M0 0L0 -8";
    e.param_key = std::move(key);
    return e;
  };
  const std::string svg =
      R"(<svg width="240" height="80" xmlns="http://www.w3.org/2000/svg">)"
      R"(<rect x="0" y="0" width="240" height="80" fill="#16181d"/></svg>)";
  std::vector<DesignFrameElement> els{knob(40.f, "gain"), knob(120.f, "cutoff"),
                                      knob(200.f, "")};
  return std::make_unique<pulp::view::DesignFrameView>(svg, std::move(els));
}

}  // namespace

PulpNativeViewIPlug::PulpNativeViewIPlug(const InstanceInfo& info)
: iplug::Plugin(info, MakeConfig(kNumParams, 1))
{
  GetParam(kGain)->InitDouble("Gain", 80.0, 0.0, 100.0, 0.01, "%");
  GetParam(kCutoff)->InitDouble("Cutoff", 1000.0, 20.0, 20000.0, 0.01, "Hz",
                                IParam::kFlagsNone, "", IParam::ShapePowCurve(3.0));
  SetEditorSize(PLUG_WIDTH, PLUG_HEIGHT);
}

void PulpNativeViewIPlug::wireEditorAccessors()
{
  if (!mEmbed) return;

  // ABI v8: value read-outs come from iPlug2's own IParam::GetDisplay — the
  // WDL_String call lives HERE (in the plugin TU), never in the neutral header.
  mEmbed->setParamDisplayFormatter([this](const std::string& key, double norm) {
    const int idx = key == "gain" ? kGain : (key == "cutoff" ? kCutoff : -1);
    if (idx < 0) return std::string();
    WDL_String s;
    GetParam(idx)->GetDisplayForHost(norm, /*normalized*/ true, s);
    std::string out(s.Get() ? s.Get() : "");
    if (const char* lbl = GetParam(idx)->GetLabel(); lbl && *lbl) { out += ' '; out += lbl; }
    return out;
  });

  // ABI v8: a non-parameter UI action (e.g. an "open manual" link in the design).
  mEmbed->setHostActionHandler([](const std::string& action, const std::string& args_json) {
    fprintf(stderr, "[native-view] host action '%s' args=%s\n",
            action.c_str(), args_json.c_str());
    return 1;  // handled
  });
}

void* PulpNativeViewIPlug::OpenWindow(void* pParent)
{
  if (!mEmbed)
  {
    mEmbed = std::make_unique<pulp_iplug2::PulpEmbedEditor>(
        pulp::embed::NativeViewFactory{makeRootView}, PLUG_WIDTH, PLUG_HEIGHT, *this,
        /* design key -> paramIdx */ { {"gain", kGain}, {"cutoff", kCutoff} });
    wireEditorAccessors();
  }

  mHostParents = (pParent == nullptr);
  mNotifiedAttached = false;
  if (!mHostParents)
    mEmbed->open(pParent);

  if (void* nv = mEmbed->nativeHandle())
  {
    NSView* child = (__bridge NSView*) nv;
    [child setAutoresizingMask:NSViewNotSizable];
    [child setFrame:NSMakeRect(0.f, 0.f, (float) PLUG_WIDTH, (float) PLUG_HEIGHT)];
  }

  // Size the view to the design's preferred size, honoring aspect/min/max.
  mEmbed->applyPreferredSizeOnOpen();
  return mEmbed->nativeHandle();
}

void PulpNativeViewIPlug::CloseWindow()
{
  if (mEmbed)
  {
    mEmbed->close();
    mNotifiedAttached = false;
  }
  OnUIClose();
}

void PulpNativeViewIPlug::OnParentWindowResize(int width, int height)
{
  if (!mEmbed) return;
  // Clamp the host-requested size to the design's constraints before
  // applying it (the idiomatic iPlug2 ConstrainEditorResize seam).
  mEmbed->constrainSize(width, height);
  mEmbed->resize(width, height, 1.0f);
}

void PulpNativeViewIPlug::OnIdle()
{
  if (!mEmbed) return;

  if (mHostParents && !mNotifiedAttached)
    mNotifiedAttached = mEmbed->notifyAttached();

  mEmbed->tick();

  if (std::getenv("PULP_EMBED_SELFCHECK") && ++mIdleFrames == 40)
  {
    const bool live = mEmbed->writeCapturePng("/tmp/iplug-nativeview-live.png");
    const bool det  = mEmbed->writeRenderPng("/tmp/iplug-nativeview-render.png",
                                             PLUG_WIDTH, PLUG_HEIGHT);
    fprintf(stderr,
            "SELFCHECK gpu=%d bound=%d liveCapture=%s deterministic=%s\n",
            mEmbed->isGpuBacked() ? 1 : 0, mEmbed->boundParameterCount(),
            live ? "ok" : "FAIL", det ? "ok" : "FAIL");
    [NSApp terminate:nil];
  }
}

#if IPLUG_DSP
void PulpNativeViewIPlug::ProcessBlock(sample** inputs, sample** outputs, int nFrames)
{
  const int nChans = NOutChansConnected();
  for (int c = 0; c < nChans; c++)
    std::memset(outputs[c], 0, nFrames * sizeof(sample));
}
#endif
