// Baked lighting in the renderer (agent-lightbake): a static model placed where the bake has a region
// draws with the LIGHTMAP shader variant and its uv2 scale / offset in the instance block; a model
// elsewhere gets the PROBES variant; quality Off draws neither. Recording device, no GPU.
#include "engine/render/BakedLighting.h"
#include "engine/render/LightmapUv.h"

namespace bakedrendertest {
class ShaderRecorder final : public aiverse::gfx::Device {
public:
    struct Draw { std::string fragment; std::vector<float> inst; };
    std::vector<Draw> draws;
    std::vector<std::string> shaders;                 // fragment source per shader handle - 1
    std::vector<uint32_t> pipeShader;                 // pipeline handle - 1 -> shader handle
    std::vector<std::string> pipeLabel;
    const aiverse::gfx::DeviceInfo& info() const override { return info_; }
    aiverse::gfx::BufferHandle createBuffer(const aiverse::gfx::BufferDesc&) override { return {++next_}; }
    void updateBuffer(aiverse::gfx::BufferHandle, const void*, size_t, size_t) override {}
    void destroyBuffer(aiverse::gfx::BufferHandle) override {}
    aiverse::gfx::TextureHandle createTexture(const aiverse::gfx::TextureDesc&) override { return {++next_}; }
    void updateTexture(aiverse::gfx::TextureHandle, int, int, int, int, const void*, int) override {}
    void destroyTexture(aiverse::gfx::TextureHandle) override {}
    aiverse::gfx::ShaderHandle createShader(const aiverse::gfx::ShaderDesc& d) override {
        shaders.push_back(d.glslFragment ? d.glslFragment : "");
        return {uint32_t(shaders.size())};
    }
    void destroyShader(aiverse::gfx::ShaderHandle) override {}
    aiverse::gfx::PipelineHandle createPipeline(const aiverse::gfx::PipelineDesc& d) override {
        pipeShader.push_back(d.shader.id); pipeLabel.push_back(d.label ? d.label : "");
        return {uint32_t(pipeShader.size())};
    }
    void destroyPipeline(aiverse::gfx::PipelineHandle) override {}
    const std::string& lastError() const override { return err_; }
    void beginFrame(int, int) override {}
    void endFrame() override {}
    void beginPass(const aiverse::gfx::PassDesc&) override {}
    void endPass() override {}
    void setViewport(const aiverse::gfx::Rect&) override {}
    void setScissor(const aiverse::gfx::Rect&) override {}
    void setPipeline(aiverse::gfx::PipelineHandle p) override { pipe_ = p.id; }
    void setVertexBuffer(aiverse::gfx::BufferHandle, size_t) override {}
    void setIndexBuffer(aiverse::gfx::BufferHandle, size_t) override {}
    void setTexture(int, aiverse::gfx::TextureHandle) override {}
    void setUniforms(int slot, const void* d, size_t n) override {
        if (slot == 0) { const float* f = static_cast<const float*>(d); u0_.assign(f, f + n / sizeof(float)); }
    }
    void draw(uint32_t, uint32_t) override {}
    void drawIndexed(uint32_t, uint32_t, int32_t, uint32_t) override {
        if (pipe_ == 0 || pipe_ > pipeShader.size() || pipeLabel[pipe_ - 1] != "model") return;
        const uint32_t s = pipeShader[pipe_ - 1];
        draws.push_back({s && s <= shaders.size() ? shaders[s - 1] : std::string(), u0_});
    }
private:
    aiverse::gfx::DeviceInfo info_;
    std::string err_;
    uint32_t next_ = 0, pipe_ = 0;
    std::vector<float> u0_;
};
} // namespace bakedrendertest

TEST(baked_lighting_lightmap_and_probe_variants) {
    using namespace bakedrendertest;
    using namespace aiverse::render;
    MeshBuilder mb;
    mb.box({0, 0.5f, 0}, {1, 1, 1});
    Material mt;
    auto model = modelFromParts("crate", {{"body", &mb, mt, mat4{}}});
    model->path = "models/crate.glb";
    for (auto& v : model->vertices) setLightmapUv(v, {0.5f, 0.5f});
    for (auto& mesh : model->meshes) for (auto& p : mesh.primitives) p.lightmapUv = true;
    ShaderRecorder dev;
    Renderer3D r3d;
    CHECK(r3d.init(dev));
    r3d.setShadowQuality(ShadowQuality::Off);
    CHECK(uploadModel(dev, *model));
    int meshNode = -1;
    for (size_t i = 0; i < model->nodes.size(); ++i) if (model->nodes[i].mesh >= 0) { meshNode = int(i); break; }
    CHECK(meshNode >= 0);
    const mat4 placed = mat4::translate({2, 0, 3});
    aiverse::bake::LightmapData data;
    data.pages.push_back({});
    data.pages[0].width = data.pages[0].height = 4;
    data.pages[0].rgbm.assign(64, 128);
    aiverse::bake::LightmapRegion region;
    region.st[0] = 0.25f; region.st[1] = 0.5f; region.st[2] = 0.125f; region.st[3] = 0.375f;
    data.regions[aiverse::bake::lightmapRegionKey(model->path, meshNode, placed * model->bindWorld[size_t(meshNode)])] = region;
    data.probes.nx = data.probes.ny = data.probes.nz = 2;
    data.probes.coeffs.assign(8 * 12, 0.25f);
    BakedLighting baked;
    CHECK(baked.upload(dev, std::move(data)));
    r3d.setBakedLighting(&baked);
    r3d.setBakedQuality(BakedQuality::High);
    RTSCamera cam; cam.target = {2, 0, 3}; cam.distance = 12; cam.pitch = 1.0f;
    auto frame = [&](const mat4& world) {
        dev.draws.clear();
        r3d.beginScene(cam, 640, 480);
        ModelDraw d; d.model = model.get(); d.world = world; d.flags = kDrawStatic;
        r3d.drawModel(d);
        r3d.render(aiverse::gfx::PassDesc{});
        return dev.draws;
    };
    const auto lit = frame(placed);
    CHECK(!lit.empty());
    for (const auto& d : lit) {
        CHECK(d.fragment.find("#define LIGHTMAP 1") != std::string::npos && d.fragment.find("#define PROBES") == std::string::npos);
        CHECK(d.inst.size() >= 32 && d.inst[28] == 0.25f && d.inst[29] == 0.5f && d.inst[30] == 0.125f && d.inst[31] == 0.375f);
    }
    const auto moved = frame(mat4::translate({2.5f, 0, 3}));   // not where it was baked: probes
    CHECK(!moved.empty());
    for (const auto& d : moved) CHECK(d.fragment.find("#define PROBES 1") != std::string::npos && d.fragment.find("#define LIGHTMAP") == std::string::npos);
    r3d.setBakedQuality(BakedQuality::Off);
    const auto off = frame(placed);
    CHECK(!off.empty());
    for (const auto& d : off) CHECK(d.fragment.find("#define LIGHTMAP") == std::string::npos && d.fragment.find("#define PROBES") == std::string::npos);
    // capture: static draws are reported with their node matrices
    int captured = 0;
    r3d.setModelCapture([&](const ModelDraw&, const std::vector<mat4>& nodes) { ++captured; CHECK(nodes.size() == model->nodes.size()); });
    frame(placed);
    r3d.setModelCapture({});
    CHECK(captured == 1);
    r3d.setBakedLighting(nullptr);
    baked.release();
    releaseModel(dev, *model);
    r3d.shutdown();
}
