# pulp-embed-iplug2

The **adapter library** (`PulpEmbedEditor`) that embeds a Pulp-imported design
(e.g. a Figma frame) in an [iPlug2](https://github.com/iPlug2/iPlug2) plugin's
editor. It's an iPlug2 wrapper over
[`pulp-view-embed`](https://github.com/danielraffel/pulp-view-embed). The importer
and a future new-plugin template both depend on this adapter — **this is the
bridge you extend or study**, not a starting point for your own plugin.

## Which repo do I want?

Three iPlug2 pieces, and they are **not interchangeable**:

| I want to… | Repo |
|---|---|
| **Import an existing iPlug2 plugin's UI** — automated: a Pulp UI over your *unchanged* iPlug2 DSP (`--emit hybrid-ui`) | [`pulp-import-iplug`](https://github.com/danielraffel/pulp-import-iplug) |
| **Start a NEW plugin from scratch** with a hand-built Pulp UI | a dedicated iPlug2 template (not yet published — until then, start from `example/` here) |
| **Extend / understand the bridge** both of the above depend on | **`pulp-embed-iplug2` — the adapter library (this repo)** |

**Most common mix-up:** to bring an **existing** plugin's UI across, use
[`pulp-import-iplug`](https://github.com/danielraffel/pulp-import-iplug), not this
adapter directly. Canonical map: the Pulp SDK guide
[**Putting a Pulp UI in a JUCE plugin**](https://github.com/Generous-Corp/pulp/blob/main/docs/guides/juce-embed.md)
(its iPlug2 note covers this adapter/importer pair).

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

### Greenfield: build IParams from the design

A new/thin plugin can let the design DEFINE its parameters instead of declaring
them by hand. `editor.designParams()` returns one descriptor per bindable control
(key, widget kind, discreteness, option count, default, and name/unit once the
importer carries them — ABI v5 metadata). Iterate it to init your `IParam`s and
build the key→index map in one pass:

```cpp
std::vector<std::pair<const char*, int>> keyMap;
int idx = 0;
for (auto& p : editor.designParams()) {
    const char* nm = p.name.empty() ? p.key.c_str() : p.name.c_str();
    if (p.is_discrete)
        GetParam(idx)->InitEnum(nm, 0, std::max(2, p.option_count));
    else
        GetParam(idx)->InitDouble(nm, p.default_norm * 100.0, 0.0, 100.0, 0.01, p.unit.c_str());
    keyMap.push_back({p.key.c_str(), idx++});
}
// then construct the editor's interactive ctor with keyMap.
```

Ranges are normalized [0,1] today; real units/ranges arrive with the importer
metadata slice. The plugin still owns the `IParam` objects (the host stays
authoritative); for an EXISTING plugin keep declaring params by hand and just map
keys, with `designParams()` as a cross-check.

## Value read-outs & host actions (ABI v8)

Three runtime accessors let the embedded design ask the plugin questions and fire
non-parameter actions. They are wired only when both this header and the linked
`pulp_view_embed` library carry the v8 callback tail; pre-v8 they degrade
gracefully (`has_param` falls back to the `-1.0 get_param` sentinel, display/
action are simply unavailable). `PulpEmbedEditor` sets
`desc.abi_version = min(PULP_VIEW_EMBED_ABI_VERSION, pulp_embed_abi_version())`,
so a header built ahead of the shim (or vice versa) never asserts a capability
neither side agrees on.

```cpp
// has_param — does a design key drive a host param? (belt-and-suspenders behind
// the get_param sentinel; unresolved keys are logged once):
editor.hasParam("cutoff");   // -> true once bound

// param_display_text — a formatted value read-out ("-6.0 dB"), memoized per
// (key, value). Supply the formatter; its body calls iPlug2 IParam::GetDisplay
// (with a WDL_String) in YOUR translation unit, keeping this header SDK-free:
editor.setParamDisplayFormatter([this](const std::string& key, double norm) {
  WDL_String s;
  GetParam(mKeyToIdx.at(key))->GetDisplayForHost(norm, /*normalized*/ true, s);
  return std::string(s.Get());
});

// host_action — an out-of-band UI action (a settings button, an "open manual"
// link) that isn't a parameter. The handler parses args_json and returns
// non-zero if it handled the action:
editor.setHostActionHandler([](const std::string& action, const std::string& args_json) {
  if (action == "open_manual") { /* ... */ return 1; }
  return 0;
});
```

## Resize

Unlike JUCE — where an `AudioProcessorEditor` drives its own bounds and the
adapter's `resized()` forwards them — an **iPlug2 plugin constrains its editor
window itself**: the initial size is `PLUG_WIDTH`/`PLUG_HEIGHT` (`config.h`) and
resizes flow through the plugin's `OnParentWindowResize` / a host-driven
`ConstrainEditorResize`. The imported design's own constraints live in the
materialized view (`pulp_embed_size_hints`). `PulpEmbedEditor` exposes them as
the idiomatic iPlug2 seam:

```cpp
// ctor: seed the initial editor size from the design's preferred size.
int w = kW, h = kH;
editor.preferredSize(w, h);   // design pref, or the ctor logical size
SetEditorSize(w, h);

// OpenWindow: size-on-open honoring aspect/min/max.
editor.applyPreferredSizeOnOpen();

// OnParentWindowResize: clamp the host-requested size to the design's
// aspect/min/max before applying it.
void OnParentWindowResize(int width, int height) override {
  editor.constrainSize(width, height);   // enforce min/max, lock aspect
  editor.resize(width, height, 1.0f);
}

editor.isResizable();   // 1 iff the design declares itself resizable
```

`constrainSize` treats width as the primary drag axis: with an aspect lock it
derives the height from the width, then re-clamps so the lock never violates
min/max.

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

### Native-view plugin example

`examples/native-view-plugin/` is a second real plugin (APP + VST3 + AU + CLAP)
whose editor is a **hand-built compiled Pulp `View`** (a `DesignFrameView` with
two `param_key`'d knobs) mounted via `PulpEmbedEditor`'s **native-view factory**
ctor — not an importer bundle. It exercises the full adapter surface in a real
target: native-view parameter binding, the ABI v8 value read-outs + host
actions, and the resize recipe. It needs an iPlug2 checkout + Pulp SDK to
build; its scaffold is validated headlessly by the `native-view-plugin-scaffold`
ctest (file-content assertions). See `examples/native-view-plugin/README.md`.

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
