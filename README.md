# pulp-embed-iplug2

An [iPlug2](https://github.com/iPlug2/iPlug2) adapter for
[`pulp-view-embed`](../pulp-view-embed): embed a Pulp-imported design (e.g. a
Figma frame) in an iPlug2 plugin's editor.

> Status: **experiment**. The adapter helper (`include/PulpEmbedEditor.h`) is
> framework-neutral — it drives the flat C ABI and takes the parent native
> window as a `void*`, so it compiles and links against the C ABI **without** the
> iPlug2 SDK. The compile/link check (`ctest`) verifies that. The `example/`
> project builds a real plugin in **APP + VST3 + AU + CLAP** and passes the
> format validators (auval / pluginval / CLAP load smoke). DAW load (Logic,
> REAPER, …) remains a manual step.

## Status / what works / known limitations / roadmap

**What works (macOS):**

- Framework-neutral `PulpEmbedEditor` helper drives the `pulp_view_embed` flat C
  ABI from an iPlug2 editor's open/resize/tick/close hooks; no Pulp C++ type
  enters your iPlug2 translation units.
- `example/` builds a real plugin in **APP + VST3 + AU + CLAP** whose editor IS
  the embedded Pulp design (`UI NONE`), at full fidelity (importer JS bundle).
- Both parenting paths: pulp-parents (APP/VST3/CLAP) and host-parents
  (AUv2 Cocoa view factory).
- **Interactive parameters (ABI v3):** design controls bind by string key to
  iPlug2 `IParam`s — a dragged control writes the host param; host automation
  pushes values back. iPlug2 maps its own params to the design's keys.
- Offscreen render mode, `resolve_resource` host asset callback, and bundled
  fonts all flow through the same C ABI.
- Headless self-check (`PULP_EMBED_SELFCHECK=1`) proves the editor renders +
  live-captures without a DAW; auval / pluginval / CLAP load smokes pass.

**Known limitations:**

- macOS only today (Windows once `pulp-view-embed` registers a Windows
  `PluginViewHost` factory).
- Requires an installed Pulp SDK + a developer-supplied iPlug2 checkout with its
  prebuilt deps and the VST3/CLAP SDKs.
- `pulp_embed_resize` validates the DPI scale but treats it as advisory for the
  windowed editor (the host window drives backing scale); only the deterministic
  capture APIs honor a caller scale.
- Real-DAW load (automation/state/window management) is a manual check.

**Resolved design questions** (from the foreign-host-embedding plan):

- *Event-loop tick* — borrowed from the host: the iPlug2 editor's UI timer (and
  Pulp's GPU display-link) drives `pulp_embed_tick`; the adapter runs no loop.
- *Parameter model* — string-key based, which maps cleanly onto iPlug2's
  `IParam` (the plugin owns the param objects and binds each to a design key).

**Roadmap:** Windows host; real-DAW automation/state validation; zero-copy GPU
compositing (currently CPU RGBA readback for the offscreen path).

Third-party attribution for the borrowed iPlug2 IPlugEffect resource files is in
[`NOTICE.md`](NOTICE.md).

## How it embeds

iPlug2 hands the editor a parent native window/view on open. There are two
parenting paths, selected by whether the format adapter supplies a parent:

- **APP / VST3 / CLAP** pass a real parent native view → **pulp-parents mode**:
  `pulp_embed_attach(view, parent)` adds Pulp's child into that parent and fires
  the view-opened lifecycle (`PulpEmbedEditor::open`).
- **AUv2** — its Cocoa view factory calls `OpenWindow(nullptr)` and parents the
  returned `NSView` itself → **host-parents mode**: return Pulp's native handle
  and call `pulp_embed_notify_attached()` (`PulpEmbedEditor::notifyAttached`)
  once the host has placed the child in a live window.

`PulpEmbedIPlug::OpenWindow` handles both. The framework-neutral helper:

```cpp
#include "PulpEmbedEditor.h"

// in your plugin:
pulp_iplug2::PulpEmbedEditor embed_{"design.ir.json", kW, kH};

// editor open (e.g. IGraphics OpenWindow(void* pParent) or a custom editor path):
embed_.open(pParent);
// host resize:
embed_.resize(w, h, scale);
// UI timer (~30 Hz):
embed_.tick();
// editor close:
embed_.close();
```

No Pulp C++ type appears in your iPlug2 translation units — only
`pulp_view_embed.h`.

## Build (compile/link check)

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH=/path/to/pulp-sdk-install \
  -DPULP_VIEW_EMBED_DIR=/path/to/pulp-view-embed
cmake --build build -j
ctest --test-dir build --output-on-failure   # runs compilecheck
```

`pulp_embed_iplug2` is an INTERFACE target your iPlug2 plugin links; it brings in
the `pulp_view_embed` C ABI.

## Example plugin (APP + VST3 + AU + CLAP)

`example/` is a real iPlug2 plugin (UI NONE — the editor IS the embedded Pulp
design) that embeds the "VST Style" Figma fixture at full fidelity in all four
formats.

### Prerequisites

iPlug2 needs its prebuilt deps and the VST3 + CLAP SDKs (AU needs no extra SDK):

```bash
# in your iPlug2 checkout:
Dependencies/download-prebuilt-libs.sh
Dependencies/IPlug/download-vst3-sdk.sh v3.7.12_build_20
Dependencies/IPlug/download-clap-sdks.sh main main   # SDK + helpers paired at main
```

> CLAP note: clap-helpers `main` references draft extensions (webview,
> mini-curve-display) that only exist in CLAP SDK `main`, so pair them at the
> same ref. An older CLAP SDK tag with `main` helpers will fail to compile.

### Build

```bash
cmake -S example -B example/build -DCMAKE_BUILD_TYPE=Release \
  -DIPLUG2_DIR=/path/to/iPlug2 \
  -DPULP_VIEW_EMBED_DIR=/path/to/pulp-view-embed \
  -DCMAKE_PREFIX_PATH=/path/to/pulp-sdk-install
# all formats:
cmake --build example/build -j
# or one at a time:
cmake --build example/build --target PulpEmbedIPlug-app  -j   # standalone .app
cmake --build example/build --target PulpEmbedIPlug-vst3 -j
cmake --build example/build --target PulpEmbedIPlug-au   -j
cmake --build example/build --target PulpEmbedIPlug-clap -j
```

iPlug2 deploys each built format to the system folder
(`~/Library/Audio/Plug-Ins/{VST3,Components,CLAP}`, app to `~/Applications`) as
a post-build step.

`PulpEmbedEditor(source, w, h)` auto-detects a `ui.js` bundle dir (high-fidelity
scripted-UI path) vs a `.json` DesignIR file (lightweight native path). The AU
type/subtype/manufacturer (`aufx` / `Pemb` / `Pulp`) live in the per-format
Info.plist templates under `example/resources/`.

### Validate

```bash
# AU — auval (built in to macOS); opens the AU briefly, silent passthrough DSP:
auval -v aufx Pemb Pulp

# VST3 — pluginval if installed (brew install --cask pluginval):
/Applications/pluginval.app/Contents/MacOS/pluginval --strictness-level 5 \
  --validate example/build/out/PulpEmbedIPlug.vst3
# …else the bundled headless VST3 module-load smoke:
tools/build-vst3-smoke.sh && /tmp/vst3_load_smoke example/build/out/PulpEmbedIPlug.vst3

# CLAP — clap-validator if installed, else the bundled headless load smoke:
cc -std=c11 -I"$IPLUG2_DIR/Dependencies/IPlug/CLAP_SDK/include" \
  tools/clap_load_smoke.c -o /tmp/clap_load_smoke
/tmp/clap_load_smoke example/build/out/PulpEmbedIPlug.clap
```

### Verify the editor renders the embed (headless)

The standalone self-checks the running editor: it attaches the embed, renders a
few live GPU frames, writes the live back buffer + a deterministic Skia raster,
and quits.

```bash
PULP_EMBED_SELFCHECK=1 example/build/out/PulpEmbedIPlug.app/Contents/MacOS/PulpEmbedIPlug
# → SELFCHECK gpu=1 liveCapture=ok deterministic=ok
# writes /tmp/iplug-live-capture.png (live GPU) and /tmp/iplug-render.png
```

The same `OpenWindow(parent)` → attach → render path runs in the VST3/CLAP
plugin editor; pluginval's editor open/close test exercises it under a host.

## Remaining work

- Manual load in a real DAW (Logic, REAPER, Ableton, Bitwig) — automated
  validators above cover ABI + render lifecycle, but in-DAW behavior (automation,
  state, window management) is a human check.
- DPI / backing-scale change handling on the editor resize path.

## License

MIT.
