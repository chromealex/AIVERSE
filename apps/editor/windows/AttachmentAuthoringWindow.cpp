// AIVERSE Studio — genre-independent imported attachment authoring.
// Dedicated ModelAssetProvider owns BOTH parent and child GPU/CPU Models.
// We never borrow a Modeling/AnimationGraph window's directly-owned Model
// into this cache, and never create a second animation evaluator.
#include "../Editor.h"
#include "../EditorUtil.h"
#include "engine/view/ImportedAttachmentAuthoring.h"
#include "engine/view/AttachmentLayout.h"
#include "engine/view/EcsAttachmentScene.h"
#include "engine/view/SceneAttachmentHost.h"
#include "engine/view/AnimatedSceneAttachmentSources.h"
#include "engine/render/Animation.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::editor {
namespace fs = std::filesystem;

class AttachmentAuthoringWindow final : public EditorWindow {
    using Imported = view::ImportedAttachmentAuthoring;
    using Registry = view::AppearanceAssetRegistry;
    struct AuthoredChild {
        std::string model;
        std::string socket;
        bool enabled=true;
        render::mat4 local{};
        render::mat4 childWorld{};
        std::string id; // optional stable prefab child key
        bool animated=false;
        std::string animationGraph; // project-relative Graph asset
    };
    struct Session {
        std::unique_ptr<render::ModelAssetProvider> provider;
        std::unique_ptr<Registry> registry;
        Registry::Snapshot source;
        // Cosmetic, source-neutral ECS world, never gameplay Simulation.
        World presentation;
        Entity parentEntity{};
        std::unique_ptr<view::SceneAttachmentHost> attachmentHost;
        std::map<std::string,Registry::Snapshot> animatedChildren;
        std::vector<Registry::Snapshot> children;
    };

    gfx::Device* device_=nullptr; // Editor owner, never freed here
    std::string project_;
    std::string sourcePath_;
    std::string layoutPath_="attachments.json";
    std::string newChild_,newSocket_,message_;
    std::vector<AuthoredChild> authored_;
    std::unique_ptr<Session> session_;
    // A retired provider still owns every old CPU/GPU Model until a full
    // GPU-completion proof. No idle stall on ordinary successful publication.
    // This is bounded deferred ownership, NOT fence-based destruction.
    std::vector<std::unique_ptr<Session>> retiredSessions_;
    static constexpr size_t kRetainedSessionLimit=8;
    render::Renderer3D renderer_;
    Viewport viewport_;
    ::aiverse::View presentationView_; // render-only, no graph evaluation
    ::aiverse::View animatedPreviewView_; // child poses precomputed in ONE host
    view::AnimatedSceneAttachmentSources animatedSources_;
    bool animatedFrameReady_=false;
    std::vector<view::AnimatedSceneAttachmentSources::Key> animatedFrameKeys_;
    render::Pose previewPose_;
    std::vector<render::mat4> previewMatrices_;
    float previewTime_=0.f,previewYaw_=0.6f,previewPitch_=0.4f;
    bool previewPlaying_=true,rendererReady_=false;
    bool dirty_=false;
    bool pendingReimport_=false;

    static bool readAsset(const std::string& path,std::string& result) {
        std::ifstream input(path,std::ios::binary);
        if(!input)return false;
        result.assign(std::istreambuf_iterator<char>(input),
                      std::istreambuf_iterator<char>());
        return !input.bad();
    }

    // Restrict authoring to the current project, including through symlinks.
    // Input files must exist; a new layout JSON may be created in an existing
    // project directory. No absolute or parent-traversal authoring paths.
    std::string withinProject(const std::string& path) const {
        if(project_.empty() || path.empty())
            throw std::invalid_argument("Open a project and choose a file");
        const fs::path relative(path);
        if(relative.is_absolute())
            throw std::invalid_argument("Only project-relative files are supported");
        for(const auto& part:relative)
            if(part=="..")
                throw std::invalid_argument("Parent directory escape is forbidden");
        const auto root=fs::weakly_canonical(fs::path(project_));
        const auto target=fs::weakly_canonical(root/relative);
        const auto local=target.lexically_relative(root);
        if(local.empty() || local.is_absolute())
            throw std::invalid_argument("Asset path is outside the project");
        for(const auto& part:local)
            if(part=="..")
                throw std::invalid_argument("Symlink escapes the project");
        return target.string();
    }

    // Full device-idle proof is deliberately conservative for authoring
    // transactions. If it fails, keep the provider alive (leak rather than
    // destruct GPU-visible Models with no completion proof).
    bool retire(std::unique_ptr<Session>& owned) noexcept {
        if(!owned)return true;
        if(!device_)return false;
        try {
            if(!device_->waitForResourceIdle())return false;
            if(owned->attachmentHost)
                owned->attachmentHost->clear();
            owned->attachmentHost.reset();
            owned->animatedChildren.clear();
            owned->children.clear();
            owned->source.reset();
            owned->registry.reset();
            if(owned->provider)
                owned->provider->shutdown(*device_);
            owned.reset();
            return true;
        }catch(...) {return false;}
    }
    void preserveUnsafe(std::unique_ptr<Session>& owned) noexcept {
        // Never let an unproven GPU owner destruct at window teardown.
        if(owned)(void)owned.release();
    }
    void deferSession(std::unique_ptr<Session>& owned) noexcept {
        if(!owned)return;
        try {
            retiredSessions_.push_back(std::move(owned));
        }catch(...) {
            // No queue storage is safe to reclaim without GPU completion.
            preserveUnsafe(owned);
        }
    }
    void collectSessions(bool force=false) noexcept {
        if(!device_ || retiredSessions_.empty() ||
           (!force && retiredSessions_.size()<kRetainedSessionLimit))
            return;
        try {
            if(!device_->waitForResourceIdle()) {
                message_="Retired attachment Models held: GPU idle unavailable";
                return;
            }
            for(auto& session:retiredSessions_) {
                if(!retire(session))preserveUnsafe(session);
            }
            retiredSessions_.clear();
        }catch(...) {
            // A failed GPU proof must never free prior resource generations.
        }
    }

    void preview(Editor& ed) {
        if(!session_ || !session_->source || !session_->source->model)
            return;
        if(!rendererReady_) {
            viewport_.setGpuDeviceLease(ed.gpuDeviceLease());
            if(!renderer_.init(ed.device()))
                throw std::runtime_error("attachment preview renderer initialization failed");
            renderer_.setShadowQuality(render::ShadowQuality::Low);
            rendererReady_=true;
        }
        auto available=ImGui::GetContentRegionAvail();
        const float height=std::clamp(available.y*0.50f,180.f,470.f);
        viewport_.ensure(ed.device(),
                         std::clamp(int(available.x),96,1200),
                         std::clamp(int(height),96,900),
                         ed.viewportRetirement(), "Attachments");
        const auto* model=session_->source->model;
        auto rigRead=session_->provider->resources().acquireModelRead(model);
        if(!rigRead) {
            message_="Preview parent Model is no longer cache-resident";
            return;
        }
        const int clip=model->clips.empty()?-1:0;
        if(clip>=0) {
            const float duration=model->clips[size_t(clip)].duration;
            if(std::isfinite(duration) && duration>0.f) {
                if(previewPlaying_)
                    previewTime_=std::fmod(previewTime_+1.f/60.f,duration);
                ImGui::Checkbox("Play preview",&previewPlaying_);
                ImGui::SliderFloat("Preview time",&previewTime_,0.f,duration);
            }
        }
        render::bindPose(*model,previewPose_);
        if(clip>=0)
            render::sampleClip(model->clips[size_t(clip)],
                               previewTime_,previewPose_);
        render::worldMatrices(*model,previewPose_,previewMatrices_);
        render::RTSCamera cam;
        cam.target=(model->boundsMin+model->boundsMax)*0.5f;
        cam.distance=std::max(2.f,
            render::length(model->boundsMax-model->boundsMin)*1.4f);
        cam.minDistance=0.1f;
        cam.yaw=previewYaw_;
        cam.pitch=previewPitch_;
        renderer_.beginScene(cam,viewport_.w,viewport_.h);
        presentationView_.beginPreparedFrame();
        animatedPreviewView_.beginPreparedFrame();
        Imported::Prepared packet;
        std::string error;
        const bool ready=session_->attachmentHost &&
            session_->attachmentHost->prepare(
                1,"preview",session_->provider->resources(),
                render::mat4{},packet,&error);
        // Graphs were stepped exactly ONCE by EditorWindow::update at
        // the pre-draw structural barrier. This ImGui draw only prepares
        // borrowed poses and sends graphless packets to Renderer3D.
        std::vector<view::AnimatedSceneAttachmentSources::Prepared>
            animatedPackets;
        if(ready) {
            packet.object.id=1; // isolated preview source identity
            packet.object.draw.pose=&previewPose_;
            packet.object.draw.nodeWorld=&previewMatrices_;
            presentationView_.submitOne(renderer_,packet.object);
            if(animatedFrameReady_) {
                animatedPackets.reserve(animatedFrameKeys_.size());
                for(const auto& childKey:animatedFrameKeys_) {
                    view::AnimatedSceneAttachmentSources::Prepared child;
                    std::string childError;
                    if(animatedSources_.prepare(
                           *session_->attachmentHost,
                           session_->provider->resources(),
                           childKey,packet.object.draw,
                           std::span<const render::mat4>(
                               previewMatrices_.data(),previewMatrices_.size()),
                           child,&childError)) {
                        // A different graphless View renders this fully
                        // prepared child. No second Graph evaluation.
                        animatedPreviewView_.submitOne(renderer_,child.object);
                        animatedPackets.push_back(std::move(child));
                    } else message_="Animated child preview: "+childError;
                }
            }
        } else message_="Preview packet rejected: "+error;
        gfx::PassDesc pass;
        pass.colorTarget=viewport_.color;
        pass.depthTarget=viewport_.depth;
        pass.clearColor[0]=0.08f;
        pass.clearColor[1]=0.09f;
        pass.clearColor[2]=0.12f;
        pass.clearColor[3]=1.f;
        renderer_.render(pass);
        ed.device().endPass();
        // Both the parent read lease and prepared child/LOD read leases
        // remain alive until after Renderer3D::render has consumed inputs.
        viewport_.image(ed.device(),available.x,height);
        if(ImGui::IsItemHovered() &&
           ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            previewYaw_+=ImGui::GetIO().MouseDelta.x*0.01f;
            previewPitch_=std::clamp(
                previewPitch_+ImGui::GetIO().MouseDelta.y*0.01f,-0.9f,1.4f);
        }
    }

    void loadLayout() {
        const auto path=withinProject(layoutPath_);
        std::string contents;
        if(!readAsset(path,contents))
            throw std::runtime_error("Attachment JSON file is not readable");
        view::AttachmentLayout layout;
        view::AttachmentLayoutCodec::read(Json::parse(contents),layout);
        std::vector<AuthoredChild> next;
        next.reserve(layout.attachments.size());
        for(const auto& entry:layout.attachments) {
            (void)withinProject(entry.model);
            next.push_back({entry.model,entry.socket,entry.enabled,
                            entry.local,entry.childWorld,entry.id,
                            entry.animated,entry.animationGraph});
        }
        const auto parent=layout.parent.empty()?sourcePath_:layout.parent;
        if(!parent.empty())(void)withinProject(parent);
        sourcePath_=parent;
        authored_.swap(next);
        dirty_=true;
        message_="Loaded authored layout; choose Reimport to publish";
    }

    void saveLayout() {
        const auto path=withinProject(layoutPath_);
        view::AttachmentLayout layout;
        layout.parent=sourcePath_;
        for(const auto& child:authored_)
            layout.attachments.push_back({
                child.model,child.socket,child.enabled,
                child.local,child.childWorld,child.id,
                child.animated,child.animationGraph});
        const Json root=view::AttachmentLayoutCodec::write(layout);
        std::ofstream output(path,std::ios::binary|std::ios::trunc);
        if(!output || !(output<<root.dump(2)))
            throw std::runtime_error("Unable to write attachment layout");
        dirty_=false;
        message_="Saved attachment JSON";
    }

    void stageAndPublish(Editor& ed) {
        // Owner-thread, no render submissions from this editor window.
        // A candidate provider is independent from the current last-good
        // owner: even a failed GPU upload cannot evict old published Models.
        auto staged=std::make_unique<Session>();
        staged->provider=std::make_unique<render::ModelAssetProvider>(readAsset);
        // Share the Editor Device's explicit generation token. Pointer
        // identity alone is insufficient after graphics device recreation.
        staged->provider->setGpuDeviceLease(ed.gpuDeviceLease());
        staged->registry=std::make_unique<Registry>();
        std::string error;
        try {
            const auto parentPath=withinProject(sourcePath_);
            auto* parent=staged->provider->load(ed.device(),parentPath);
            if(!parent)
                throw std::runtime_error("Parent Model import failed");
            Registry::Request parentRequest;
            parentRequest.model=parent;
            parentRequest.sources.push_back(parentPath);
            if(!staged->registry->publish("studio/attachments/parent",
                                           parentRequest,{}, {},&error))
                throw std::runtime_error("Parent appearance: "+error);
            staged->source=staged->registry->find("studio/attachments/parent");
            std::map<std::string,Registry::Snapshot> imported;
            view::AttachmentLayout document;
            document.parent=sourcePath_;
            for(const auto& child:authored_)
                document.attachments.push_back({
                    child.model,child.socket,child.enabled,
                    child.local,child.childWorld,child.id,
                    child.animated,child.animationGraph});
            // Validate EVERY entry (including disabled child identities)
            // before importing GPU resources into this staging provider.
            const Json authored=view::AttachmentLayoutCodec::write(document);
            size_t authoredIndex=0;
            for(const auto& child:authored_) {
                ++authoredIndex;
                if(!child.enabled)continue;
                const auto source=withinProject(child.model);
                if(child.animated) {
                    if(child.id.empty() || child.animationGraph.empty())
                        throw std::invalid_argument(
                            "animated child needs stable id and Graph file");
                    auto* model=staged->provider->load(ed.device(),source);
                    if(!model)
                        throw std::runtime_error(
                            "animated child source Model import failed");
                    Registry::Request request;
                    request.model=model;
                    request.sources.push_back(source);
                    request.animationGraph=Json::string(
                        withinProject(child.animationGraph));
                    request.graphReader=readAsset;
                    request.sourceId=uint64_t(authoredIndex);
                    const auto key="studio/attachments/animated/"+child.id;
                    if(!staged->registry->publish(
                           key,request,{}, {},&error))
                        throw std::runtime_error(
                            "animated child Graph compilation: "+error);
                    auto appearance=staged->registry->find(key);
                    if(!appearance || !appearance->graph)
                        throw std::runtime_error(
                            "animated child published without Graph");
                    staged->animatedChildren.emplace(
                        child.id,std::move(appearance));
                    continue;
                }
                auto it=imported.find(source);
                if(it==imported.end()) {
                    Registry::Snapshot appearance;
                    Registry::Request request;
                    if(!Imported::importChild(
                        *staged->provider,ed.device(),*staged->registry,
                        "studio/attachments/child/"+source,
                        source,request,appearance,{},&error))
                        throw std::runtime_error("Child appearance: "+error);
                    it=imported.emplace(source,std::move(appearance)).first;
                }
            }
            staged->parentEntity=staged->presentation.create();
            staged->attachmentHost=
                std::make_unique<view::SceneAttachmentHost>(
                    staged->presentation);
            const auto resolver=[&](const std::string& key)->Registry::Snapshot {
                const auto found=imported.find(withinProject(key));
                return found==imported.end()?Registry::Snapshot{}:found->second;
            };
            if(!staged->attachmentHost->activate(
                   {1,"preview"},staged->parentEntity,
                   authored,
                   staged->source,staged->provider->resources(),
                   resolver,&error))
                throw std::runtime_error("Scene attachment activate: "+error);
            // All new Models and sockets are ready in a SEPARATE cache.
            // Make publication immediately visible and defer the previous
            // provider's destruction; no full-idle wait on common reimport.
            // It remains alive so old in-flight GPU work cannot outlive it.
            // Old child GraphInstances must be detached while the old
            // provider and published CPU Model remain alive.
            animatedSources_.clear();
            auto old=std::move(session_);
            session_=std::move(staged);
            deferSession(old);
            const auto* active=session_->attachmentHost->find(1,"preview");
            message_="Published "+
                     std::to_string(active?active->bindings.children.size():0)+
                     " sockets in presentation ECS; retired providers: "+
                     std::to_string(retiredSessions_.size());
        }catch(...) {
            // Even a partially uploaded scratch provider may be GPU-live.
            // Keep it in the same deferred owner queue on failure.
            deferSession(staged);
            throw;
        }
    }

public:
    ~AttachmentAuthoringWindow() override {
        // Release independent child GraphInstances while all borrowed
        // Model/Graph publications are still owned by this window.
        animatedSources_.clear();
        // Editor::shutdown has already proven GPU idle before destroying
        // its windows. Do not discard owner-side attachments without proof.
        if(device_) {
            try {
                if(device_->waitForResourceIdle() && rendererReady_) {
                    viewport_.release(*device_);
                    renderer_.shutdown();
                    rendererReady_=false;
                }
            }catch(...) {}
        }
        if(!retire(session_))preserveUnsafe(session_);
        collectSessions(true);
        for(auto& pending:retiredSessions_)preserveUnsafe(pending);
        retiredSessions_.clear();
    }
    const char* id() const override{return "animation_attachments";}
    const char* title() const override{return "MotionWeave — Attachments";}
    const char* group() const override{return "look";}
    const char* dockArea() const override{return "right";}
    bool openByDefault() const override{return false;}
    int order() const override{return 35;}

    void update(Editor& ed,double dt) override {
        device_=&ed.device();
        // Advisory bounded cleanup, not one blocking barrier per edit.
        // Stage/Studio tests must verify GPU completion on each backend.
        collectSessions();
        const auto current=ed.ws().project.dir;
        if(current==project_) {
            // Runs at Studio's owner update boundary before windows draw
            // and before another ImGui/render submission can borrow Models.
            if(pendingReimport_) {
                pendingReimport_=false;
                try { stageAndPublish(ed); }
                catch(const std::exception& e) { message_=e.what(); }
            }
            animatedFrameReady_=false;
            animatedFrameKeys_.clear();
            if(open && session_ && session_->attachmentHost) {
                std::vector<view::AnimatedSceneAttachmentSources::Input> inputs;
                inputs.reserve(session_->animatedChildren.size());
                for(const auto& [id,appearance]:session_->animatedChildren)
                    inputs.push_back({
                        {1,"preview",id},appearance,appearance->parameters});
                view::AnimatedSceneAttachmentSources::Statistics stats;
                std::string error;
                const float delta=previewPlaying_
                    ?float(std::clamp(dt,0.,0.1)):0.f;
                if(animatedSources_.sync(
                       *session_->attachmentHost,session_->provider->resources(),
                       inputs,delta,stats,&error)) {
                    animatedFrameReady_=true;
                    for(const auto& input:inputs)
                        animatedFrameKeys_.push_back(input.key);
                } else message_="Animated child graph evaluation: "+error;
            }
            return;
        }
        // Old GPU Models remain owned by the deferred provider even after
        // the project changes, until GPU idle can be proven on this device.
        animatedSources_.clear();
        animatedFrameReady_=false;
        animatedFrameKeys_.clear();
        deferSession(session_);
        project_=current;
        pendingReimport_=false;
        authored_.clear();
        sourcePath_.clear();
        layoutPath_="attachments.json";
        newChild_.clear();
        newSocket_.clear();
        dirty_=false;
        message_.clear();
    }

    void draw(Editor& ed) override {
        if(project_!=ed.ws().project.dir) {
            ImGui::TextUnformatted("Project switch awaiting GPU owner cleanup");
            return;
        }
        if(project_.empty()) {
            ImGui::TextUnformatted("Open a project to author Model attachments");
            return;
        }
        ImGui::TextUnformatted(
            "Source-neutral imported child Models. No gameplay/genre rules.");
        ImGui::TextDisabled(
            "Dedicated provider; does not borrow ModelingWindow GPU Models.");
        ui::inputString("Parent Model",sourcePath_);
        ui::inputString("Layout JSON",layoutPath_);
        if(ImGui::Button("Load JSON")) {
            try{loadLayout();}catch(const std::exception& e){message_=e.what();}
        }
        ImGui::SameLine();
        if(ImGui::Button("Save JSON")) {
            try{saveLayout();}catch(const std::exception& e){message_=e.what();}
        }
        ImGui::Separator();
        for(size_t i=0;i<authored_.size();++i) {
            ImGui::PushID(int(i));
            auto& child=authored_[i];
            if(ImGui::Checkbox("Enabled",&child.enabled))dirty_=true;
            if(ui::inputString("Child Model",child.model))dirty_=true;
            if(ui::inputString("Socket",child.socket))dirty_=true;
            if(ui::inputString("Prefab child ID",child.id))dirty_=true;
            if(ImGui::Checkbox("Independent animation",&child.animated))
                dirty_=true;
            if(child.animated &&
               ui::inputString("Child AnimationGraph file",child.animationGraph))
                dirty_=true;
            float position[]={
                child.local.m[12],child.local.m[13],child.local.m[14]};
            if(ImGui::DragFloat3("Socket offset",position,0.01f)) {
                child.local.m[12]=position[0];
                child.local.m[13]=position[1];
                child.local.m[14]=position[2];
                dirty_=true;
            }
            if(ImGui::TreeNode("Advanced affine transforms")) {
                const auto editBasis=[&](const char* prefix,render::mat4& matrix) {
                    for(int col=0;col<3;++col) {
                        ImGui::PushID(col);
                        float axis[]={matrix.m[col*4],matrix.m[col*4+1],
                                      matrix.m[col*4+2]};
                        if(ImGui::DragFloat3(prefix,axis,0.01f)) {
                            for(int row=0;row<3;++row)
                                matrix.m[col*4+row]=axis[row];
                            dirty_=true;
                        }
                        ImGui::PopID();
                    }
                };
                ImGui::TextUnformatted("Socket-local orientation / scale");
                editBasis("Local axis",child.local);
                ImGui::TextUnformatted("Child-model orientation / scale");
                editBasis("Child axis",child.childWorld);
                float origin[]={
                    child.childWorld.m[12],
                    child.childWorld.m[13],
                    child.childWorld.m[14]};
                if(ImGui::DragFloat3("Child offset",origin,0.01f)) {
                    child.childWorld.m[12]=origin[0];
                    child.childWorld.m[13]=origin[1];
                    child.childWorld.m[14]=origin[2];
                    dirty_=true;
                }
                ImGui::TextDisabled(
                    "Matrix axes support arbitrary rotation, scale and shear");
                ImGui::TreePop();
            }
            if(ImGui::Button("Detach")) {
                authored_.erase(authored_.begin()+i);
                dirty_=true;
                ImGui::PopID();
                break;
            }
            ImGui::Separator();
            ImGui::PopID();
        }
        ui::inputString("New child",newChild_);
        ui::inputString("New socket",newSocket_);
        if(ImGui::Button("Attach") && !newChild_.empty() && !newSocket_.empty()) {
            authored_.push_back({newChild_,newSocket_,true});
            newChild_.clear();
            newSocket_.clear();
            dirty_=true;
        }
        if(ImGui::Button("Reimport & Publish")) {
            // Never wait for GPU completion from the middle of an active
            // Studio UI draw. Run import/replace in the NEXT owner update.
            pendingReimport_=true;
            message_="Reimport queued for the next pre-draw owner update";
        }
        if(dirty_)ImGui::TextDisabled("Unsaved attachment layout");
        if(session_) {
            // Separate preview owns its own source+child Model cache;
            // existing Modeling/Graph windows keep independent graph clocks.
            try { preview(ed); }
            catch(const std::exception& e) {message_=e.what();}
            ImGui::Text("Published source: %s",
                sourcePath_.c_str());
            const auto* active=session_->attachmentHost
                ?session_->attachmentHost->find(1,"preview"):nullptr;
            ImGui::Text("Presentation ECS children: %zu",
                active?active->children.size():0);
            ImGui::Text("Compiled active sockets: %zu",
                active?active->bindings.children.size():0);
            ImGui::Text("Retired provider sessions: %zu",
                        retiredSessions_.size());
            ImGui::TextDisabled(
                "Retirement is deferred; full GPU idle fallback only at cap/shutdown");
        }
        if(!message_.empty())ImGui::TextWrapped("%s",message_.c_str());
        ImGui::TextDisabled(
            "This window authors & validates real assets; live 3D preview "
            "and nonblocking GPU hot reload remain separate work.");
    }
};
AIVERSE_EDITOR_WINDOW(AttachmentAuthoringWindow);
} // namespace aiverse::editor
