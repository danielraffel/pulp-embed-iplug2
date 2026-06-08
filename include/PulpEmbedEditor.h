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
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <initializer_list>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace pulp_iplug2 {

// ── host parameter bridge (design control <-> iPlug2 IParam) ────────────────
//
// This file stays FRAMEWORK-NEUTRAL: it never names an iPlug2 type, so the
// compile/link check builds it against the flat C ABI WITHOUT the iPlug2 SDK.
// The binding is reached only through the templated constructor below, whose
// body (and the iPlug2 method calls it makes) is not instantiated until an
// iPlug2 translation unit actually constructs an editor with a delegate. The
// concrete bridge is type-erased behind HostBridgeBase so PulpEmbedEditor needs
// no template parameter.
//
// The delegate is duck-typed: any type D exposing the iPlug2 IEditorDelegate
// surface works —
//   int            D::NParams() const
//   IParamLike*    D::GetParam(int idx)            // ->GetNormalized(), ->GetName()
//   void           D::BeginInformHostOfParamChangeFromUI(int idx)
//   void           D::SendParameterValueFromUI(int idx, double normalized)
//   void           D::EndInformHostOfParamChangeFromUI(int idx)
// (iplug::Plugin / IPlugAPIBase satisfy this.)
struct HostBridgeBase {
    virtual ~HostBridgeBase() = default;

    // Resolution state, framework-independent. `explicit_map` is the caller's
    // design-key -> paramIdx table; `bound` is the resolved subset (only keys
    // that exist in BOTH the design and the delegate); `last_pushed` tracks the
    // last value pushed host->UI per bound entry (for the 30 Hz pump).
    std::unordered_map<std::string, int> explicit_map;
    std::vector<std::pair<std::string, int>> bound;  // design key -> paramIdx
    std::vector<double> last_pushed;
    bool name_fallback = true;

    int find_idx(const char* key) const {
        for (const auto& b : bound)
            if (b.first == key) return b.second;
        return -1;
    }

    // Framework-specific trampolines (concrete in HostBridgeImpl<D>).
    virtual int n_params() = 0;
    virtual const char* param_name(int idx) = 0;
    virtual double param_norm(int idx) = 0;
    virtual void set_param(int idx, double normalized) = 0;
    virtual void begin_gesture(int idx) = 0;
    virtual void end_gesture(int idx) = 0;

    // ── flat-C host callbacks (ctx == this). Keyed by design string key. ────
    static void cSet(void* ctx, const char* key, double norm) {
        auto* b = static_cast<HostBridgeBase*>(ctx);
        const int i = b->find_idx(key);
        if (i >= 0) b->set_param(i, norm);
    }
    static double cGet(void* ctx, const char* key) {
        auto* b = static_cast<HostBridgeBase*>(ctx);
        const int i = b->find_idx(key);
        // Out-of-range sentinel for an unbound key: the embed seeds EVERY bindable
        // control via get_param during creation (before resolveBindings populates
        // `bound`), and treats any value in [0,1] as an authoritative host seed.
        // Returning a negative tells it "host has no opinion" so an unbound control
        // keeps its imported default instead of being forced to 0. Bound controls
        // get their real initial value pushed by resolveBindings right after create.
        return i >= 0 ? b->param_norm(i) : -1.0;
    }
    static void cBegin(void* ctx, const char* key) {
        auto* b = static_cast<HostBridgeBase*>(ctx);
        const int i = b->find_idx(key);
        if (i >= 0) b->begin_gesture(i);
    }
    static void cEnd(void* ctx, const char* key) {
        auto* b = static_cast<HostBridgeBase*>(ctx);
        const int i = b->find_idx(key);
        if (i >= 0) b->end_gesture(i);
    }
};

template <class D>
struct HostBridgeImpl final : HostBridgeBase {
    explicit HostBridgeImpl(D& d) : delegate(d) {}
    D& delegate;
    int n_params() override { return delegate.NParams(); }
    const char* param_name(int idx) override {
        auto* p = delegate.GetParam(idx);
        return p ? p->GetName() : "";
    }
    double param_norm(int idx) override {
        auto* p = delegate.GetParam(idx);
        return p ? p->GetNormalized() : 0.0;
    }
    void set_param(int idx, double n) override {
        delegate.SendParameterValueFromUI(idx, n);
    }
    void begin_gesture(int idx) override {
        delegate.BeginInformHostOfParamChangeFromUI(idx);
    }
    void end_gesture(int idx) override {
        delegate.EndInformHostOfParamChangeFromUI(idx);
    }
};

class PulpEmbedEditor {
public:
    // source is either an importer JS bundle directory (high-fidelity
    // scripted-UI path; contains ui.js) or a DesignIR JSON file (lightweight
    // native path) — auto-detected. Visual-only: design controls render but no
    // host parameter is driven.
    PulpEmbedEditor(std::string source, int logicalWidth, int logicalHeight)
        : path_(std::move(source)), w_(logicalWidth), h_(logicalHeight) {
        createView();
    }

    // Interactive: bind the design's controls to an iPlug2 delegate's IParams.
    // `delegate` is the plugin (iplug::Plugin / IEditorDelegate); it must
    // outlive this editor. `keyToParamIdx` maps a design control key (its
    // pulpParamKey, else its widget id) to an iPlug2 paramIdx (e.g. kGain).
    // Unlike JUCE (string paramID), iPlug2 params are integer-indexed, so the
    // plugin supplies this map explicitly. When `nameFallback` is true, a design
    // key with no explicit entry is matched (case-insensitively) against
    // IParam::GetName(); explicit entries always win. Controls that resolve to a
    // param bind bidirectionally — a dragged knob fires
    // Begin/SendParameterValueFromUI/End; host automation & preset recall push
    // back into the control on the 30 Hz tick. Unresolved controls stay
    // visual-only. boundParameterCount() reports how many resolved.
    template <class Delegate>
    PulpEmbedEditor(std::string source, int logicalWidth, int logicalHeight,
                    Delegate& delegate,
                    std::initializer_list<std::pair<const char*, int>> keyToParamIdx,
                    bool nameFallback = true)
        : path_(std::move(source)), w_(logicalWidth), h_(logicalHeight) {
        auto impl = std::make_unique<HostBridgeImpl<Delegate>>(delegate);
        impl->name_fallback = nameFallback;
        for (const auto& kv : keyToParamIdx)
            impl->explicit_map.emplace(kv.first, kv.second);
        bridge_ = std::move(impl);
        createView();
        resolveBindings();
    }

    ~PulpEmbedEditor() {
        // Destroy the view (and any in-flight host callbacks) before bridge_,
        // which is freed after this body — so host_ctx stays valid for the view.
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

    // Non-owning handle to the embedded view, for advanced hosts and headless
    // test harnesses that call the flat C ABI directly (e.g.
    // pulp_embed_param_count / pulp_embed_simulate_param_drag). Owned by this
    // editor — do NOT destroy it. NULL before creation succeeds.
    PulpEmbedView* view() const { return view_; }

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
        pumpHostToUi();
        pollHotReload();
    }

    // Count of design controls that resolved to an iPlug2 param (0 when
    // constructed without a delegate, or when no design key matched). Handy for
    // self-checks / "is the bridge live?".
    int boundParameterCount() const {
        return bridge_ ? static_cast<int>(bridge_->bound.size()) : 0;
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
    // Shared construction body: build the desc (wiring the host bridge when
    // bridge_ is set — the callbacks must be in the desc at create time, since
    // host_ctx is captured then), create the view from a bundle dir or DesignIR
    // JSON, and arm the dev hot-reload watcher for a bundle.
    void createView() {
        PulpEmbedDesc d{};
        d.struct_size = sizeof(PulpEmbedDesc);
        d.abi_version = PULP_VIEW_EMBED_ABI_VERSION;
        d.logical_width = w_;
        d.logical_height = h_;
        d.scale_factor = 1.0f;
        d.backend_pref = PULP_EMBED_BACKEND_PREF_AUTO;
        d.design_width = w_;
        d.design_height = h_;
        if (bridge_) {
            d.host_ctx = bridge_.get();
            d.host.set_param = &HostBridgeBase::cSet;
            d.host.get_param = &HostBridgeBase::cGet;
            d.host.begin_gesture = &HostBridgeBase::cBegin;
            d.host.end_gesture = &HostBridgeBase::cEnd;
        }
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

    // Map each design control key -> an iPlug2 paramIdx and push initial values
    // host->UI so controls reflect the current state on open. Explicit map wins;
    // an unmatched key falls back to a case-insensitive IParam::GetName() match
    // when enabled. No-op without a bridge or view.
    void resolveBindings() {
        if (!bridge_ || !view_) return;
        const int nparams = bridge_->n_params();
        const int n = pulp_embed_param_count(view_);
        for (int i = 0; i < n; ++i) {
            char key[256] = {0};
            pulp_embed_param_key(view_, i, key, sizeof key);
            int idx = -1;
            const auto it = bridge_->explicit_map.find(key);
            if (it != bridge_->explicit_map.end())
                idx = it->second;
            else if (bridge_->name_fallback)
                idx = matchByName(key);
            // Reject an unmatched key (visual-only, never guessed) OR an explicit
            // map entry pointing past the delegate's param count (a stale enum) —
            // binding it would later fire Begin/Send/End on an invalid paramIdx.
            if (idx < 0 || idx >= nparams) continue;
            bridge_->bound.emplace_back(key, idx);
        }
        bridge_->last_pushed.assign(bridge_->bound.size(), -1.0);
        for (size_t i = 0; i < bridge_->bound.size(); ++i) {
            const double v = bridge_->param_norm(bridge_->bound[i].second);
            pulp_embed_param_changed(view_, bridge_->bound[i].first.c_str(), v);
            bridge_->last_pushed[i] = v;
        }
    }

    // Case-insensitive match of a design key against IParam::GetName(); -1 if
    // none. Only consulted when no explicit map entry exists.
    int matchByName(const char* key) const {
        const int n = bridge_->n_params();
        for (int i = 0; i < n; ++i)
            if (iequals(key, bridge_->param_name(i))) return i;
        return -1;
    }
    static bool iequals(const char* a, const char* b) {
        if (a == nullptr || b == nullptr) return false;
        for (; *a && *b; ++a, ++b)
            if (std::tolower((unsigned char) *a) != std::tolower((unsigned char) *b))
                return false;
        return *a == '\0' && *b == '\0';
    }

    // Push host-side parameter changes (automation / preset recall / a sibling
    // editor) into the matching controls. Cheap float compares per bound param
    // at 30 Hz. pulp_embed_param_changed does NOT re-enter set_param, so there is
    // no feedback loop. No-op without a bridge.
    void pumpHostToUi() {
        if (!bridge_ || !view_) return;
        for (size_t i = 0; i < bridge_->bound.size(); ++i) {
            const double v = bridge_->param_norm(bridge_->bound[i].second);
            if (v != bridge_->last_pushed[i]) {
                pulp_embed_param_changed(view_, bridge_->bound[i].first.c_str(), v);
                bridge_->last_pushed[i] = v;
            }
        }
    }

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

    // Host parameter bridge (null = visual-only). Type-erased so this class is
    // not templated. The destructor body runs pulp_embed_destroy(view_) BEFORE
    // any member is destroyed, so host_ctx (== bridge_.get()) stays valid for any
    // in-flight callback regardless of member declaration order.
    std::unique_ptr<HostBridgeBase> bridge_;

    // Dev hot-reload watcher state (bundle path only).
    bool watch_ = false;
    std::filesystem::path watch_file_;
    std::filesystem::file_time_type last_write_{};
    std::filesystem::file_time_type pending_write_{};
};

}  // namespace pulp_iplug2
