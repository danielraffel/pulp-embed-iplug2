// Compile/link check for the iPlug2 adapter helper against the pulp_view_embed
// C ABI. PulpEmbedEditor is framework-neutral (parent passed as void*), so this
// verifies the helper is valid C++ and links the ABI WITHOUT the iPlug2 SDK.
// Full iPlug2-plugin wiring is documented in the README.
#include "PulpEmbedEditor.h"

#include <cstdio>

int main() {
    pulp_iplug2::PulpEmbedEditor editor("nonexistent.ir.json", 800, 480);
    // No fixture here — just exercise the ABI surface compiles + links.
    std::printf("valid=%d gpu=%d err=%s\n",
                editor.valid() ? 1 : 0,
                editor.isGpuBacked() ? 1 : 0,
                editor.lastError().c_str());
    editor.resize(820, 500, 2.0f);
    editor.tick();
    editor.close();
    std::printf("pulp-embed-iplug2 compilecheck OK\n");
    return 0;
}
