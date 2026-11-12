// agent-vfxgraph: engine side of the Unity VFX Graph / TrailRenderer / LineRenderer import (tools/importers/unity_vfx).

TEST(fx_line_shape_follows_arc_modes) {
    // "spread" puts burst particle i of n at (2 i / n - 1) * radius along the line (LineRenderer import);
    // random keeps the original uniform math.
    FxRun r(R"({ "emitters": [{ "burst": 4, "life": 5, "speed": 0, "spread": 0, "shape": "line", "radius": 2,
                 "arcMode": "spread", "size": 0.1 }] })");
    r.ps.play(r.id, {0, 0, 0});
    r.step(0.1f);
    std::vector<float> xs;
    for (auto& p : r.parts) xs.push_back(p.pos.x + p.pos.y + p.pos.z);   // the line lies along one effect axis
    std::sort(xs.begin(), xs.end());
    CHECK(xs.size() == 4);
    if (xs.size() == 4) {
        const float want[4] = {-2, -1, 0, 1};
        for (int i = 0; i < 4; ++i) CHECK(std::fabs(std::fabs(xs[size_t(i)]) - std::fabs(want[i])) < 1e-3f);
    }
    FxRun rnd(R"({ "emitters": [{ "burst": 50, "life": 5, "speed": 0, "spread": 0, "shape": "line", "radius": 2, "size": 0.1 }] })");
    rnd.ps.play(rnd.id, {0, 0, 0});
    rnd.step(0.1f);
    float lo = 1e9f, hi = -1e9f;
    for (auto& p : rnd.parts) { float v = p.pos.x + p.pos.y + p.pos.z; lo = std::min(lo, v); hi = std::max(hi, v); }
    CHECK(rnd.parts.size() == 50 && lo >= -2.001f && hi <= 2.001f && hi - lo > 2);
}

TEST(fx_ribbon_trail_renderer_follows_the_effect) {
    // TrailRenderer import: hidden particles spawned per metre, joined into a ribbon; the ribbon follows move()
    // and shrinks away after stop().
    FxRun r(R"({ "emitters": [{ "hidden": true, "loop": true, "duration": 1, "perMeter": 4, "life": 0.5, "speed": 0,
                 "spread": 0, "size": 0.5, "trail": { "mode": "ribbon", "time": 0.5, "width": [[0, 1], [1, 0]] } }] })");
    auto h = r.ps.play(r.id, {0, 0, 0});
    for (int i = 1; i <= 10; ++i) { r.ps.move(h, {float(i) * 0.5f, 0, 0}); r.step(1 / 30.0f); }
    CHECK(r.parts.size() >= 15);   // ~4 per metre over 5 m
    r.ps.stop(h);
    r.step(1.0f);
    CHECK(r.parts.empty());
}

TEST(fx_external_import_dir_loads) {
    // Checks a whole imported effect folder (Unity import dry runs kept outside the repo):
    //   AIVERSE_FX_CHECK_DIR=<project> aiverse_tests fx_external_import_dir_loads
    // Without the variable there is nothing to check.
    const char* dir = std::getenv("AIVERSE_FX_CHECK_DIR");
    if (!dir || !*dir) return;
    const std::string root = dir;
    fx::ParticleSystem ps;
    ps.setQuality(fx::FxQuality::High);
    ps.init(nullptr);
    ps.setFileReader([&](const std::string& p, std::string& text) {
        std::ifstream f(root + "/" + p, std::ios::binary);
        if (!f) return false;
        text.assign(std::istreambuf_iterator<char>(f), {});
        return true;
    });
    int loaded = 0, failed = 0;
    for (auto& e : std::filesystem::directory_iterator(root + "/fx")) {
        if (e.path().extension() != ".json" || e.path().stem() == "fx") continue;
        const int id = ps.effect(e.path().stem().string());
        if (id < 0) { ++failed; continue; }
        ++loaded;
        ps.play(id, {0, 0, 0});
        for (int i = 0; i < 30; ++i) ps.update(1 / 30.0f);
        ps.clear();
    }
    int missingTex = 0;
    for (auto& er : ps.errors()) {
        if (er.find("texture") != std::string::npos) { ++missingTex; continue; }   // dry runs write no images
        std::printf("    %s\n", er.c_str());
    }
    std::printf("    %d effects loaded, %d failed, %d texture messages\n", loaded, failed, missingTex);
    CHECK(failed == 0);
}

TEST(fx_many_projectile_ribbons_stay_in_budget) {
    // Drone Fall fires hundreds of projectiles with TrailRenderer ribbons: per tier the particle budget holds,
    // low drops the ribbons (trails start at medium) and the simulation cost stays small.
    const char* json = R"({ "emitters": [{ "hidden": true, "loop": true, "duration": 1, "perMeter": 10, "life": 0.18, "speed": 0,
        "spread": 0, "size": 2, "maxParticles": 11, "trail": { "mode": "ribbon", "time": 0.18, "width": [[0, 1], [1, 0.06]] } },
        { "texture": "builtin:spark", "loop": true, "duration": 1, "rate": 30, "life": 0.3, "speed": 0.5, "size": 0.3, "maxParticles": 12 }] })";
    for (fx::FxQuality q : {fx::FxQuality::Low, fx::FxQuality::Medium, fx::FxQuality::High}) {
        FxRun r(json, q);
        std::vector<fx::FxHandle> hs;
        for (int i = 0; i < 300; ++i) hs.push_back(r.ps.play(r.id, {float(i % 20), 1, float(i / 20)}));
        const auto t0 = std::chrono::steady_clock::now();
        for (int f = 0; f < 60; ++f) {
            for (size_t i = 0; i < hs.size(); ++i) r.ps.move(hs[i], {float(i % 20) + f * 0.3f, 1, float(i / 20)});
            r.ps.update(1 / 60.0f);
        }
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / 60;
        const auto lim = fx::limitsFor(q);
        std::printf("    %s: %d particles (budget %d), %.3f ms / frame for 300 trails\n", fx::qualityName(q), r.ps.stats().particles,
                    lim.maxParticles, ms);
        CHECK(r.ps.stats().particles <= lim.maxParticles);
    }
}
