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
