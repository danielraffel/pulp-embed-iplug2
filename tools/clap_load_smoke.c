/* clap_load_smoke — a headless CLAP load smoke test for when clap-validator is
 * not installed. Loads a .clap bundle, runs the entry init, walks the plugin
 * factory, instantiates each plugin, calls init/activate/start_processing and
 * the matching teardown, then reports. Exit 0 on success, non-zero on failure.
 *
 * Build (macOS):
 *   cc -std=c11 -I<CLAP_SDK>/include tools/clap_load_smoke.c -o /tmp/clap_load_smoke
 * Run:
 *   /tmp/clap_load_smoke path/to/Plugin.clap
 *
 * This proves the module loads and the CLAP ABI surface is wired — it does NOT
 * open the GUI (that needs a host window) and does NOT render audio.
 */
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <clap/clap.h>

int main(int argc, char** argv) {
  if (argc < 2) {
    fprintf(stderr, "usage: %s path/to/Plugin.clap\n", argv[0]);
    return 2;
  }
  const char* bundle = argv[1];

  /* A .clap on macOS is a bundle dir; the binary lives in Contents/MacOS/<name>. */
  char binpath[2048];
  const char* base = strrchr(bundle, '/');
  base = base ? base + 1 : bundle;
  char name[512];
  strncpy(name, base, sizeof(name) - 1);
  name[sizeof(name) - 1] = '\0';
  char* dot = strstr(name, ".clap");
  if (dot) *dot = '\0';
  snprintf(binpath, sizeof(binpath), "%s/Contents/MacOS/%s", bundle, name);

  void* h = dlopen(binpath, RTLD_NOW | RTLD_LOCAL);
  if (!h) {
    /* Fall back to dlopen on the bundle path directly (flat module). */
    h = dlopen(bundle, RTLD_NOW | RTLD_LOCAL);
  }
  if (!h) {
    fprintf(stderr, "FAIL dlopen: %s\n", dlerror());
    return 1;
  }

  const clap_plugin_entry_t* entry =
      (const clap_plugin_entry_t*) dlsym(h, "clap_entry");
  if (!entry) {
    fprintf(stderr, "FAIL: no clap_entry symbol\n");
    return 1;
  }
  printf("clap_entry OK (clap_version %u.%u.%u)\n",
         entry->clap_version.major, entry->clap_version.minor,
         entry->clap_version.revision);

  if (!entry->init || !entry->init(bundle)) {
    fprintf(stderr, "FAIL: entry->init returned false\n");
    return 1;
  }

  const clap_plugin_factory_t* factory =
      (const clap_plugin_factory_t*) entry->get_factory(CLAP_PLUGIN_FACTORY_ID);
  if (!factory) {
    fprintf(stderr, "FAIL: no plugin factory\n");
    entry->deinit();
    return 1;
  }

  uint32_t count = factory->get_plugin_count(factory);
  printf("plugin_count = %u\n", count);
  if (count == 0) {
    fprintf(stderr, "FAIL: zero plugins in factory\n");
    entry->deinit();
    return 1;
  }

  /* Minimal stub host. */
  static clap_host_t host;
  host.clap_version = CLAP_VERSION;
  host.host_data = NULL;
  host.name = "clap_load_smoke";
  host.vendor = "Pulp";
  host.url = "https://github.com/danielraffel/pulp";
  host.version = "0.1.0";
  host.get_extension = NULL;
  host.request_restart = NULL;
  host.request_process = NULL;
  host.request_callback = NULL;

  int failures = 0;
  for (uint32_t i = 0; i < count; ++i) {
    const clap_plugin_descriptor_t* desc =
        factory->get_plugin_descriptor(factory, i);
    if (!desc) {
      fprintf(stderr, "FAIL: null descriptor at %u\n", i);
      ++failures;
      continue;
    }
    printf("  [%u] id=%s name=%s vendor=%s\n", i, desc->id, desc->name,
           desc->vendor ? desc->vendor : "");

    const clap_plugin_t* plug = factory->create_plugin(factory, &host, desc->id);
    if (!plug) {
      fprintf(stderr, "  FAIL: create_plugin(%s) returned NULL\n", desc->id);
      ++failures;
      continue;
    }
    if (!plug->init(plug)) {
      fprintf(stderr, "  FAIL: plugin->init returned false\n");
      plug->destroy(plug);
      ++failures;
      continue;
    }
    if (!plug->activate(plug, 48000.0, 32, 4096)) {
      fprintf(stderr, "  FAIL: plugin->activate returned false\n");
      plug->destroy(plug);
      ++failures;
      continue;
    }
    if (!plug->start_processing(plug)) {
      fprintf(stderr, "  FAIL: plugin->start_processing returned false\n");
      plug->deactivate(plug);
      plug->destroy(plug);
      ++failures;
      continue;
    }
    plug->stop_processing(plug);
    plug->deactivate(plug);
    plug->destroy(plug);
    printf("  [%u] init/activate/start/stop/deactivate/destroy OK\n", i);
  }

  entry->deinit();
  dlclose(h);

  if (failures) {
    fprintf(stderr, "CLAP LOAD SMOKE FAILED (%d failures)\n", failures);
    return 1;
  }
  printf("CLAP LOAD SMOKE PASSED\n");
  return 0;
}
