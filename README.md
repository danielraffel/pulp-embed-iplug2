# pulp-embed-iplug2

An [iPlug2](https://github.com/iPlug2/iPlug2) adapter for
[`pulp-view-embed`](../pulp-view-embed): embed a Pulp-imported design (e.g. a
Figma frame) in an iPlug2 plugin's editor.

> Status: **experiment**. The adapter helper (`include/PulpEmbedEditor.h`) is
> framework-neutral — it drives the flat C ABI and takes the parent native
> window as a `void*`, so it compiles and links against the C ABI **without** the
> iPlug2 SDK. The compile/link check (`ctest`) verifies that. Wiring it into a
> real iPlug2 plugin is below; full DAW validation is the remaining step.

## How it embeds

iPlug2 hands the editor a parent native window/view on open. `PulpEmbedEditor`
uses **pulp-parents mode**: `pulp_embed_attach(view, parent)` adds Pulp's child
into that parent and fires the view-opened lifecycle.

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

## Standalone example

`example/` is a real iPlug2 `IPlugAPP` standalone (UI NONE) that embeds the
"VST Style" Figma fixture at full fidelity:

```bash
cmake -S example -B example/build -DCMAKE_BUILD_TYPE=Release \
  -DIPLUG2_DIR=/path/to/iPlug2 \
  -DPULP_VIEW_EMBED_DIR=/path/to/pulp-view-embed \
  -DCMAKE_PREFIX_PATH=/path/to/pulp-sdk-install
cmake --build example/build --target PulpEmbedIPlug-app -j
open "example/build/out/PulpEmbedIPlug.app"
```

iPlug2 must have its prebuilt deps (`Dependencies/download-prebuilt-libs.sh`).
`PulpEmbedEditor(source, w, h)` auto-detects a `ui.js` bundle dir (high-fidelity
scripted-UI path) vs a `.json` DesignIR file (lightweight native path).

## Remaining work

- Wire into a concrete iPlug2 example plugin project (IGraphics editor open path)
  and validate in a DAW + the iPlug2 standalone.
- DPI / backing-scale change handling on the iPlug2 editor resize path.

## License

MIT.
