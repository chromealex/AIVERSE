// Optional fixture used by the existing Metal and GL HDR runners.
// A wall touching the floor produces a continuous near-to-far shadow edge.
#pragma once
namespace aiverse::test {
inline void initContactShadowScene(HdrScene& s) {
    s.camera.target={1.5f,0,0};s.camera.distance=18;s.camera.pitch=1.25f;s.camera.yaw=0;
    s.renderer.lighting.sunDir={-0.8f,1,0.5f};
    s.renderer.lighting.sunIntensity=1;
    s.renderer.lighting.sunColor={1,1,1};
    s.renderer.lighting.skyColor={0.08f,0.08f,0.08f};
    s.renderer.lighting.groundColor={0.08f,0.08f,0.08f};
    s.renderer.lighting.shadowSoftness=0.4f;
    s.renderer.lighting.shadowContactHardening=std::getenv("AIVERSE_SHADOW_FIXED")==nullptr;
    if(const char* q=std::getenv("AIVERSE_SHADOWS"))
        s.renderer.setShadowQuality(std::string(q)=="low"?render::ShadowQuality::Low:std::string(q)=="medium"?render::ShadowQuality::Medium:render::ShadowQuality::High);
    s.renderer.setTraceEnabled(false);
    s.renderer.lights.setQuality(render::LightQuality::Off);
    s.post.configure(Json::parse(R"({"hdr":"off","effects":[]})"),Json());
}
inline void drawContactShadowScene(HdrScene& s) {
    s.renderer.drawMesh(s.cube,render::mat4::translate({-2.5f,0,0})*render::mat4::scale({0.5f,5,5}),{0.65f,0.5f,0.35f,1});
    // Detached small caster: its whole shadow must remain soft, including the nearest edge.
    s.renderer.drawMesh(s.cube,render::mat4::translate({1,2,-3})*render::mat4::scale({1,0.3f,1}),{0.35f,0.5f,0.65f,1});
}
}
