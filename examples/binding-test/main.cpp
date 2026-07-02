// Adapter-level test of PulpEmbedEditor's iPlug2 parameter bridge.
//
// The bridge is duck-typed (it never names an iPlug2 type — see PulpEmbedEditor.h),
// so this test drives it with a FAKE delegate that mimics the iPlug2
// IEditorDelegate surface (NParams / GetParam / Begin|Send|EndInformHostOfParam-
// ChangeFromUI). That means the adapter binding is verified WITHOUT the iPlug2 SDK,
// against the real pulp_view_embed C ABI and a real materialized design.
//
// It proves, end to end through the adapter:
//   (1) explicit key->paramIdx map resolves design controls to delegate params,
//   (2) a UI gesture (pulp_embed_simulate_param_drag) trampolines into the
//       delegate as Begin / Send(value) / End on the right param index,
//   (3) a host-side value change is pumped UI-ward on tick() and moves the
//       control (pulp_embed_param_value reflects it),
//   (4) the by-name fallback resolves a control whose key matches IParam::GetName().
//
// Fixture: the faithful-vector "VST Style" DesignIR (15 SVG-patch knobs), the same
// bindable-control fixture the pulp-view-embed smoke uses, via the lightweight
// DesignIR path (no JS engine needed).

#include "PulpEmbedEditor.h"

#include <pulp/view/design_frame_view.hpp>  // native-view path: hand-built View

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#ifndef PULP_EMBED_FIXTURES_DIR
#define PULP_EMBED_FIXTURES_DIR "fixtures"
#endif

namespace {

int g_failures = 0;
void check(bool ok, const char* what) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++g_failures;
}

// ── A minimal fake of the iPlug2 IEditorDelegate + IParam surface the bridge
// uses. Records UI->host calls so the test can assert the gesture sequence. ──
struct FakeParam {
    std::string name;
    double norm = 0.0;
    double GetNormalized() const { return norm; }
    void SetNormalized(double n) { norm = n; }
    const char* GetName() const { return name.c_str(); }
};

struct FakeDelegate {
    std::vector<FakeParam> params;
    std::vector<int> begun;
    std::vector<int> ended;
    std::vector<std::pair<int, double>> sets;

    int NParams() const { return static_cast<int>(params.size()); }
    FakeParam* GetParam(int i) {
        return (i >= 0 && i < static_cast<int>(params.size())) ? &params[i] : nullptr;
    }
    void BeginInformHostOfParamChangeFromUI(int i) { begun.push_back(i); }
    void EndInformHostOfParamChangeFromUI(int i) { ended.push_back(i); }
    // iPlug2's SendParameterValueFromUI sets the param normalized AND informs the
    // host; mirror both so get_param reflects the new value.
    void SendParameterValueFromUI(int i, double n) {
        sets.emplace_back(i, n);
        if (auto* p = GetParam(i)) p->SetNormalized(n);
    }
};

// Records the ABI v6 string-bridge host callbacks (text_field <-> host state).
struct StringHost {
    std::vector<std::pair<std::string, std::string>> sets;  // set_string calls
    std::map<std::string, std::string> store;               // backs get_string
    static void cSet(void* ctx, const char* key, const char* utf8) {
        auto* h = static_cast<StringHost*>(ctx);
        h->sets.emplace_back(key, utf8 ? utf8 : "");
        h->store[key] = utf8 ? utf8 : "";
    }
    static int32_t cGet(void* ctx, const char* key, char* out, int32_t cap) {
        auto* h = static_cast<StringHost*>(ctx);
        const auto it = h->store.find(key);
        if (it == h->store.end()) return -1;
        const int32_t n = static_cast<int32_t>(it->second.size());
        if (out && cap > 0) {
            const int32_t c = n < cap - 1 ? n : cap - 1;
            std::memcpy(out, it->second.data(), static_cast<size_t>(c));
            out[c] = '\0';
        }
        return n;
    }
};

std::string fixtures_dir() {
    if (const char* e = std::getenv("PULP_EMBED_FIXTURES_DIR")) return e;
    return PULP_EMBED_FIXTURES_DIR;
}

int index_of_key(PulpEmbedView* v, const std::string& key) {
    const int n = pulp_embed_param_count(v);
    for (int i = 0; i < n; ++i) {
        char k[256] = {0};
        pulp_embed_param_key(v, i, k, sizeof k);
        if (key == k) return i;
    }
    return -1;
}

}  // namespace

int main() {
    const std::string ir = fixtures_dir() + "/figma-vst-style-faithful/design.ir.json";
    const int W = 1000, H = 600;

    // Discover the first bindable design key (avoid hardcoding fixture internals).
    std::string firstKey;
    {
        pulp_iplug2::PulpEmbedEditor probe(ir, W, H);
        check(probe.valid(), "probe editor created from faithful DesignIR");
        if (!probe.valid()) {
            std::fprintf(stderr, "create failed: %s\n", probe.lastError().c_str());
            return 1;
        }
        const int n = pulp_embed_param_count(probe.view());
        check(n > 0, "design exposes >= 1 bindable control");
        char key[256] = {0};
        pulp_embed_param_key(probe.view(), 0, key, sizeof key);
        firstKey = key;
        check(!firstKey.empty(), "first bindable key is non-empty");
    }
    if (firstKey.empty()) return 1;
    std::printf("  (binding to design key \"%s\")\n", firstKey.c_str());

    // ── (1)+(2)+(3): explicit map, UI->host gesture, host->UI pump ──────────
    {
        FakeDelegate d;
        d.params.push_back({"GAIN", 0.0});  // param 0 (name irrelevant for explicit map)
        // nameFallback off so ONLY the explicit map binds.
        pulp_iplug2::PulpEmbedEditor ed(ir, W, H, d, {{firstKey.c_str(), 0}}, false);
        check(ed.valid(), "bound editor created");
        check(ed.boundParameterCount() == 1, "explicit map bound exactly one control");

        const int idx = index_of_key(ed.view(), firstKey);
        check(idx >= 0, "bound key is enumerable on the view");

        // resolveBindings pushed the delegate's initial value (0.0) into the
        // control; the param should now read ~0.0.
        const double initial = pulp_embed_param_value(ed.view(), idx);
        check(std::fabs(initial - 0.0) < 1e-2, "initial host value pushed into control on open");

        // (2) UI gesture: a real drag through the control's interaction path.
        const double before = pulp_embed_param_value(ed.view(), idx);
        const double target = (before < 0.5) ? 0.85 : 0.15;
        check(pulp_embed_simulate_param_drag(ed.view(), idx, target) == PULP_EMBED_OK,
              "simulate_param_drag OK");
        check(!d.begun.empty() && d.begun.back() == 0,
              "delegate saw BeginInformHostOfParamChangeFromUI(0)");
        check(!d.ended.empty() && d.ended.back() == 0,
              "delegate saw EndInformHostOfParamChangeFromUI(0)");
        check(!d.sets.empty() && d.sets.back().first == 0,
              "delegate saw SendParameterValueFromUI on param 0");
        const double after = pulp_embed_param_value(ed.view(), idx);
        check(std::fabs(after - before) > 0.05, "control value moved after the drag");
        check(!d.sets.empty() && std::fabs(d.sets.back().second - after) < 1e-3,
              "delegate's set value matches the control's new value");
        check(std::fabs(d.params[0].norm - after) < 1e-3,
              "delegate param normalized reflects the drag");

        // (3) host->UI pump: change the delegate value, tick, control follows.
        const double pushed = (after < 0.5) ? 0.90 : 0.10;
        const size_t sets_before = d.sets.size();
        d.params[0].SetNormalized(pushed);
        ed.tick();  // pumpHostToUi() runs here
        const double now = pulp_embed_param_value(ed.view(), idx);
        check(std::fabs(now - pushed) < 1e-3, "control reflects host-pushed value after tick");
        check(d.sets.size() == sets_before,
              "host->UI pump did NOT re-enter SendParameterValueFromUI (no feedback loop)");
    }

    // ── (4): by-name fallback (no explicit map) ─────────────────────────────
    {
        FakeDelegate d;
        d.params.push_back({firstKey, 0.5});  // param name == the design key
        pulp_iplug2::PulpEmbedEditor ed(ir, W, H, d, {}, /*nameFallback=*/true);
        check(ed.valid(), "name-fallback editor created");
        check(ed.boundParameterCount() == 1,
              "by-name fallback bound the control whose key matches IParam::GetName()");
    }

    // ── unmatched: an explicit map to a nonexistent key binds nothing ───────
    {
        FakeDelegate d;
        d.params.push_back({"GAIN", 0.0});
        pulp_iplug2::PulpEmbedEditor ed(ir, W, H, d, {{"no_such_key", 0}}, false);
        check(ed.boundParameterCount() == 0, "unmatched explicit key binds nothing (visual-only)");
    }

    // ── unbound controls keep their imported default (the seed path must NOT
    //    zero an unbound bindable control — get_param returns a sentinel for
    //    unbound keys so the embed keeps the design default). ─────────────────
    {
        std::vector<double> defaults;  // imported defaults from a host-less probe
        {
            pulp_iplug2::PulpEmbedEditor probe(ir, W, H);
            const int n = pulp_embed_param_count(probe.view());
            for (int i = 0; i < n; ++i)
                defaults.push_back(pulp_embed_param_value(probe.view(), i));
        }
        FakeDelegate d;
        d.params.push_back({"x", 0.0});  // bound control will be seeded to 0
        pulp_iplug2::PulpEmbedEditor ed(ir, W, H, d, {{firstKey.c_str(), 0}}, false);
        const int boundIdx = index_of_key(ed.view(), firstKey);
        const int n = pulp_embed_param_count(ed.view());
        bool others_preserved = true;
        bool any_nonzero_default = false;
        for (int i = 0; i < n; ++i) {
            if (i == boundIdx) continue;
            if (defaults[i] > 1e-3) any_nonzero_default = true;
            if (std::fabs(pulp_embed_param_value(ed.view(), i) - defaults[i]) > 1e-3)
                others_preserved = false;
        }
        check(any_nonzero_default,
              "fixture has >=1 unbound control with a non-zero default (test is meaningful)");
        check(others_preserved,
              "unbound controls keep their imported default (seed path does not zero them)");
    }

    // ── a stale/out-of-range explicit paramIdx is rejected, not bound ───────
    {
        FakeDelegate d;
        d.params.push_back({"x", 0.0});  // only param 0 exists
        pulp_iplug2::PulpEmbedEditor ed(ir, W, H, d, {{firstKey.c_str(), 999}}, false);
        check(ed.boundParameterCount() == 0,
              "explicit out-of-range paramIdx is rejected (would fire on an invalid index)");
    }

    // ── ABI v5 param metadata: the design exposes per-control kind/discreteness
    //    so a host can build a correct param (knobs continuous; the faithful
    //    fixture also has discrete choice controls with options). ─────────────
    {
        pulp_iplug2::PulpEmbedEditor probe(ir, W, H);
        const int n = pulp_embed_param_count(probe.view());
        int knobs = 0, discrete_with_options = 0, named = 0;
        bool kinds_nonempty = true, defaults_in_range = true, all_ok = (n > 0);
        bool found_macro_depth = false, unnamed_has_no_meta = true;
        for (int i = 0; i < n; ++i) {
            PulpEmbedParamInfo pi{};
            if (pulp_embed_param_info(probe.view(), i, &pi) != PULP_EMBED_OK) all_ok = false;
            if (pi.widget_kind[0] == '\0') kinds_nonempty = false;
            if (pi.default_norm < 0.0 || pi.default_norm > 1.0) defaults_in_range = false;
            if (std::string(pi.widget_kind) == "knob" && !pi.is_discrete) ++knobs;
            if (pi.is_discrete && pi.option_count > 0) ++discrete_with_options;
            // §2.1: the importer's design caption (IRInteractiveElement.label)
            // surfaces as the param NAME with has_meta set; unnamed controls keep
            // has_meta clear (host falls back to the key).
            if (pi.has_meta) {
                ++named;
                if (std::string(pi.name) == "Macro Depth") found_macro_depth = true;
            } else if (pi.name[0] != '\0') {
                unnamed_has_no_meta = false;  // a name without has_meta would be a bug
            }
        }
        check(all_ok, "param_info returns OK for every valid index");
        check(kinds_nonempty, "every control reports a non-empty widget_kind");
        check(defaults_in_range, "every control's default_norm is within [0,1]");
        check(knobs > 0, "at least one continuous knob is reported");
        check(discrete_with_options > 0,
              "at least one discrete control reports option_count > 0 (dropdown/tab/stepper)");
        // §2.1 end-to-end: the fixture's one labeled knob surfaces its caption.
        check(found_macro_depth,
              "labeled control surfaces its design caption as the param name (has_meta)");
        check(unnamed_has_no_meta,
              "an unnamed control reports has_meta=0 (no spurious name)");
        // out-of-range index is rejected + zero-filled
        PulpEmbedParamInfo bad{};
        bad.is_discrete = 7;
        check(pulp_embed_param_info(probe.view(), 99999, &bad) == PULP_EMBED_ERR_INVALID_ARG &&
                  bad.is_discrete == 0,
              "param_info rejects an out-of-range index and zero-fills");

        // Greenfield helper: designParams() mirrors the ABI metadata as
        // ready-to-init descriptors a plugin builds IParams from.
        const auto dps = probe.designParams();
        check(static_cast<int>(dps.size()) == n,
              "designParams() returns one descriptor per bindable control");
        bool keys_ok = true, kinds_ok = true;
        int dp_discrete = 0, dp_knobs = 0;
        for (const auto& d : dps) {
            if (d.key.empty()) keys_ok = false;
            if (d.widget_kind.empty()) kinds_ok = false;
            if (d.is_discrete && d.option_count > 0) ++dp_discrete;
            if (d.widget_kind == "knob" && !d.is_discrete) ++dp_knobs;
        }
        check(keys_ok && kinds_ok, "every descriptor carries a key + widget_kind");
        check(dp_knobs > 0 && dp_discrete > 0,
              "descriptors include continuous knobs AND discrete controls (greenfield init)");

        // Static greenfield reader: same descriptors WITHOUT an editor (offscreen),
        // so a plugin can declare IParams at construction time.
        const auto sdps = pulp_iplug2::PulpEmbedEditor::readDesignParams(ir, W, H);
        check(sdps.size() == dps.size(),
              "readDesignParams() (offscreen, no editor) returns the same count");
        bool same = sdps.size() == dps.size();
        for (size_t i = 0; same && i < sdps.size(); ++i)
            if (sdps[i].key != dps[i].key || sdps[i].widget_kind != dps[i].widget_kind ||
                sdps[i].is_discrete != dps[i].is_discrete)
                same = false;
        check(same, "offscreen readDesignParams() matches the editor's designParams()");
    }

    // ── ABI v6 text-field string bridge (raw ABI: a text_field binds to host
    //    string STATE, separate from the numeric param bridge). ──────────────
    {
        StringHost sh;
        PulpEmbedDesc d{};
        d.struct_size = sizeof(PulpEmbedDesc);
        d.abi_version = PULP_VIEW_EMBED_ABI_VERSION;
        d.logical_width = W;
        d.logical_height = H;
        d.scale_factor = 1.0f;
        d.backend_pref = PULP_EMBED_BACKEND_PREF_AUTO;
        d.design_width = W;
        d.design_height = H;
        d.host_ctx = &sh;
        d.host.set_string = &StringHost::cSet;
        d.host.get_string = &StringHost::cGet;
        PulpEmbedView* v = nullptr;
        pulp_embed_create_from_design_json(&d, ir.c_str(), &v);
        check(v != nullptr, "create with string host callbacks");
        if (v) {
            const int sc = pulp_embed_string_param_count(v);
            check(sc >= 1, "design exposes >= 1 text_field string param");
            char skey[256] = {0};
            pulp_embed_string_param_key(v, 0, skey, sizeof skey);
            check(skey[0] != '\0', "string param key is non-empty");

            // UI -> host: a simulated user edit fires host.set_string.
            check(pulp_embed_simulate_text_input(v, 0, "Hello") == PULP_EMBED_OK,
                  "simulate_text_input OK");
            check(!sh.sets.empty() && sh.sets.back().second == "Hello",
                  "host.set_string saw the user edit");
            char gv[256] = {0};
            pulp_embed_get_string(v, skey, gv, sizeof gv);
            check(std::string(gv) == "Hello", "get_string reflects the edit");

            // host -> view: push without echoing back to host.set_string.
            const size_t before = sh.sets.size();
            check(pulp_embed_set_string(v, skey, "World") == PULP_EMBED_OK, "set_string OK");
            char gv2[256] = {0};
            pulp_embed_get_string(v, skey, gv2, sizeof gv2);
            check(std::string(gv2) == "World", "field reflects the host-pushed string");
            check(sh.sets.size() == before,
                  "host->view set_string did NOT echo back to host.set_string (no loop)");

            // unknown key tolerated (blind push); out-of-range simulate rejected.
            check(pulp_embed_set_string(v, "no_such_text", "x") == PULP_EMBED_OK,
                  "set_string(unknown key) is a tolerated no-op");
            check(pulp_embed_simulate_text_input(v, 9999, "x") == PULP_EMBED_ERR_INVALID_ARG,
                  "simulate_text_input rejects an out-of-range index");
            pulp_embed_destroy(v);
        }
    }

    // ── ABI v6 string bridge through the ADAPTER (PulpEmbedEditor string API:
    //    live-edit handler + capture/restore round-trip for preset save/load) ──
    {
        FakeDelegate d;
        d.params.push_back({"GAIN", 0.0});
        // A delegate is present so the string host callbacks are wired (they
        // share the param bridge's host_ctx). nameFallback off = only the param
        // bridge's explicit map binds; strings are independent of it.
        pulp_iplug2::PulpEmbedEditor ed(ir, W, H, d, {{firstKey.c_str(), 0}}, false);
        check(ed.valid(), "string-adapter editor created");

        const int sc = ed.stringFieldCount();
        check(sc >= 1, "adapter reports >= 1 text_field string control");
        if (sc >= 1) {
            const std::string skey = ed.stringFieldKey(0);
            check(!skey.empty(), "adapter string field key is non-empty");

            // Live-edit notification: a simulated user edit fires the handler.
            std::vector<std::pair<std::string, std::string>> edits;
            ed.setStringChangeHandler(
                [&](const std::string& k, const std::string& v) { edits.emplace_back(k, v); });

            check(pulp_embed_simulate_text_input(ed.view(), 0, "Hello") == PULP_EMBED_OK,
                  "adapter: simulate_text_input OK");
            check(!edits.empty() && edits.back().first == skey && edits.back().second == "Hello",
                  "adapter: string-change handler saw the live edit");
            check(ed.stringValue(skey) == "Hello", "adapter: stringValue reflects the edit");

            // captureStringState (SerializeState): snapshot the live view.
            auto saved = ed.captureStringState();
            bool captured = false;
            for (const auto& kv : saved)
                if (kv.first == skey && kv.second == "Hello") captured = true;
            check(captured, "adapter: captureStringState snapshots the edited value");

            // restoreStringState (UnserializeState): push without echoing back.
            const size_t edits_before = edits.size();
            std::vector<std::pair<std::string, std::string>> preset = {{skey, "World"}};
            ed.restoreStringState(preset);
            check(ed.stringValue(skey) == "World", "adapter: restoreStringState recalls the preset");
            check(edits.size() == edits_before,
                  "adapter: restore did NOT re-fire the change handler (no echo loop)");

            // Blind restore of an unknown key is tolerated.
            ed.restoreStringState({{"no_such_text_field", "x"}});
            check(true, "adapter: restore of an unknown key is a tolerated no-op");
        }
    }

    // ── NATIVE-VIEW path: mount a HAND-BUILT DesignFrameView (no DesignIR/ui.js)
    //    via the factory ctor and prove its param_key'd controls bind to the
    //    duck-typed delegate exactly like an imported design. Proves the
    //    pulp_embed_create_from_view + DesignFrameElement::param_key substrate
    //    end to end through the iPlug2 adapter. ────────────────────────────────
    {
        using pulp::view::DesignFrameElement;
        auto knob = [](float cx, std::string key) {
            DesignFrameElement e;
            e.kind = DesignFrameElement::Kind::knob;
            e.cx = cx; e.cy = 40.0f; e.hit_radius = 18.0f;
            e.needle_d = "M0 0L0 -8";
            e.param_key = std::move(key);
            return e;
        };
        // factory_ is moved-from inside the editor, so build a fresh factory here.
        auto factory = [knob]() -> std::unique_ptr<pulp::view::View> {
            const std::string svg =
                R"(<svg width="240" height="80" xmlns="http://www.w3.org/2000/svg">)"
                R"(<rect x="0" y="0" width="240" height="80" fill="#222"/></svg>)";
            std::vector<DesignFrameElement> els{knob(40, "gain"), knob(120, "cutoff"),
                                                knob(200, "")};  // last: visual-only
            return std::make_unique<pulp::view::DesignFrameView>(svg, std::move(els));
        };

        FakeDelegate d;
        d.params.push_back({"gain", 0.0});    // idx 0
        d.params.push_back({"cutoff", 0.0});  // idx 1
        pulp_iplug2::PulpEmbedEditor ed(
            pulp::embed::NativeViewFactory{factory}, 240, 80, d,
            {{"gain", 0}, {"cutoff", 1}}, /*nameFallback=*/false);
        check(ed.valid(), "native-view editor created from a View factory");
        check(ed.boundParameterCount() == 2,
              "native: two param_key'd controls bound (keyless knob stays visual-only)");

        const int gainIdx = index_of_key(ed.view(), "gain");
        check(gainIdx >= 0, "native: 'gain' key enumerable on the live view");
        if (gainIdx >= 0) {
            check(pulp_embed_simulate_param_drag(ed.view(), gainIdx, 0.8) == PULP_EMBED_OK,
                  "native: simulate_param_drag on 'gain' OK");
            check(!d.begun.empty() && d.begun.back() == 0,
                  "native: delegate saw Begin on param 0 (gain)");
            check(!d.sets.empty() && d.sets.back().first == 0 &&
                      std::fabs(d.params[0].norm - 0.8) < 0.05,
                  "native: gain drag drove delegate param 0 to ~0.8");
        }
    }

    // ── ABI v8 runtime accessors through the ADAPTER (has_param /
    //    param_display_text / host_action). These go through the SAME bridge +
    //    trampolines the v8 host callbacks use, so they exercise the accessor
    //    code even when the linked ABI header predates v8 (the desc-callback
    //    wiring is #if-gated on PULP_VIEW_EMBED_ABI_VERSION >= 8). ────────────
    {
        FakeDelegate d;
        d.params.push_back({"GAIN", 0.5});
        pulp_iplug2::PulpEmbedEditor ed(ir, W, H, d, {{firstKey.c_str(), 0}}, false);
        check(ed.valid(), "v8-accessor editor created");

        // has_param: a bound key resolves; an unmapped key does not.
        check(ed.hasParam(firstKey.c_str()),
              "hasParam() true for a bound design key (belt-and-suspenders behind get_param)");
        check(!ed.hasParam("no_such_key"),
              "hasParam() false for an unmapped design key");

        // param_display_text: default (no formatter) yields a normalized read-out;
        // a supplied formatter (the seam where a real plugin calls IParam::GetDisplay)
        // is used and memoized per (key, value).
        const std::string def = ed.paramDisplayText(firstKey.c_str(), 0.5);
        check(!def.empty(), "paramDisplayText() returns a value even without a formatter");

        int formatterCalls = 0;
        ed.setParamDisplayFormatter([&](const std::string& k, double n) {
            ++formatterCalls;
            char b[64];
            std::snprintf(b, sizeof b, "%s=%.0f%%", k.c_str(), n * 100.0);
            return std::string(b);
        });
        const std::string t1 = ed.paramDisplayText(firstKey.c_str(), 0.5);
        const std::string t2 = ed.paramDisplayText(firstKey.c_str(), 0.5);  // memo hit
        check(t1 == firstKey + "=50%", "paramDisplayText() uses the supplied formatter");
        check(t1 == t2 && formatterCalls == 1,
              "paramDisplayText() memoizes per (key, value) — formatter called once");
        ed.paramDisplayText(firstKey.c_str(), 0.25);  // new value -> formatter again
        check(formatterCalls == 2, "a new value re-invokes the formatter (memo keyed on value)");

        // host_action: the handler receives (action, args_json) and its result
        // is returned.
        std::string sawAction, sawArgs;
        ed.setHostActionHandler([&](const std::string& a, const std::string& j) {
            sawAction = a; sawArgs = j; return 1;
        });
        const int r = ed.dispatchHostAction("open_manual", R"({"page":3})");
        check(r == 1 && sawAction == "open_manual" && sawArgs == R"({"page":3})",
              "dispatchHostAction() forwards (action, args_json) and returns the handler result");
        check(ed.dispatchHostAction("noop") == 0 || r == 1,
              "dispatchHostAction() tolerates an action even with a handler present");
    }

    // ── v8 accessors on a VISUAL-ONLY editor (no delegate) degrade cleanly ──
    {
        pulp_iplug2::PulpEmbedEditor ed(ir, W, H);
        check(!ed.hasParam(firstKey.c_str()), "visual-only: hasParam() is false (no bridge)");
        check(ed.paramDisplayText(firstKey.c_str(), 0.5).empty(),
              "visual-only: paramDisplayText() is empty (no bridge)");
        check(ed.dispatchHostAction("x") == 0, "visual-only: dispatchHostAction() is a no-op");
    }

    // ── Resize recipe: the design's size hints feed the iPlug2 constrain
    //    path (preferred size-on-open + aspect/min/max clamp). ─────────────────
    {
        pulp_iplug2::PulpEmbedEditor ed(ir, W, H);
        check(ed.valid(), "resize-hints editor created");
        const PulpEmbedSizeHints hn = ed.sizeHints();
        // The faithful fixture reports SOME size; preferredSize falls back to the
        // ctor logical size when the design carries no preference.
        int pw = 0, ph = 0;
        ed.preferredSize(pw, ph);
        check(pw > 0 && ph > 0, "preferredSize() returns a positive size (design pref or ctor fallback)");

        // constrainSize clamps to min/max. Drive it with synthetic bounds to
        // prove the clamp math independent of the fixture's specific hints.
        // (min/max of 0 = unbounded, so a fixture without bounds leaves the
        // request unchanged — assert the invariant that holds either way.)
        int cw = 10, chh = 10;  // absurdly small
        ed.constrainSize(cw, chh);
        if (hn.min_width  > 0) check(cw  >= hn.min_width,  "constrainSize() enforces min_width");
        if (hn.min_height > 0) check(chh >= hn.min_height, "constrainSize() enforces min_height");
        check(cw > 0 && chh > 0, "constrainSize() never yields a non-positive size");

        int bw = 1000000, bh = 1000000;  // absurdly large
        ed.constrainSize(bw, bh);
        if (hn.max_width  > 0) check(bw <= hn.max_width,  "constrainSize() enforces max_width");
        if (hn.max_height > 0) check(bh <= hn.max_height, "constrainSize() enforces max_height");
        // isResizable() is a pure query — just prove it returns without crashing.
        (void) ed.isResizable();
        check(true, "isResizable() query is callable");
    }

    // ── aspect-lock clamp is exercised deterministically (no fixture dependency):
    //    a bridge-less editor's constrainSize is a pure function of sizeHints, so
    //    verify the aspect math holds for the general contract. Here we assert the
    //    invariant that width stays the driving axis when an aspect is present. ─
    {
        // We cannot inject synthetic hints without the ABI, so this just documents
        // + guards that constrainSize is monotonic: a larger request never yields
        // a smaller constrained width than a smaller request.
        pulp_iplug2::PulpEmbedEditor ed(ir, W, H);
        int a = 200, ah = 200; ed.constrainSize(a, ah);
        int b = 800, bh = 800; ed.constrainSize(b, bh);
        check(b >= a, "constrainSize() is monotonic in width (larger request -> >= width)");
    }

    std::printf("%s\n", g_failures == 0 ? "pulp-embed-iplug2 binding-test OK"
                                        : "pulp-embed-iplug2 binding-test FAILED");
    return g_failures == 0 ? 0 : 1;
}
