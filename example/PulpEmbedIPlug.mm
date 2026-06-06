#include "PulpEmbedIPlug.h"
#include "IPlug_include_in_plug_src.h"

#import <AppKit/AppKit.h>
#include <cstdlib>
#include <cstring>

#ifndef PULP_EMBED_DEMO_IR
 #define PULP_EMBED_DEMO_IR ""
#endif

PulpEmbedIPlug::PulpEmbedIPlug(const InstanceInfo& info)
: Plugin(info, MakeConfig(0, 1))
{
  // Tell the host (incl. the APP standalone) the editor's logical size, so the
  // window opens at the design size instead of a fallback default. Without this
  // a UI-NONE plugin reports a 0×0 editor and the standalone picks its own size.
  SetEditorSize(PLUG_WIDTH, PLUG_HEIGHT);
}

void* PulpEmbedIPlug::OpenWindow(void* pParent)
{
  if (!mEmbed)
    mEmbed = std::make_unique<pulp_iplug2::PulpEmbedEditor>(PULP_EMBED_DEMO_IR, PLUG_WIDTH, PLUG_HEIGHT);
  // pulp-parents mode: attach Pulp's child into iPlug2's parent NSView and fire
  // the view-opened lifecycle, then hand the host the child view to track.
  mEmbed->open(pParent);
  // Frame the returned child view to the design size (mirrors IPlugCocoaUI) and
  // size the embed to match, so it isn't stretched to a fallback window size.
  if (void* nv = mEmbed->nativeHandle())
  {
    NSView* child = (__bridge NSView*) nv;
    // The host attaches the child with a width|height-sizable autoresizing mask.
    // The SWELL standalone creates its dialog at the template size (300×300) and
    // only AFTER OpenWindow grows the content view to GetEditorWidth/Height
    // (1000×600) via ClientResize. With autoresizing on, that +700×+300 delta is
    // added to the child's frame (1000×600 → 1700×900) and, because the SWELL
    // dialog is flipped, shoves origin.y to -300 — the enlarged/clipped symptom.
    // Pin the child to a fixed frame and disable autoresizing so it tracks the
    // content view exactly (both are 1000×600 once ClientResize runs).
    [child setAutoresizingMask:NSViewNotSizable];
    [child setFrame:NSMakeRect(0.f, 0.f, (float) PLUG_WIDTH, (float) PLUG_HEIGHT)];
  }
  mEmbed->resize(PLUG_WIDTH, PLUG_HEIGHT, 1.0f);
  return mEmbed->nativeHandle();
}

void PulpEmbedIPlug::OnParentWindowResize(int width, int height)
{
  if (mEmbed) mEmbed->resize(width, height, 1.0f);
}

void PulpEmbedIPlug::OnIdle()
{
  if (!mEmbed) return;
  mEmbed->tick();

  // Self-check: after the editor has rendered a few live frames, write the LIVE
  // GPU back buffer + the deterministic render, then quit. Lets CI verify the
  // running standalone without screen-recording permissions.
  if (std::getenv("PULP_EMBED_SELFCHECK") && ++mIdleFrames == 40)
  {
    const bool live = mEmbed->writeCapturePng("/tmp/iplug-live-capture.png");
    const bool det  = mEmbed->writeRenderPng("/tmp/iplug-render.png", PLUG_WIDTH, PLUG_HEIGHT);
    fprintf(stderr, "SELFCHECK gpu=%d liveCapture=%s deterministic=%s\n",
            mEmbed->isGpuBacked() ? 1 : 0, live ? "ok" : "FAIL", det ? "ok" : "FAIL");
    [NSApp terminate:nil];
  }
}

#if IPLUG_DSP
void PulpEmbedIPlug::ProcessBlock(sample** inputs, sample** outputs, int nFrames)
{
  // Silent passthrough — this demo is about the UI, not audio.
  const int nChans = NOutChansConnected();
  for (int c = 0; c < nChans; c++)
    std::memset(outputs[c], 0, nFrames * sizeof(sample));
}
#endif
