// vst3_load_smoke — a headless VST3 module-load smoke test for when pluginval
// is not installed. Uses the Steinberg VST3 SDK hosting API to load a .vst3
// bundle, walk its factory class infos, instantiate the audio component
// (IComponent), initialize + terminate it, and report. Exit 0 on success.
//
// Build (macOS): see tools/build-vst3-smoke.sh — it pulls the small set of SDK
// hosting sources needed and links Foundation/CoreFoundation.
// Run: /tmp/vst3_load_smoke path/to/Plugin.vst3
//
// This proves the module loads and the VST3 factory + component ABI is wired —
// it does NOT open the editor (that needs a host window) and does NOT render.

#include "public.sdk/source/vst/hosting/module.h"
#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"  // kVstAudioEffectClass

#include <cstdio>
#include <string>

using namespace Steinberg;

int main(int argc, char** argv) {
  if (argc < 2) {
    fprintf(stderr, "usage: %s path/to/Plugin.vst3\n", argv[0]);
    return 2;
  }
  const std::string path = argv[1];

  std::string err;
  auto module = VST3::Hosting::Module::create(path, err);
  if (!module) {
    fprintf(stderr, "FAIL: Module::create: %s\n", err.c_str());
    return 1;
  }
  printf("module loaded: %s\n", path.c_str());

  const auto& factory = module->getFactory();
  auto classInfos = factory.classInfos();
  printf("factory classes = %zu\n", classInfos.size());
  if (classInfos.empty()) {
    fprintf(stderr, "FAIL: factory has zero classes\n");
    return 1;
  }

  int components = 0;
  int failures = 0;
  for (const auto& ci : classInfos) {
    printf("  class: name=%s category=%s\n", ci.name().c_str(),
           ci.category().c_str());
    if (ci.category() != kVstAudioEffectClass)
      continue;  // skip controller-only classes; we instantiate the effect
    ++components;

    auto comp = factory.createInstance<Vst::IComponent>(ci.ID());
    if (!comp) {
      fprintf(stderr, "  FAIL: createInstance(IComponent) returned null\n");
      ++failures;
      continue;
    }
    if (comp->initialize(nullptr) != kResultOk) {
      fprintf(stderr, "  FAIL: IComponent::initialize failed\n");
      ++failures;
      continue;
    }
    // Query the controller class id (proves the single-component/edit-controller
    // wiring resolves) — best-effort, non-fatal if absent.
    TUID controllerId;
    if (comp->getControllerClassId(controllerId) == kResultOk)
      printf("  controller class id resolved\n");

    comp->terminate();
    printf("  IComponent initialize/terminate OK\n");
  }

  if (components == 0) {
    fprintf(stderr, "FAIL: no audio-effect class found in factory\n");
    return 1;
  }
  if (failures) {
    fprintf(stderr, "VST3 LOAD SMOKE FAILED (%d failures)\n", failures);
    return 1;
  }
  printf("VST3 LOAD SMOKE PASSED\n");
  return 0;
}
