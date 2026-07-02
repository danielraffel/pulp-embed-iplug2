# native-view-plugin — real iPlug2 plugin over a hand-built compiled Pulp View

A real iPlug2 plugin (**APP + VST3 + AU + CLAP**) whose editor is **not** an
imported design bundle but a **hand-built compiled Pulp `View`** — a
`DesignFrameView` with two `param_key`'d knobs — mounted over the plugin through
the `pulp_view_embed` C ABI via `PulpEmbedEditor`'s **native-view factory**
constructor.

This is the companion to `example/` (which embeds an importer JS bundle at full
fidelity). It exists to exercise, in a real plugin target, the adapter surface
the headless `examples/binding-test/` harness proves with a fake delegate:

- **Native-view binding** — `PulpEmbedEditor(NativeViewFactory, w, h, *this,
  {{"gain", kGain}, {"cutoff", kCutoff}})` binds the compiled view's knobs to
  the plugin's `IParam`s bidirectionally (drag → host param; automation/preset →
  knob), through the same `pulp_embed_create_from_view` substrate.
- **ABI v8 value read-outs (`param_display_text`)** — `setParamDisplayFormatter`
  routes the design's value chrome through iPlug2's own
  `IParam::GetDisplayForHost` (the `WDL_String` call lives in
  `NativeViewPlugin.mm`, never in the framework-neutral header).
- **ABI v8 host actions (`host_action`)** — `setHostActionHandler` handles a
  non-parameter UI action (e.g. an "open manual" link) without minting a fake
  param.
- **P3 resize recipe** — `preferredSize()`/`applyPreferredSizeOnOpen()` seeds the
  editor size from the design's `pulp_embed_size_hints`, and `constrainSize()` in
  `OnParentWindowResize` clamps host-driven resizes to the design's
  aspect/min/max — the idiomatic iPlug2 `ConstrainEditorResize` seam.

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DIPLUG2_DIR=/path/to/iPlug2 \
  -DPULP_VIEW_EMBED_DIR=/path/to/pulp-view-embed \
  -DCMAKE_PREFIX_PATH=/path/to/pulp-sdk-install
cmake --build build -j
```

### Verify the editor renders (headless self-check)

```bash
PULP_EMBED_SELFCHECK=1 build/out/PulpNativeViewIPlug.app/Contents/MacOS/PulpNativeViewIPlug
# → SELFCHECK gpu=1 bound=2 liveCapture=ok deterministic=ok
# writes /tmp/iplug-nativeview-live.png + /tmp/iplug-nativeview-render.png
```

`bound=2` proves both `param_key`'d knobs resolved to `kGain` / `kCutoff`.

## Resources note (build gap)

iPlug2's `iplug_add_plugin` locates per-format `Info.plist` templates + the
standalone `MainMenu.xib` / `main.rc*` files by **product name** convention.
This scaffold ships `resources/resource.h` (shared id table); for a real build,
copy the four `*-Info.plist` templates + `*-macOS-MainMenu.xib` + the `main.rc*`
files from `../../example/resources/` and rename their `PulpEmbedIPlug` prefix to
`PulpNativeViewIPlug` (and bump the AU subtype fourcc so it does not collide with
`example/`'s `Pemb` — this example uses `Pnv1`, see `config.h`). This example was
**not** built here (no iPlug2 checkout / Pulp SDK install in this environment);
the sources, CMake, and wiring are a faithful scaffold validated by the
`native-view-plugin-scaffold` ctest (file-content assertions), not a compiled
run.
