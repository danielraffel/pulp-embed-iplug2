// PulpEmbedEditor — drives the pulp_view_embed C ABI from an iPlug2 plugin's
// editor lifecycle. iPlug2 hands the editor a parent native window/view on open;
// we attach Pulp's child into it (pulp-parents mode), forward host resizes, tick
// on a timer, and tear down on close. No Pulp C++ type crosses in — only the
// flat C ABI header — so this compiles in an iPlug2 TU without Pulp's toolchain.
//
// Wiring into an iPlug2 plugin (sketch):
//   - construct PulpEmbedEditor(designIrPath, w, h) in your plugin ctor
//   - in your editor's platform open (e.g. IGraphics OpenWindow(void* pParent),
//     or a custom IEditorDelegate path) call editor.open(pParent)
//   - on host resize call editor.resize(w, h, scale)
//   - drive editor.tick() from your UI timer (~30 Hz)
//   - on editor close call editor.close()
#pragma once

#include <pulp_view_embed.h>
#include <string>

namespace pulp_iplug2 {

class PulpEmbedEditor {
public:
    PulpEmbedEditor(std::string designIrJsonPath, int logicalWidth, int logicalHeight)
        : path_(std::move(designIrJsonPath)), w_(logicalWidth), h_(logicalHeight) {
        PulpEmbedDesc d{};
        d.struct_size = sizeof(PulpEmbedDesc);
        d.abi_version = PULP_VIEW_EMBED_ABI_VERSION;
        d.logical_width = w_;
        d.logical_height = h_;
        d.scale_factor = 1.0f;
        d.backend_pref = PULP_EMBED_BACKEND_PREF_AUTO;
        d.design_width = w_;
        d.design_height = h_;
        pulp_embed_create_from_design_json(&d, path_.c_str(), &view_);
    }

    ~PulpEmbedEditor() {
        if (view_) { pulp_embed_destroy(view_); view_ = nullptr; }
    }

    PulpEmbedEditor(const PulpEmbedEditor&) = delete;
    PulpEmbedEditor& operator=(const PulpEmbedEditor&) = delete;

    bool valid() const { return view_ != nullptr; }
    bool isGpuBacked() const {
        return view_ && pulp_embed_active_backend(view_) == PULP_EMBED_BACKEND_GPU;
    }
    std::string lastError() const {
        char buf[512];
        if (view_) pulp_embed_last_error(view_, buf, sizeof(buf));
        else pulp_embed_last_create_error(buf, sizeof(buf));
        return buf;
    }

    // iPlug2 editor open: parentNativeWindow is the NSView*/HWND iPlug2 supplies.
    // Pulp parents its child into it and fires the view-opened lifecycle.
    bool open(void* parentNativeWindow) {
        if (!view_ || !parentNativeWindow) return false;
        return pulp_embed_attach(view_, parentNativeWindow) == PULP_EMBED_OK;
    }

    void close() { if (view_) pulp_embed_detach(view_); }

    void resize(int w, int h, float scale) {
        if (view_) pulp_embed_resize(view_, w, h, scale > 0.0f ? scale : 1.0f);
    }

    void tick() { if (view_) pulp_embed_tick(view_); }

private:
    std::string path_;
    int w_ = 0, h_ = 0;
    PulpEmbedView* view_ = nullptr;
};

}  // namespace pulp_iplug2
