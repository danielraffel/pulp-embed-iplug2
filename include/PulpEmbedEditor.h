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
#include <pulp_view_embed.hpp>  // pulp::embed::ParamDesc + param_descs/read_design_params
#include <pulp_view_embed_native.hpp>  // pulp::embed::NativeViewFactory + create_from_view
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <functional>
#include <initializer_list>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
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

    // ── ABI v6 text-field string bridge (text_field <-> host STATE) ──────────
    // text_fields carry a string, not a normalized value, so they ride a
    // separate side-channel. `strings` is the plugin's authoritative string
    // store (preset name / label / search text) — what the plugin serializes;
    // `on_string` is an optional live-edit notification (e.g. mark-dirty). The
    // store is keyed by the text_field's design key. Unlike the numeric bridge,
    // strings need no delegate resolution — every design key is its own store
    // slot, so this works for any editor that wires the callbacks.
    std::unordered_map<std::string, std::string> strings;
    std::function<void(const std::string&, const std::string&)> on_string;

    static void cSetString(void* ctx, const char* key, const char* utf8) {
        auto* b = static_cast<HostBridgeBase*>(ctx);
        const std::string k = key ? key : "";
        const std::string v = utf8 ? utf8 : "";
        b->strings[k] = v;
        if (b->on_string) b->on_string(k, v);
    }
    static int32_t cGetString(void* ctx, const char* key, char* out, int32_t cap) {
        auto* b = static_cast<HostBridgeBase*>(ctx);
        const auto it = b->strings.find(key ? key : "");
        if (it == b->strings.end()) return -1;  // no host opinion: keep imported default
        const int32_t n = static_cast<int32_t>(it->second.size());
        if (out && cap > 0) {
            const int32_t c = n < cap - 1 ? n : cap - 1;
            std::memcpy(out, it->second.data(), static_cast<size_t>(c));
            out[c] = '\0';
        }
        return n;
    }

    // ── ABI v8 runtime accessors (has_param / param_display_text / host_action) ─
    //
    // These back the view->host query callbacks the embed uses to render live
    // parameter chrome (value read-outs, "does this control drive a host
    // param?" hit-tests) and to fire non-parameter UI actions (a settings-panel
    // button, an "open manual" link) without minting a fake param. All three
    // are keyed/named by the same design strings as the numeric bridge, share
    // the bridge's host_ctx, and run on the host UI thread.
    //
    // has_param — belt-and-suspenders behind the existing -1.0 get_param
    //   sentinel: `bound` is the resolved key set (explicit map + name
    //   fallback), so a key resolves iff it is in `bound`. Unresolved keys are
    //   logged ONCE (a design referencing a control the plugin never mapped is
    //   a wiring bug worth surfacing, but only once — the embed may query every
    //   frame).
    // param_display_text — a formatted, human-readable value string ("-6.0 dB",
    //   "440 Hz", "On"). The plugin supplies the formatter via
    //   setParamDisplayFormatter (its body calls iPlug2 IParam::GetDisplay with
    //   a WDL_String in the plugin TU — kept out of this framework-neutral
    //   header so the compile/link check stays SDK-free). Results are memoized
    //   per (key, quantized value) because the embed may ask for the same
    //   read-out every repaint.
    // host_action — an out-of-band UI action; the plugin's handler parses
    //   `args_json` and returns non-zero if it handled the action.
    std::function<std::string(const std::string& key, double normalized)> on_param_display;
    std::function<int(const std::string& action, const std::string& args_json)> on_host_action;
    std::unordered_map<std::string, std::string> display_memo;  // (key\x1fvalue) -> text
    std::unordered_set<std::string> logged_unresolved;          // has_param log-once

    bool has_param_impl(const char* key) {
        if (key && find_idx(key) >= 0) return true;
        const std::string k = key ? key : "";
        if (logged_unresolved.insert(k).second)
            std::fprintf(stderr,
                         "[pulp-embed-iplug2] has_param: design key \"%s\" resolves "
                         "to no host parameter (visual-only?)\n",
                         k.c_str());
        return false;
    }

    std::string param_display_impl(const char* key, double normalized) {
        const std::string k = key ? key : "";
        char vbuf[24];
        std::snprintf(vbuf, sizeof vbuf, "%.4f", normalized);
        const std::string mk = k + '\x1f' + vbuf;
        const auto it = display_memo.find(mk);
        if (it != display_memo.end()) return it->second;
        std::string out;
        if (on_param_display) {
            out = on_param_display(k, normalized);
        } else {
            // No plugin formatter: fall back to a normalized read-out so the
            // embed still shows *a* value instead of nothing.
            char b[24];
            std::snprintf(b, sizeof b, "%.2f", normalized);
            out = b;
        }
        display_memo.emplace(mk, out);
        return out;
    }

    static int32_t cHasParam(void* ctx, const char* key) {
        return static_cast<HostBridgeBase*>(ctx)->has_param_impl(key) ? 1 : 0;
    }
    static size_t cParamDisplayText(void* ctx, const char* key, double normalized,
                                    char* buf, size_t cap) {
        const std::string s =
            static_cast<HostBridgeBase*>(ctx)->param_display_impl(key, normalized);
        const size_t n = s.size();
        if (buf && cap > 0) {
            const size_t c = n < cap - 1 ? n : cap - 1;
            std::memcpy(buf, s.data(), c);
            buf[c] = '\0';
        }
        return n;
    }
    static int32_t cHostAction(void* ctx, const char* action, const char* args_json) {
        auto* b = static_cast<HostBridgeBase*>(ctx);
        return b->on_host_action ? b->on_host_action(action ? action : "",
                                                     args_json ? args_json : "")
                                 : 0;
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

    // Native-view, visual-only: mount a hand-built compiled View (e.g. a
    // DesignFrameView subclass) instead of an importer-generated design. The
    // factory builds the root tree on the Pulp side. No host parameter is driven.
    PulpEmbedEditor(pulp::embed::NativeViewFactory factory,
                    int logicalWidth, int logicalHeight)
        : w_(logicalWidth), h_(logicalHeight), factory_(std::move(factory)) {
        createView();
    }

    // Native-view, interactive: mount a compiled View and bind its DesignFrameView
    // elements' param_keys to iPlug2 IParams. Same keyToParamIdx contract as the
    // file-based bridge ctor — the design key is the element's param_key. The
    // binding path (resolveBindings, via pulp_embed_param_count/_key) is shared,
    // so a native view binds exactly like an imported one.
    template <class Delegate>
    PulpEmbedEditor(pulp::embed::NativeViewFactory factory,
                    int logicalWidth, int logicalHeight,
                    Delegate& delegate,
                    std::initializer_list<std::pair<const char*, int>> keyToParamIdx,
                    bool nameFallback = true)
        : w_(logicalWidth), h_(logicalHeight), factory_(std::move(factory)) {
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

    // One design control's parameter description (ABI v5 metadata), for a
    // GREENFIELD plugin that wants to BUILD its IParams from the design. Shared,
    // framework-neutral type from pulp_view_embed.hpp: { key, widget_kind,
    // is_discrete, option_count, default_norm, name, unit }. `name`/`unit` are
    // populated once the importer carries them (else empty — fall back to `key`).
    using DesignParamDesc = pulp::embed::ParamDesc;

    // Enumerate the design's bindable controls as parameter descriptors, in the
    // stable ABI index order. A greenfield plugin iterates these to init its
    // IParams, e.g.:
    //   int idx = 0;
    //   for (auto& p : editor.designParams()) {
    //     const char* nm = p.name.empty() ? p.key.c_str() : p.name.c_str();
    //     if (p.is_discrete) GetParam(idx)->InitEnum(nm, 0, std::max(2, p.option_count));
    //     else GetParam(idx)->InitDouble(nm, p.default_norm * 100.0, 0, 100, 0.01,
    //                                    p.unit.c_str());
    //     keyMap.push_back({p.key.c_str(), idx++});   // feed the binding ctor
    //   }
    // (Ranges are normalized [0,1] today; real units arrive with the importer
    // metadata slice. The plugin still owns the IParam objects.)
    std::vector<DesignParamDesc> designParams() const {
        return pulp::embed::param_descs(view_);
    }

    // Static greenfield entry point: read a design's parameter descriptors WITHOUT
    // an editor/window, so a plugin can declare its IParams at CONSTRUCTION time
    // (before any editor exists) directly from the design. Builds an OFFSCREEN
    // view, enumerates, tears it down. `source` is a bundle dir (ui.js) or a
    // DesignIR JSON file — auto-detected. Empty vector if it can't be opened.
    static std::vector<DesignParamDesc> readDesignParams(const std::string& source,
                                                         int logicalWidth,
                                                         int logicalHeight) {
        std::error_code ec;
        const bool is_bundle =
            std::filesystem::is_directory(source, ec) ||
            std::filesystem::exists(std::filesystem::path(source) / "ui.js", ec);
        return pulp::embed::read_design_params(source, is_bundle, logicalWidth, logicalHeight);
    }

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

    // ── text-field string state (ABI v6) ───────────────────────────────────
    // A design's text_field controls carry a UTF-8 string bound to the plugin's
    // OWN state (preset name / label / search text) — saved/restored with the
    // plugin, NOT a DAW-automatable parameter. These methods read/write that
    // state directly on the live view, so they work whether or not the editor
    // was given a numeric-param delegate. Use captureStringState() at
    // SerializeState time and restoreStringState() at UnserializeState time.

    // Number of bindable text_field string controls in the design.
    int stringFieldCount() const {
        return view_ ? pulp_embed_string_param_count(view_) : 0;
    }

    // The string control's design key at `index` (empty if out of range).
    std::string stringFieldKey(int index) const {
        char buf[256] = {0};
        if (view_) pulp_embed_string_param_key(view_, index, buf, sizeof buf);
        return buf;
    }

    // Current UTF-8 text of the string control identified by `key` (empty if the
    // key is unknown). Sized two-call so arbitrarily long values round-trip.
    std::string stringValue(const std::string& key) const {
        if (!view_) return {};
        const size_t need = pulp_embed_get_string(view_, key.c_str(), nullptr, 0);
        std::string out(need, '\0');
        if (need) pulp_embed_get_string(view_, key.c_str(), out.data(), need + 1);
        return out;
    }

    // Host -> view: set the text of the string control identified by `key`
    // (preset recall). A no-op (returns true) for a key that matches no
    // text_field, so a blind restore is safe.
    bool setStringValue(const std::string& key, const std::string& value) {
        return view_ &&
               pulp_embed_set_string(view_, key.c_str(), value.c_str()) == PULP_EMBED_OK;
    }

    // Snapshot every text_field's (key, value) for SerializeState. Reads the live
    // view, so it reflects in-editor edits even without a host callback wired.
    std::vector<std::pair<std::string, std::string>> captureStringState() const {
        std::vector<std::pair<std::string, std::string>> out;
        const int n = stringFieldCount();
        out.reserve(static_cast<size_t>(n));
        for (int i = 0; i < n; ++i) {
            std::string k = stringFieldKey(i);
            out.emplace_back(k, stringValue(k));
        }
        return out;
    }

    // Restore a snapshot from UnserializeState. Pushes each value host -> view
    // without echoing back through the set_string callback (no edit loop).
    void restoreStringState(const std::vector<std::pair<std::string, std::string>>& state) {
        for (const auto& kv : state) setStringValue(kv.first, kv.second);
    }

    // Optional live-edit notification: invoked (with key, new UTF-8 value)
    // whenever the user edits a text_field, so the plugin can mark its state
    // dirty. Only fires when the editor was constructed with a delegate (the
    // string host callbacks share that bridge's host_ctx). The captured value is
    // also kept in the bridge's string store.
    void setStringChangeHandler(std::function<void(const std::string&, const std::string&)> fn) {
        if (bridge_) bridge_->on_string = std::move(fn);
    }

    // ── ABI v8 runtime accessors (has_param / param_display_text / host_action) ─
    //
    // These need a bound bridge (constructed with a delegate), same as the
    // string-change handler — they share that bridge's host_ctx. On a
    // visual-only editor hasParam is false, paramDisplayText is empty, and host
    // actions are ignored.

    // Does a design control key resolve to a bound host parameter? (Mirrors the
    // v8 has_param callback; the belt-and-suspenders check behind the -1.0
    // get_param sentinel.) Const — does not log (the callback path logs once).
    bool hasParam(const char* key) const {
        return bridge_ && bridge_->find_idx(key) >= 0;
    }

    // Formatted, human-readable display of a NORMALIZED value for a design key
    // ("-6.0 dB", "440 Hz"). Memoized per (key, value). Uses the formatter set
    // via setParamDisplayFormatter; without one, returns a normalized read-out.
    std::string paramDisplayText(const char* key, double normalized) const {
        return bridge_ ? bridge_->param_display_impl(key, normalized) : std::string();
    }

    // Supply the value->text formatter. The plugin's body calls iPlug2
    // IParam::GetDisplay (with a WDL_String) in ITS translation unit — kept out
    // of this framework-neutral header so the compile/link check stays SDK-free:
    //   editor.setParamDisplayFormatter([this](const std::string& key, double n) {
    //     WDL_String s; GetParam(mKeyToIdx.at(key))->GetDisplayForHost(n, true, s);
    //     return std::string(s.Get());
    //   });
    void setParamDisplayFormatter(std::function<std::string(const std::string&, double)> fn) {
        if (bridge_) { bridge_->on_param_display = std::move(fn); bridge_->display_memo.clear(); }
    }

    // Handle an out-of-band UI action (a non-parameter button/link). The handler
    // parses `args_json` and returns non-zero if it handled the action. Backs
    // the v8 host_action callback.
    void setHostActionHandler(std::function<int(const std::string&, const std::string&)> fn) {
        if (bridge_) bridge_->on_host_action = std::move(fn);
    }

    // Fire a host action directly (the same entry point the v8 host_action
    // callback trampolines to). Returns the handler's result, or 0 if none.
    int dispatchHostAction(const std::string& action, const std::string& args_json = "{}") {
        return bridge_ && bridge_->on_host_action ? bridge_->on_host_action(action, args_json) : 0;
    }

    // ── Resize recipe ─────────────────────────────────────────────────────────
    //
    // Unlike JUCE — where an AudioProcessorEditor drives its OWN bounds and the
    // adapter's resized() forwards them — an iPlug2 plugin CONSTRAINS its editor
    // window itself: the initial size is PLUG_WIDTH/PLUG_HEIGHT (config.h) and
    // resizes flow through the plugin's OnParentWindowResize / a host-driven
    // ConstrainEditorResize path. The design's own constraints live in the
    // materialized view (pulp_embed_size_hints). These helpers are the idiomatic
    // iPlug2 seam: read the design's hints, seed the initial editor bounds
    // (size-on-open), and clamp host resizes to the design's aspect/min/max.
    // The plugin calls preferredSize() in its ctor to feed SetEditorSize(), and
    // constrainSize() from OnParentWindowResize(); the helper names no iPlug2
    // type, so it stays in the framework-neutral header. See README "Resize".

    // The design's resize constraints (preferred/min/max/aspect/resizable).
    PulpEmbedSizeHints sizeHints() const {
        PulpEmbedSizeHints h{};
        if (view_) pulp_embed_size_hints(view_, &h);
        return h;
    }

    // 1 iff the imported design declares itself resizable.
    bool isResizable() const { return sizeHints().resizable != 0; }

    // The design's preferred initial size (falls back to the ctor logical size
    // when the design carries no preference). Seed SetEditorSize() with this.
    void preferredSize(int& w, int& h) const {
        const PulpEmbedSizeHints hn = sizeHints();
        w = hn.preferred_width  > 0 ? hn.preferred_width  : w_;
        h = hn.preferred_height > 0 ? hn.preferred_height : h_;
    }

    // Clamp a requested (w,h) to the design's min/max, then — if the design
    // locks an aspect ratio — derive the dependent axis from the width (the
    // primary drag axis), re-clamping so the lock never violates min/max. Call
    // from the plugin's ConstrainEditorResize / OnParentWindowResize.
    void constrainSize(int& w, int& h) const {
        const PulpEmbedSizeHints hn = sizeHints();
        if (hn.min_width  > 0 && w < hn.min_width)  w = hn.min_width;
        if (hn.min_height > 0 && h < hn.min_height) h = hn.min_height;
        if (hn.max_width  > 0 && w > hn.max_width)  w = hn.max_width;
        if (hn.max_height > 0 && h > hn.max_height) h = hn.max_height;
        if (hn.aspect_ratio > 0.0f) {
            h = static_cast<int>(static_cast<float>(w) / hn.aspect_ratio + 0.5f);
            if (hn.min_height > 0 && h < hn.min_height) {
                h = hn.min_height;
                w = static_cast<int>(h * hn.aspect_ratio + 0.5f);
            }
            if (hn.max_height > 0 && h > hn.max_height) {
                h = hn.max_height;
                w = static_cast<int>(h * hn.aspect_ratio + 0.5f);
            }
        }
    }

    // Size-on-open: size the embedded view to the design's preferred size,
    // honoring aspect/min/max. Call once after open()/notifyAttached().
    void applyPreferredSizeOnOpen() {
        int w = w_, h = h_;
        preferredSize(w, h);
        constrainSize(w, h);
        resize(w, h, 1.0f);
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
    // Build the embed descriptor, wiring the host bridge when bridge_ is set (the
    // callbacks must be in the desc at create time, since host_ctx is captured
    // then). Shared by the file-based and native-view create paths.
    PulpEmbedDesc buildDesc() const {
        PulpEmbedDesc d{};
        d.struct_size = sizeof(PulpEmbedDesc);
        // Negotiate down to the LOWER of what this header knows and what the
        // linked library actually implements, so a header built ahead of the
        // shim (or vice versa) degrades gracefully instead of asserting a
        // capability neither side agrees on. struct_size still gates which
        // trailing callback fields the shim reads.
        const uint32_t lib = pulp_embed_abi_version();
        d.abi_version = PULP_VIEW_EMBED_ABI_VERSION < lib ? PULP_VIEW_EMBED_ABI_VERSION : lib;
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
            // ABI v6 string side-channel (text_field <-> host state). Shares the
            // bridge's host_ctx; get_string seeds from bridge_->strings (empty =
            // keep imported default) so a preset can be pre-loaded before create.
            d.host.set_string = &HostBridgeBase::cSetString;
            d.host.get_string = &HostBridgeBase::cGetString;
#if defined(PULP_VIEW_EMBED_ABI_VERSION) && PULP_VIEW_EMBED_ABI_VERSION >= 8
            // ABI v8 runtime accessors — wired only when BOTH this header and
            // the linked library carry the v8 callback tail. struct_size gates
            // field presence in the shim; the runtime check keeps a v8 header
            // linked against a pre-v8 library from writing fields the shim
            // won't read. Pre-v8, has_param degrades to the -1.0 get_param
            // sentinel and display/action are simply unavailable.
            if (lib >= 8u) {
                d.host.has_param          = &HostBridgeBase::cHasParam;
                d.host.param_display_text = &HostBridgeBase::cParamDisplayText;
                d.host.host_action        = &HostBridgeBase::cHostAction;
            }
#endif
        }
        return d;
    }

    // Shared construction body: build the desc, then create the view. When a
    // native-view factory_ is set, mount the host's compiled View (e.g. a
    // DesignFrameView subclass) via pulp_embed_create_from_view — its param_key'd
    // elements bind through the SAME host bridge; no file, so no hot-reload.
    // Otherwise create from a bundle dir or DesignIR JSON and arm hot-reload.
    void createView() {
        PulpEmbedDesc d = buildDesc();

        if (factory_) {
            pulp::embed::pulp_embed_create_from_view(&d, std::move(factory_), &view_);
            return;
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
    // Set when constructed from a native View factory (mutually exclusive with
    // path_): createView() mounts it via pulp_embed_create_from_view and moves
    // from it (one-shot). Empty for the file-based paths.
    pulp::embed::NativeViewFactory factory_;

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
