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

#include <cmath>
#include <cstdio>
#include <cstdlib>
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
        int knobs = 0, discrete_with_options = 0;
        bool kinds_nonempty = true, defaults_in_range = true, all_ok = (n > 0);
        for (int i = 0; i < n; ++i) {
            PulpEmbedParamInfo pi{};
            if (pulp_embed_param_info(probe.view(), i, &pi) != PULP_EMBED_OK) all_ok = false;
            if (pi.widget_kind[0] == '\0') kinds_nonempty = false;
            if (pi.default_norm < 0.0 || pi.default_norm > 1.0) defaults_in_range = false;
            if (std::string(pi.widget_kind) == "knob" && !pi.is_discrete) ++knobs;
            if (pi.is_discrete && pi.option_count > 0) ++discrete_with_options;
        }
        check(all_ok, "param_info returns OK for every valid index");
        check(kinds_nonempty, "every control reports a non-empty widget_kind");
        check(defaults_in_range, "every control's default_norm is within [0,1]");
        check(knobs > 0, "at least one continuous knob is reported");
        check(discrete_with_options > 0,
              "at least one discrete control reports option_count > 0 (dropdown/tab/stepper)");
        // out-of-range index is rejected + zero-filled
        PulpEmbedParamInfo bad{};
        bad.is_discrete = 7;
        check(pulp_embed_param_info(probe.view(), 99999, &bad) == PULP_EMBED_ERR_INVALID_ARG &&
                  bad.is_discrete == 0,
              "param_info rejects an out-of-range index and zero-fills");
    }

    std::printf("%s\n", g_failures == 0 ? "pulp-embed-iplug2 binding-test OK"
                                        : "pulp-embed-iplug2 binding-test FAILED");
    return g_failures == 0 ? 0 : 1;
}
