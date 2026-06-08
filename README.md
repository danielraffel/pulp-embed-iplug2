# pulp-embed-iplug2

An [iPlug2](https://github.com/iPlug2/iPlug2) adapter for
[`pulp-view-embed`](https://github.com/danielraffel/pulp-view-embed): embed a Pulp-imported design (e.g. a
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
- **Interactive parameters (ABI v2 param bridge):** construct `PulpEmbedEditor`
  with the plugin delegate and a design-key → `IParam` index map, and design
  controls bind **bidirectionally** — a dragged control writes the host param
  (`Begin`/`SendParameterValueFromUI`/`End`); host automation / preset recall
  pushes values back into the control (polled on the timer tick). Unlike JUCE
  (string `paramID`), iPlug2 params are integer-indexed, so the plugin supplies
  the key → index map (with an optional case-insensitive `IParam::GetName()`
  fallback). Unmatched controls stay visual-only; `boundParameterCount()` reports
  how many resolved. See *Interactive parameters* below.
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
- *Parameter model* — the plugin owns the `IParam` objects and binds each to a
  design control key. The C ABI bridge is string-keyed; because iPlug2 params are
  integer-indexed (no native string id), the plugin passes a small key → param
  index map (or relies on the `IParam::GetName()` fallback).

**Roadmap:** Windows host; real-DAW automation/state validation; zero-copy GPU
compositing (currently CPU RGBA readback for the offscreen path).

Third-party attribution for the borrowed iPlug2 IPlugEffect resource files is in
[`NOTICE.md`](NOTICE.md).

## What gets embedded (FAQ)

- **What shows up in your editor?** One rendered native child view — the
  imported design, drawn by Pulp — parented into the window iPlug2 hands you on
  open (`UI NONE`: the editor *is* the embedded design).
- **Which designs?** Anything `pulp import-design` can import: **Figma, Claude
  Design, Stitch, v0, Pencil, React Native** (it consumes the importer's
  `--emit js` bundle or `--emit ir-json`, not the design tool directly). Pulp's
  layout is **flex + grid only**, so CSS block/float/table/multi-column designs
  are out of scope by design.
- **GPU or CPU?** GPU by default (Dawn/Metal + Skia Graphite); CPU raster
  fallback when the GPU stack is absent. The example plugin renders on GPU.
- **JS engine?** Only on the high-fidelity bundle path (Pulp's QuickJS scripted
  UI — that's what makes it pixel-match the importer). The lightweight DesignIR
  path uses native widgets, no JS.
- **Skia/Dawn or just C++?** Your plugin statically links the Pulp SDK, which
  brings Skia + Dawn transitively (tens of MB) — but **no Pulp C++ type enters
  your iPlug2 translation units**; you include only `pulp_view_embed.h` (C).
- **Changing the UX later?** Re-run the importer and ship a new bundle (no C++
  edits); bind controls to iPlug2 `IParam`s by string key via the ABI v3 param
  bridge to make them interactive.
- **Iterating fast (hot-reload)?** Launch with `PULP_EMBED_HOT_RELOAD=1` and edit
  the bundle's `ui.js` — the open editor live-reloads (values preserved), no
  re-import. Off by default so it never ships in a release. Use absolute asset
  paths (importer default) for the dev loop. See the core
  [Editing & hot-reload](https://github.com/danielraffel/pulp-view-embed#editing--hot-reload-the-dev-loop--no-re-import-per-tweak) guide.

Full architecture + supported-imports table + roadmap:
[`pulp-view-embed` README](https://github.com/danielraffel/pulp-view-embed#what-you-actually-get-plain-english-faq).

## Hot reload (dev loop)

Tweak the design while the plugin/app editor is open — no re-import, no
recompile. **Off by default** (so it never ships in a release):

1. Launch with the dev flag: `PULP_EMBED_HOT_RELOAD=1 open PulpEmbedIPlug.app`
   (for a plugin, set it in the environment your DAW inherits).
2. Open the editor so the embedded design is visible.
3. Edit the bundle's `ui.js` (or `theme.json`) and save.
4. The editor live-reloads within a frame or two, **preserving knob/control
   values** — Pulp's `ScriptedUiSession` hot-reloader, pumped by the embed's
   per-tick `tick()`/`poll()`.

`PulpEmbedEditor` does this automatically: when `PULP_EMBED_HOT_RELOAD` is set it
arms a **debounced file-watcher** on the bundle's `ui.js` (polled from `tick()`)
that calls `pulp_embed_reload_bundle` on change — just save, no manual step.
Force it on/off with `editor.enableBundleHotReload(true|false)`. Leave it off in
release builds.

Use the importer's default **absolute** asset paths for the dev loop (a
portabilized relative bundle resolves assets through the production wrapper,
which the watcher can't see). Full guide:
[Editing & hot-reload](https://github.com/danielraffel/pulp-view-embed#editing--hot-reload-the-dev-loop--no-re-import-per-tweak).

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

## Interactive parameters

Bind the embedded design's controls to your plugin's `IParam`s so a dragged knob
writes the host parameter (with begin/end gesture grouping) and host automation /
preset recall moves the control. Construct the editor with the plugin delegate and
a design-key → param-index map:

```cpp
#include "PulpEmbedEditor.h"

enum EParams { kGain = 0, kMix, kNumParams };

// in your plugin ctor (iplug::Plugin IS an IEditorDelegate):
GetParam(kGain)->InitDouble("Gain", 80.0, 0.0, 100.0, 0.01, "%");
GetParam(kMix)->InitDouble("Dry/Wet", 100.0, 0.0, 100.0, 0.01, "%");

// when you create the editor (e.g. in OpenWindow), pass *this + the key map:
mEmbed = std::make_unique<pulp_iplug2::PulpEmbedEditor>(
    designPath, kW, kH, *this,
    /* design key -> paramIdx */ { {"gain", kGain}, {"mix", kMix} });
// mEmbed->boundParameterCount() -> how many design controls resolved (here up to 2).
```

The bind key is the design control's `pulpParamKey` (else its widget id). Pass
`nameFallback=true` (the default) to also match a design key against
`IParam::GetName()` when it has no explicit map entry; explicit entries always win,
and an explicit index past `NParams()` is ignored (never fires on an invalid param).
Controls with no match stay visual-only. The host→UI direction is pumped from
`tick()`, so keep calling `editor.tick()` from your UI timer (you already do). The
bundled figma demo is visual-only (its control keys don't match any param) → binds
0; point `PulpEmbedEditor` at a param-keyed design to see a non-zero bind count.

> Internals: the binding is **framework-neutral** — `PulpEmbedEditor.h` names no
> iPlug2 type, so the compile/link check still builds it without the iPlug2 SDK.
> The delegate is reached through a type-erased, duck-typed bridge that is only
> instantiated when this constructor is used inside an iPlug2 translation unit.

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
