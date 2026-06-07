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
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

namespace pulp_iplug2 {

class PulpEmbedEditor {
public:
    // source is either an importer JS bundle directory (high-fidelity
    // scripted-UI path; contains ui.js) or a DesignIR JSON file (lightweight
    // native path) — auto-detected.
    PulpEmbedEditor(std::string source, int logicalWidth, int logicalHeight)
        : path_(std::move(source)), w_(logicalWidth), h_(logicalHeight) {
        PulpEmbedDesc d{};
        d.struct_size = sizeof(PulpEmbedDesc);
        d.abi_version = PULP_VIEW_EMBED_ABI_VERSION;
        d.logical_width = w_;
        d.logical_height = h_;
        d.scale_factor = 1.0f;
        d.backend_pref = PULP_EMBED_BACKEND_PREF_AUTO;
        d.design_width = w_;
        d.design_height = h_;
        std::error_code ec;
        const bool is_bundle =
            std::filesystem::is_directory(path_, ec) ||
            std::filesystem::exists(std::filesystem::path(path_) / "ui.js", ec);
        if (is_bundle)
            pulp_embed_create_from_ui_bundle(&d, path_.c_str(), &view_);
        else
            pulp_embed_create_from_design_json(&d, path_.c_str(), &view_);

        // Dev hot-reload: for a bundle, remember its ui.js and auto-enable the
        // watcher when PULP_EMBED_HOT_RELOAD is set in the environment.
        if (is_bundle) {
            const std::filesystem::path p(path_);
            watch_file_ = std::filesystem::is_directory(p, ec) ? (p / "ui.js") : p;
            if (std::getenv("PULP_EMBED_HOT_RELOAD") != nullptr)
                enableBundleHotReload(true);
        }
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

    // Pulp's child native view (NSView*). iPlug2's OpenWindow returns this so
    // the host can track/resize the editor view.
    void* nativeHandle() const { return view_ ? pulp_embed_native_handle(view_) : nullptr; }

    // Host-parents mode (B): the host (e.g. AUv2's Cocoa view factory) parents
    // Pulp's child NSView itself. Once that child is in a live window hierarchy,
    // call this to fire Pulp's view-opened lifecycle. Returns true once attached;
    // poll from tick() until it succeeds (the host may parent a frame later).
    bool notifyAttached() {
        return view_ && pulp_embed_notify_attached(view_) == PULP_EMBED_OK;
    }

    // Verification helpers. writeCapturePng grabs the LIVE GPU back buffer (the
    // on-screen surface); writeRenderPng is the deterministic Skia raster.
    bool writeCapturePng(const char* path) {
        if (!view_) return false;
        size_t n = 0;
        if (pulp_embed_capture_png(view_, nullptr, 0, &n) != PULP_EMBED_OK || !n) return false;
        std::vector<uint8_t> png(n);
        if (pulp_embed_capture_png(view_, png.data(), png.size(), &n) != PULP_EMBED_OK) return false;
        return write_file(path, png);
    }
    bool writeRenderPng(const char* path, int w, int h) {
        if (!view_) return false;
        size_t n = 0;
        if (pulp_embed_render_png(view_, w, h, 1.0f, nullptr, 0, &n) != PULP_EMBED_OK || !n) return false;
        std::vector<uint8_t> png(n);
        if (pulp_embed_render_png(view_, w, h, 1.0f, png.data(), png.size(), &n) != PULP_EMBED_OK) return false;
        return write_file(path, png);
    }

    void close() { if (view_) pulp_embed_detach(view_); }

    void resize(int w, int h, float scale) {
        if (view_) pulp_embed_resize(view_, w, h, scale > 0.0f ? scale : 1.0f);
    }

    void tick() {
        if (!view_) return;
        pulp_embed_tick(view_);
        pollHotReload();
    }

    // Dev hot-reload watcher: poll the bundle's ui.js mtime on tick() and call
    // pulp_embed_reload_bundle when it changes (debounced one tick vs a mid-write
    // save). Editing the bundle reloads the open editor live — no DAW reload.
    // Bundle path only; auto-enabled when PULP_EMBED_HOT_RELOAD is set. Ship off
    // in release builds (it's a developer loop).
    void enableBundleHotReload(bool enable = true) {
        std::error_code ec;
        watch_ = enable && !watch_file_.empty() &&
                 std::filesystem::exists(watch_file_, ec);
        if (watch_) last_write_ = pending_write_ = mtime();
    }

private:
    std::filesystem::file_time_type mtime() const {
        std::error_code ec;
        auto t = std::filesystem::last_write_time(watch_file_, ec);
        return ec ? std::filesystem::file_time_type{} : t;
    }
    void pollHotReload() {
        if (!watch_ || !view_) return;
        const auto m = mtime();
        if (m != last_write_) {
            // Apply only once the mtime has been stable for a tick (debounce);
            // reload_bundle is probe-first/last-good, so a bad edit is safe.
            if (m == pending_write_ &&
                pulp_embed_reload_bundle(view_, nullptr) == PULP_EMBED_OK)
                last_write_ = m;
            pending_write_ = m;
        }
    }

    static bool write_file(const char* path, const std::vector<uint8_t>& b) {
        if (b.empty()) return false;
        FILE* f = std::fopen(path, "wb");
        if (!f) return false;
        const bool ok = std::fwrite(b.data(), 1, b.size(), f) == b.size();
        std::fclose(f);
        return ok;
    }
    std::string path_;
    int w_ = 0, h_ = 0;
    PulpEmbedView* view_ = nullptr;

    // Dev hot-reload watcher state (bundle path only).
    bool watch_ = false;
    std::filesystem::path watch_file_;
    std::filesystem::file_time_type last_write_{};
    std::filesystem::file_time_type pending_write_{};
};

}  // namespace pulp_iplug2
