// Editor «Живая природа»: private preview World from the map, ignition,
// ticking, live read-only source and presentation frame (no GPU / audio).
#include "apps/editor/PhysisNaturePreview.h"
#include <cstdio>

using namespace aiverse;
using namespace aiverse::editor;
namespace {
int failed = 0;
#define CHECK(c) do { if(!(c)) { std::printf("NATURE PREVIEW FAIL %s:%d %s\n",__FILE__,__LINE__,#c);++failed; } } while(0)

const PhysisNaturePreview::HeightAt flat = [](float, float) { return 0.f; };

void privatePreview() {
    MapDoc doc; doc.reset(64, 48);
    fx::ParticleSystem ps;                      // never initialized: no device
    PhysisNaturePreview nature;
    nature.settings.enabled = true;
    nature.settings.previewCellMeters = 2;
    nature.settings.windSpeed = 12; nature.settings.windDirectionDeg = 0;
    nature.settings.humidity = 0.05f;
    const World* src = nature.prepareSource(ps, doc, nullptr, flat);
    CHECK(src && nature.previewWorld() == src && !nature.usingLive());
    const auto* state = src->resourceConst<physis::State>();
    CHECK(state && state->settings.cellSizeMeters == 2);
    CHECK(state->settings.width == 32 && state->settings.height == 24);
    CHECK(src->resourceConst<physis::DisasterState>());
    std::string why;
    CHECK(nature.ignite(20, 24, &why));
    CHECK(nature.advance(30) == 30);
    world::NaturePresentationPolicy p; p.centerX = 32; p.centerZ = 24; p.radiusMeters = 60;
    const auto frame = world::buildNaturePresentation(*src, p);
    CHECK(frame.valid && !frame.flames.empty());
    CHECK(frame.meanWindX > 8);
    double w = 0, x = 0;
    for (const auto& f : frame.flames) { w += f.intensity + f.smoke; x += f.x * (f.intensity + f.smoke); }
    CHECK(w > 0 && x / w > 20);                 // pushed downwind (+X)
    CHECK(frame.sample(20, 24).charred > 0.f);
    // Same config: no rebuild (fire survives); reset: fresh World.
    CHECK(nature.prepareSource(ps, doc, nullptr, flat)->resourceConst<physis::State>()->tick == 30);
    nature.requestReset();
    CHECK(nature.prepareSource(ps, doc, nullptr, flat)->resourceConst<physis::State>()->tick == 0);
    // Wind slider steers the private World.
    nature.settings.windDirectionDeg = 90;
    nature.applyWindSettings();
    nature.advance(120);
    const auto turned = world::buildNaturePresentation(*nature.previewWorld(), p);
    CHECK(turned.meanWindZ > 8 && std::abs(turned.meanWindX) < 2);
    // Disabled: no preview World exposed to the heatmap.
    nature.settings.enabled = false;
    CHECK(nature.previewWorld() == nullptr);
}

void mapGridAndFields() {
    MapDoc doc; doc.reset(32, 32);
    std::string error;
    CHECK(physis_authoring::apply(doc, physis_authoring::defaultConfig(), &error));
    fx::ParticleSystem ps;
    PhysisNaturePreview nature;
    nature.settings.enabled = true;
    nature.settings.previewCellMeters = 0;      // exact authored grid
    const World* src = nature.prepareSource(ps, doc, nullptr, flat);
    CHECK(src);
    Json map = Json::object(); map.set("physis", physis_authoring::read(doc));
    const auto options = physis::optionsFromMap(map);
    CHECK(options);
    const auto* state = src->resourceConst<physis::State>();
    CHECK(state && options && state->settings.width == options->climate.width &&
          state->settings.cellSizeMeters == options->climate.cellSizeMeters);
    // Editing the map's PHYSIS block rebuilds the preview on the next frame.
    Json cfg = physis_authoring::read(doc);
    Json climate = cfg["climate"]; climate.set("baseTemperature", Json::number(int64_t(30)));
    cfg.set("climate", climate);
    CHECK(physis_authoring::apply(doc, cfg, &error));
    nature.advance(5);
    CHECK(nature.prepareSource(ps, doc, nullptr, flat)->resourceConst<physis::State>()->tick == 0);
}

void liveIsReadOnly() {
    MapDoc doc; doc.reset(16, 16);
    World live;
    physis::Settings s; s.width = 4; s.height = 4; s.cellSizeMeters = 4;
    physis::initialize(live, s);
    fx::ParticleSystem ps;
    PhysisNaturePreview nature;
    nature.settings.enabled = true;
    CHECK(nature.prepareSource(ps, doc, &live, flat) == &live);
    CHECK(nature.usingLive() && nature.previewWorld() == nullptr);
    std::string why;
    CHECK(!nature.ignite(4, 4, &why) && !why.empty());
    CHECK(nature.advance(10) == 0);
    CHECK(live.resourceConst<physis::State>()->tick == 0);
}
}

int main() {
    privatePreview();
    mapGridAndFields();
    liveIsReadOnly();
    std::printf("PHYSIS nature preview: %s\n", failed ? "FAIL" : "PASS");
    return failed ? 1 : 0;
}
