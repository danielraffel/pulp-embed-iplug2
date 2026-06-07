# Third-party notices — pulp-embed-iplug2

This repository is MIT-licensed (matching Pulp). It bundles, derives from, or
builds against third-party components whose licenses are noted here.

## iPlug2 (WDL license)

The `example/` plugin is a minimal [iPlug2](https://github.com/iPlug2/iPlug2)
plugin, built against a developer-supplied iPlug2 checkout (`-DIPLUG2_DIR=…`);
iPlug2 is **not** vendored in this repo.

The platform resource files under `example/resources/` are **derived from
iPlug2's `Examples/IPlugEffect` template**:

- `main.rc` — Win32 resource script (version info, menu, accelerators, the
  audio/MIDI preferences dialog, font include).
- `main.rc_mac_dlg` — SWELL macOS translation of the preferences dialog.
- `main.rc_mac_menu` — SWELL macOS menu.
- `resource.h` — resource id definitions.
- `PulpEmbedIPlug-*-Info.plist`, `PulpEmbedIPlug-macOS-MainMenu.xib` — bundle
  metadata / macOS app nib, derived from the corresponding `IPlugEffect-*`
  template files.

These were relabeled for PulpEmbedIPlug (company/copyright → Pulp, version
→ 0.1.0, bundle ids → `dev.pulp.*`) and trimmed (the IGraphics-only Debug menu
items — Live Edit / Show Bounds / Show Drawn / Show FPS / Screenshot — were
removed because this example builds `UI NONE`: the editor IS the embedded Pulp
native view, so there is no IGraphics surface to inspect). The audio/MIDI
preference ids are kept because iPlug2's standalone APP framework
(`IPlug/APP/IPlugAPP_main.cpp`, `IPlugAPP_dialog.cpp`) references them.

iPlug2 is distributed under the **WDL license** (a permissive, zlib-style
license — see `LICENSE.txt` in the iPlug2 distribution), which allows
modification and redistribution. Original iPlug2 copyright:

> iPlug 2 C++ Plug-in Framework. Copyright (C) the iPlug 2 Developers.
> Based on WDL-OL/iPlug by Oli Larkin (2011-2018), and the original iPlug v1
> (2008) by John Schwartz / Cockos.

## VST3 SDK / CLAP SDK / AudioUnit

The VST3, CLAP, and AudioUnit format wrappers are produced by iPlug2 against
the respective SDKs (Steinberg VST3 SDK, CLAP, Apple AudioUnit), supplied by the
developer's own iPlug2 checkout. They are not redistributed here. "VST" is a
trademark of Steinberg Media Technologies GmbH; "Audio Unit" is a trademark of
Apple, Inc.

## pulp_view_embed / Pulp SDK

The embedded UI is rendered by Pulp via the `pulp_view_embed` flat C ABI
(separate repo) linked against an installed Pulp SDK. Both are MIT-licensed.
