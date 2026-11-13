// Map-scoped, frame-budgeted presentation loading. Simulation rules are never filtered.
#include "GameClient.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <limits>
#include <imgui.h>

namespace aiverse::client {
namespace {
float visibleRadius(const render::RTSCamera& camera) {
    const float w = float(platform::windowWidth()), h = float(platform::windowHeight());
    float radius = 8;
    for (float x : {0.0f, w}) for (float y : {0.0f, h}) {
        render::float3 point;
        if (camera.screenToGround(x,y,w,h,point)) radius = std::max(radius, std::hypot(point.x-camera.target.x,point.z-camera.target.z));
        else return camera.maxDistance * 2; // horizon-facing camera: conservatively prepare the scene
    }
    return radius;
}
}

std::string GameClient::initLabel(int step) const {
    static const char* keys[] = {"loading.rules", "loading.renderer", "loading.view", "loading.effects", "loading.audio", "loading.ui", "loading.ready"};
    return tr(keys[std::clamp(step, 0, 6)]);
}

void GameClient::drawLoadingStatus(gfx::Device& dev, double dt, float progress, const std::string& label) {
    overlay_.newFrame(std::max(dt, 0.001));
    auto* draw = ImGui::GetForegroundDrawList();
    const float w = ImGui::GetIO().DisplaySize.x, h = ImGui::GetIO().DisplaySize.y;
    const auto percent = tr("loading.progress", i18n::Args().num("percent", int(progress * 100 + 0.5f)));
    const float y = h * 0.80f;
    // Clip long resource paths; never let them run outside a narrow phone window.
    draw->PushClipRect({w * 0.06f, y - 4}, {w * 0.94f, h}, true);
    const auto size = ImGui::CalcTextSize(label.c_str());
    draw->AddText({std::max(w * 0.07f, (w - size.x) * 0.5f), y}, IM_COL32(235,235,235,255), label.c_str());
    draw->AddText({(w - ImGui::CalcTextSize(percent.c_str()).x) * 0.5f, y + ImGui::GetTextLineHeight() + 6}, IM_COL32(180,180,180,255), percent.c_str());
    if (platform::hostedView()) {
        draw->AddRectFilled({w*.1f, h*.9f}, {w*.9f, h*.9f+4}, IM_COL32(50,50,50,255));
        draw->AddRectFilled({w*.1f, h*.9f}, {w*(.1f+.8f*progress), h*.9f+4}, IM_COL32(255,120,30,255));
    }
    draw->PopClipRect();
    gfx::PassDesc pass;
    pass.clearColorEnabled = false; pass.clearDepthEnabled = false;
    pass.colorTarget = hostColor_; pass.depthTarget = hostDepth_;
    dev.beginPass(pass);
    overlay_.render();
    dev.endPass();
}

void GameClient::configureLoading() {
    initialAssetRadius_ = 0;
    if (std::getenv("AIVERSE_EAGER_LOADING")) return;
    if (!std::getenv("AIVERSE_FULL_MAP_LOADING")) initialAssetRadius_ = float(manifest_["loading"].getInt("initialRadius", 0));
    if (const char* mode = std::getenv("AIVERSE_STREAM_MODELS"); mode && std::string(mode) == "off") return;
    const auto& spec = manifest_["loading"];
    for (const auto& [id, radius] : spec["typeRadii"].members()) for (auto& type : rules_.types) if(type.id==id) type.view.set("streamingRadius",radius);
    const int64_t initial = spec.getInt("initialModelsPerType", 0);
    if (initial <= 0) return;
    for (const auto& id : spec["streamTypes"].items()) for (auto& type : rules_.types) if (type.id == id.str()) {
        Json streaming = Json::object(); streaming.set("initialModels", Json::number(initial));
        type.view.set("streaming", std::move(streaming));
    }
}

void GameClient::updateBackgroundLoading() {
    if (initialAssetRadius_ > 0) {
        for (const auto& o : scene_.objects) if (!o.isProjectile && !view_.typeLoaded(o.type) &&
            std::find(backgroundTypes_.begin(), backgroundTypes_.end(), o.type) == backgroundTypes_.end()) backgroundTypes_.push_back(o.type);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(2);
        while (!backgroundTypes_.empty()) {
            if (backgroundType_ < 0) {
                std::vector<float> distance(rules_.types.size(), std::numeric_limits<float>::max());
                for (const auto& o : scene_.objects) if (o.type < distance.size()) distance[o.type] = std::min(distance[o.type], assetDistance(o));
                backgroundType_ = *std::min_element(backgroundTypes_.begin(), backgroundTypes_.end(), [&](auto a, auto b) { return distance[a] < distance[b]; });
            }
            const auto type = uint16_t(backgroundType_);
            if (!view_.prepareType(type)) return;
            view_.loadType(type);
            if (typeAssetLoader_) typeAssetLoader_(type);
            backgroundTypes_.erase(std::remove(backgroundTypes_.begin(), backgroundTypes_.end(), type), backgroundTypes_.end());
            backgroundType_ = -1;
            if (backgroundTypes_.empty()) std::fprintf(stderr,"[streaming] map types ready tick=%u stalls=%zu textures=%zu gpuTextures=%zu\n",unsigned(sim_->tick()),loadingStalls_,view_.textureCache()->decodedCount(),view_.textureCache()->gpuCount());
            if (std::chrono::steady_clock::now() >= deadline) return;
        }
    }
    const size_t before = view_.pendingModelVariants();
    if (before && !backgroundRemaining_) std::fprintf(stderr, "[streaming] background variants queued=%zu\n", before);
    view_.streamModelVariants(2.0);
    const size_t after = view_.pendingModelVariants();
    if (before && !after) std::fprintf(stderr, "[streaming] all model variants ready at tick=%u\n", unsigned(sim_->tick()));
    backgroundRemaining_ = after;
}

float GameClient::assetDistance(const SceneObject& o) const {
    const auto& type = rules_.types[o.type];
    // Include large objects whose centres lie outside the loading ring.
    const float extent = std::max({8.0f, float(type.view["streamingRadius"].toDouble()), .5f * std::hypot(float(type.footprintW), float(type.footprintH))});
    return std::max(0.0f, std::hypot(o.pos.x - cam_.target.x, o.pos.z - cam_.target.z) - extent);
}
float GameClient::assetRadius() const {
    // Initial ring is deliberately wider than the normal camera footprint.
    return std::max(initialAssetRadius_, visibleRadius(cam_) + 12.0f);
}
bool GameClient::ensureLocalAssets() {
    if (initialAssetRadius_ <= 0 || planet()) return false;
    // A sudden camera jump/zoom must show loading rather than holes. Normal movement is prefetched.
    const float requiredRadius = visibleRadius(cam_);
    for (const auto& o : scene_.objects) if (!o.isProjectile && !view_.typeLoaded(o.type) && assetDistance(o) < requiredRadius) {
        ++loadingStalls_;
        std::fprintf(stderr,"[streaming] local readiness wait tick=%u type=%u distance=%.2f\n",unsigned(sim_->tick()),unsigned(o.type),assetDistance(o));
        beginMapLoading();
        return mapLoading_;
    }
    return false;
}

void GameClient::beginMapLoading() {
    loadingTypes_.clear(); warmingTypes_.clear(); loadingCursor_ = 0;
    // Include hidden units too. Fog visibility must not decide what is ready for the map.
    backgroundTypes_.clear(); backgroundType_ = -1;
    view_.setStreamingTypes(initialAssetRadius_ > 0 && !planet());
    std::vector<bool> near(rules_.types.size(), false), present(rules_.types.size(), false);
    for (const auto& o : scene_.objects) if (!o.isProjectile && o.type < near.size()) {
        present[o.type] = true;
        if (initialAssetRadius_ <= 0 || planet() || assetDistance(o) <= assetRadius()) near[o.type] = true;
    }
    sim_->world().each<Unit>([&](Entity, Unit& u) { if (u.type < present.size()) present[u.type] = true; });
    // Types from nearby dynamic spawners must be ready before their first simulation tick.
    for (const auto& id : manifest_["loading"]["warmTypes"].items()) for(size_t type=0;type<rules_.types.size();++type)
        if(rules_.types[type].id==id.str()) near[type]=present[type]=true;
    for (size_t type = 0; type < present.size(); ++type) if (present[type]) {
        const bool required=initialAssetRadius_ <= 0 || planet() || near[type];
        if (!view_.typeLoaded(uint16_t(type))) {
            if (required) loadingTypes_.push_back(uint16_t(type));
            else backgroundTypes_.push_back(uint16_t(type));
        } else if (required && initialAssetRadius_ > 0) {
            // A cancelled loading screen may leave models uploaded but compilation pending.
            warmingTypes_.push_back(uint16_t(type));
        }
    }
    std::sort(loadingTypes_.begin(), loadingTypes_.end());
    loadingTypes_.erase(std::unique(loadingTypes_.begin(), loadingTypes_.end()), loadingTypes_.end());
    loadingStarted_ = std::chrono::steady_clock::now();
    mapLoading_ = !loadingTypes_.empty() || !warmingTypes_.empty();
    loadingPrimed_ = false;
    std::fprintf(stderr,"[streaming] initial=%zu background=%zu radius=%.1f focus=%.1f,%.1f\n",loadingTypes_.size(),backgroundTypes_.size(),assetRadius(),cam_.target.x,cam_.target.z);
    if (!mapLoading_) return;
    loadingSplash_.shutdown();
    Json block = manifest_["splash"];
    if (!block.isObject()) block = splash::defaultBlock();
    loadingSplash_.init(*dev_, block, [](const std::string& path, std::string& out) { return platform::readContentFile(path, out); });
    std::fprintf(stderr, "[loading] map=%s types=%zu/%zu\n", maps_[mapIndex_].file.c_str(), loadingTypes_.size(), rules_.types.size());
}

void GameClient::frameMapLoading(gfx::Device& dev, double dt) {
    if (netMatch_) { bool advanced = false; updateNet(dt, advanced); if (!mapLoading_) return; }
    // First present the label, then perform the work on the next frame. Keep GPU operations
    // on the render thread (also works without pthreads on web).
    if (loadingPrimed_) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(8);
        while (loadingCursor_ < loadingTypes_.size()) {
            const auto type = loadingTypes_[loadingCursor_];
            if (!view_.prepareType(type)) break;
            ++loadingCursor_;
            const auto started = std::chrono::steady_clock::now();
            view_.loadType(type);
            if (initialAssetRadius_ > 0) warmingTypes_.push_back(type);
            if (typeAssetLoader_) typeAssetLoader_(type);
            if (std::getenv("AIVERSE_LOAD_TRACE")) {
                const auto ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
                std::fprintf(stderr, "[loading] type=%u resource=%s ms=%.2f\n", unsigned(type), view_.typeResource(type).c_str(), ms);
            }
            if (std::chrono::steady_clock::now() >= deadline) break;
        }
    }
    warmingTypes_.erase(std::remove_if(warmingTypes_.begin(),warmingTypes_.end(),
        [&](uint16_t type) { return view_.warmPipelines(r3d_,type); }),warmingTypes_.end());
    const bool shadersReady=warmingTypes_.empty();
    loadingPrimed_ = true;
    const float modelsProgress = loadingTypes_.empty() ? 1.0f :
        (float(loadingCursor_) + view_.typePreparationProgress()) / float(loadingTypes_.size());
    const float p = .9f * std::min(1.0f,modelsProgress) + (shadersReady ? .1f : 0.0f);
    loadingSplash_.setProgress(p);
    loadingSplash_.update(dt, true);
    if (platform::hostedView()) {
        gfx::PassDesc pass; pass.colorTarget = hostColor_; pass.depthTarget = hostDepth_;
        dev.beginPass(pass); dev.endPass();
    } else loadingSplash_.draw(dev, 1, true, platform::dpiScale());
    const auto label = loadingCursor_ < loadingTypes_.size()
        ? tr("loading.resource", i18n::Args().str("name", (view_.preparingResource().empty() ? view_.typeResource(loadingTypes_[loadingCursor_]) : view_.preparingResource()))) : tr(shadersReady ? "loading.ready" : "loading.shaders");
    drawLoadingStatus(dev, dt, loadingSplash_.shownProgress(), label);
    if (shadersReady && loadingCursor_ == loadingTypes_.size() && loadingSplash_.imagesLoaded() && loadingSplash_.shownProgress() >= 0.98f) {
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - loadingStarted_).count();
        std::fprintf(stderr, "[loading] map assets ready in %.1f ms (%zu types)\n", ms, loadingTypes_.size());
        loadingSplash_.shutdown();
        mapLoading_ = false; mapJustLoaded_ = true;
    }
}

} // namespace aiverse::client
