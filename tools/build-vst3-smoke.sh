#!/usr/bin/env bash
# Build the headless VST3 module-load smoke (tools/vst3_load_smoke.mm) against
# the iPlug2-vendored Steinberg VST3 SDK. Produces /tmp/vst3_load_smoke.
#
# Usage: tools/build-vst3-smoke.sh [VST3_SDK_DIR] [OUT]
set -euo pipefail

SDK="${1:-/Volumes/Workshop/Code/iPlug2/Dependencies/IPlug/VST3_SDK}"
OUT="${2:-/tmp/vst3_load_smoke}"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

if [[ ! -f "$SDK/public.sdk/source/vst/hosting/module.h" ]]; then
  echo "VST3 SDK not found at $SDK" >&2
  exit 1
fi

clang++ -std=c++17 -DRELEASE=1 -DNDEBUG -fobjc-arc \
  -I"$SDK" \
  "$HERE/vst3_load_smoke.mm" \
  "$SDK/public.sdk/source/vst/hosting/module.cpp" \
  "$SDK/public.sdk/source/vst/hosting/module_mac.mm" \
  "$SDK/public.sdk/source/vst/hosting/hostclasses.cpp" \
  "$SDK/public.sdk/source/vst/hosting/pluginterfacesupport.cpp" \
  "$SDK/public.sdk/source/vst/vstinitiids.cpp" \
  "$SDK/public.sdk/source/common/commonstringconvert.cpp" \
  "$SDK/public.sdk/source/vst/utility/stringconvert.cpp" \
  "$SDK/pluginterfaces/base/funknown.cpp" \
  "$SDK/pluginterfaces/base/coreiids.cpp" \
  "$SDK/pluginterfaces/base/conststringtable.cpp" \
  "$SDK/pluginterfaces/base/ustring.cpp" \
  "$SDK/base/source/fstring.cpp" \
  "$SDK/base/source/fobject.cpp" \
  "$SDK/base/source/baseiids.cpp" \
  "$SDK/base/source/fdebug.cpp" \
  "$SDK/base/thread/source/flock.cpp" \
  -framework Foundation -framework CoreFoundation \
  -o "$OUT"

echo "built $OUT"
