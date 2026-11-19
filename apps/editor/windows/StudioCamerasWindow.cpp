// AIVERSE Studio: project-authored ECS cameras. This preview World is separate
// from the gameplay simulation; cameras.json can be loaded by any native game.
// Generic Lens components only, no RTS/FPS camera type or renderer dependency.
#include "../Editor.h"
#include "../EditorUtil.h"
#include "engine/sim/CameraSceneAsset.h"
#include "engine/render/CameraCaptureViews.h"
#include "engine/render/EcsCameraRender.h"
#include "engine/render/CameraRig.h"
#include "engine/render/CameraFrameRecorder.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace aiverse::editor {
namespace {
Fixed fixedFromUi(double value) {
    if(!std::isfinite(value))return {};
    const double bounded=std::clamp(value,-1000000.,1000000.);
    return Fixed::fromRaw(int64_t(std::llround(bounded*Fixed::kOne)));
}
Rotation rotationFromCamera(const render::Camera& camera) {
    const auto q=render::cameraPoseRotation(camera);
    return {fixedFromUi(q.x),fixedFromUi(q.y),fixedFromUi(q.z),fixedFromUi(q.w)};
}
}
class StudioCamerasWindow final : public EditorWindow {
public:
    const char* id()const override{return "lens_cameras";}
    const char* title()const override{return "LENS / Cameras";}
    const char* group()const override{return "scene";}
    const char* dockArea()const override{return "right-bottom";}
    bool openByDefault()const override{return false;}
    int order()const override{return 91;}
    void shutdownAfterGpuIdle(Editor& ed) override {
        recorder_.stop();
        if(preview_.color.valid()||preview_.depth.valid())preview_.release(ed.device());
        for(auto& retired:retiredPreviews_)retired.viewport.release(ed.device());
        retiredPreviews_.clear();
        device_=nullptr;
    }
    void draw(Editor& ed) override {
        collectRetiredPreviews(ed.device());
        const std::string project=ed.ws().project.dir;
        if(project.empty()) {
            ImGui::TextDisabled("Open a project to edit camera entities.");
            return;
        }
        if(project_!=project) {
            recorder_.stop();
            captureStatus_=std::make_shared<CaptureStatus>();
            if(device_)retirePreview(*device_);
            device_=&ed.device();project_=project;
            world_=std::make_unique<World>();
            hierarchy_=std::make_unique<SceneHierarchy>();
            cameras_.clear();selected_={};
            load();
        }
        ImGui::TextDisabled("ECS Camera entities — cameras.json (game/project asset)");
        if(ImGui::Button("New Camera"))createFromScene(ed.sceneCam);
        ImGui::SameLine();
        if(ImGui::Button("Save"))save();
        ImGui::SameLine();
        if(ImGui::Button("Reload"))load();
        if(!error_.empty())ImGui::TextWrapped("%s",error_.c_str());
        ImGui::Separator();
        for(Entity entity:cameras_) {
            if(!world_->alive(entity))continue;
            const auto* name=world_->tryGetConst<CameraNameComponent>(entity);
            ImGui::PushID(int(entity.index));
            const std::string title=name?name->name:"Camera";
            if(ImGui::Selectable(title.c_str(),entity==selected_))selected_=entity;
            ImGui::PopID();
        }
        if(!world_||!world_->alive(selected_))return;
        auto* camera=world_->tryGet<CameraComponent>(selected_);
        auto* output=world_->tryGet<CameraOutputComponent>(selected_);
        auto* update=world_->tryGet<CameraUpdateComponent>(selected_);
        auto* vp=world_->tryGet<CameraViewportComponent>(selected_);
        auto* rig=world_->tryGet<CameraRigComponent>(selected_);
        auto* lens=world_->tryGet<CameraLensComponent>(selected_);
        auto* stack=world_->tryGet<CameraStackComponent>(selected_);
        if(!camera||!output||!update||!vp)return;
        ImGui::Separator();
        bool enabled=camera->enabled;
        if(ImGui::Checkbox("Enabled",&enabled))camera->enabled=enabled;
        bool ortho=camera->projection==CameraProjection::Orthographic;
        if(ImGui::Checkbox("Orthographic",&ortho))
            camera->projection=ortho?CameraProjection::Orthographic:CameraProjection::Perspective;
        float fov=float(camera->verticalFov.toDouble())*180.f/render::cameraPi;
        if(ImGui::SliderFloat("Vertical FOV",&fov,5,150,"%.1f deg"))
            camera->verticalFov=fixedFromUi(fov*render::cameraPi/180.);
        float nearPlane=float(camera->nearPlane.toDouble()),farPlane=float(camera->farPlane.toDouble());
        if(ImGui::DragFloat("Near",&nearPlane,.01f,.01f,1000.f))
            camera->nearPlane=fixedFromUi(std::min(nearPlane,farPlane-.01f));
        if(ImGui::DragFloat("Far",&farPlane,1.f,.05f,100000.f))
            camera->farPlane=fixedFromUi(std::max(farPlane,nearPlane+.01f));
        int priority=camera->priority;
        if(ImGui::DragInt("Priority",&priority,1,-10000,10000))camera->priority=priority;
        int targetType=output->kind==CameraOutputKind::Texture?1:0;
        if(ImGui::Combo("Output",&targetType,"Screen\0Render Texture\0"))
            output->kind=targetType?CameraOutputKind::Texture:CameraOutputKind::Screen;
        if(targetType) {
            int key=int(std::min<uint64_t>(output->targetAssetKey,INT32_MAX));
            if(ImGui::InputInt("RT Asset Key",&key))
                output->targetAssetKey=uint64_t(std::max(1,key));
            int w=output->width,h=output->height;
            if(ImGui::InputInt("Width",&w))output->width=uint16_t(std::clamp(w,1,8192));
            if(ImGui::InputInt("Height",&h))output->height=uint16_t(std::clamp(h,1,8192));
            ImGui::Checkbox("HDR",&output->hdr);
            ImGui::Checkbox("Depth",&output->depth);
        }
        int policy=int(update->mode);
        if(ImGui::Combo("Refresh",&policy,"Every Frame\0Every N Frames\0Fixed Hz\0On Dirty\0Manual\0"))
            update->mode=CameraUpdateMode(policy);
        int n=update->everyNFrames;
        if(update->mode==CameraUpdateMode::EveryNFrames && ImGui::InputInt("Interval",&n))
            update->everyNFrames=uint16_t(std::clamp(n,1,65535));
        int hz=update->rateHz;
        if(update->mode==CameraUpdateMode::FixedRate && ImGui::InputInt("Hz",&hz))
            update->rateHz=uint16_t(std::clamp(hz,1,240));
        ImGui::Checkbox("Custom viewport",&vp->enabled);
        if(vp->enabled) {
            float x=float(vp->x.toDouble()),y=float(vp->y.toDouble());
            float w=float(vp->width.toDouble()),h=float(vp->height.toDouble());
            if(ImGui::SliderFloat("Viewport X",&x,0,1))vp->x=fixedFromUi(x);
            if(ImGui::SliderFloat("Viewport Y",&y,0,1))vp->y=fixedFromUi(y);
            if(ImGui::SliderFloat("Viewport W",&w,.01f,1))vp->width=fixedFromUi(w);
            if(ImGui::SliderFloat("Viewport H",&h,.01f,1))vp->height=fixedFromUi(h);
        }
        if(stack && ImGui::CollapsingHeader("Camera Composition")) {
            ImGui::Checkbox("Overlay",&stack->overlay);
            int order=stack->order;
            if(ImGui::DragInt("Stack Order",&order,1,-1000,1000))
                stack->order=int16_t(std::clamp(order,-1000,1000));
            float opacity=float(stack->opacity.toDouble());
            if(ImGui::SliderFloat("Opacity",&opacity,0.f,1.f))
                stack->opacity=fixedFromUi(opacity);
        }
        if(rig && ImGui::CollapsingHeader("Camera Rig")) {
            ImGui::Checkbox("Rig Enabled",&rig->enabled);
            int mode=int(rig->mode);
            if(ImGui::Combo("Rig Mode",&mode,"Static\0Look At\0Follow\0Orbit\0Rail\0"))
                rig->mode=CameraRigMode(mode);
            float offset[3]={float(rig->offset.x.toDouble()),
                             float(rig->offset.y.toDouble()),float(rig->offset.z.toDouble())};
            if(ImGui::DragFloat3("Rig Offset",offset,.05f))
                rig->offset={fixedFromUi(offset[0]),fixedFromUi(offset[1]),fixedFromUi(offset[2])};
            float distance=float(rig->distance.toDouble());
            if(ImGui::DragFloat("Rig Distance",&distance,.05f,.01f,100000.f))
                rig->distance=fixedFromUi(distance);
            float yaw=float(rig->yaw.toDouble()),pitch=float(rig->pitch.toDouble());
            if(ImGui::SliderFloat("Rig Yaw",&yaw,-render::cameraPi,render::cameraPi))
                rig->yaw=fixedFromUi(yaw);
            if(ImGui::SliderFloat("Rig Pitch",&pitch,-1.55f,1.55f))
                rig->pitch=fixedFromUi(pitch);
            float smoothing=float(rig->smoothing.toDouble());
            if(ImGui::DragFloat("Rig Smoothing",&smoothing,.05f,0.f,100.f))
                rig->smoothing=fixedFromUi(smoothing);
            ImGui::Checkbox("Track Target",&rig->trackTarget);
            ImGui::TextDisabled("Follow/Orbit/LookAt require a runtime-bound target.");
            ImGui::TextDisabled("Rail control points are supplied by the runtime.");
        }
        if(lens && ImGui::CollapsingHeader("Physical Lens")) {
            ImGui::Checkbox("Use Physical FOV",&lens->physicalFov);
            const auto editPositive=[&](const char* label,Fixed& field,float speed,float minimum,float maximum){
                float value=float(field.toDouble());
                if(ImGui::DragFloat(label,&value,speed,minimum,maximum))
                    field=fixedFromUi(std::clamp(value,minimum,maximum));
            };
            editPositive("Focal Length (mm)",lens->focalLengthMm,.1f,.1f,1000.f);
            editPositive("Sensor Height (mm)",lens->sensorHeightMm,.1f,.1f,100.f);
            editPositive("F Number",lens->fNumber,.05f,.1f,128.f);
            editPositive("Focus Distance",lens->focusDistance,.05f,.01f,100000.f);
            editPositive("Shutter (s)",lens->shutterSeconds,.0001f,.00001f,60.f);
            editPositive("ISO",lens->iso,1.f,1.f,204800.f);
        }
        if(output->kind==CameraOutputKind::Texture) {
            ImGui::Separator();
            ImGui::TextDisabled("RenderTexture key: %llu",static_cast<unsigned long long>(output->targetAssetKey));
            ImGui::TextDisabled("Capture/export uses the runtime LENS pipeline.");
            ImGui::TextDisabled("HDR export: resolve to SDR RGBA8 before PNG readback.");
        }
        if(ImGui::Button("Use in Scene"))useInScene(ed);
        ImGui::SameLine();
        if(ImGui::Button("Delete Camera")) {
            hierarchy_->detach(*world_,selected_);
            world_->destroy(selected_);
            cameras_.erase(std::remove(cameras_.begin(),cameras_.end(),selected_),cameras_.end());
            selected_={};return;
        }
        hierarchy_->update(*world_);
        render::EcsCameraView view;
        if(render::resolveEcsCamera(*world_,selected_,view)) {
            const float side=std::clamp(ImGui::GetContentRegionAvail().x,128.f,400.f);
            // Keep the texture allocated at its original size while a GPU
            // readback is pending; resizing would retire the capture source.
            if(!recorder_.pending()) {
                const int requested=std::max(8,int(side));
                if(preview_.color.valid() && (preview_.w!=requested || preview_.h!=requested))
                    retirePreview(ed.device());
                preview_.ensure(ed.device(),requested,requested);
            }
            if(lens && lens->physicalFov && render::validCameraLens(*lens)) {
                const float sensor=float(lens->sensorHeightMm.toDouble());
                const float focal=float(lens->focalLengthMm.toDouble());
                view.camera.fovY=2.f*std::atan(sensor/(2.f*focal));
            }
            ed.renderScene(preview_,view.camera,false);
            ImGui::TextDisabled("Live GPU RenderTexture preview (%d x %d)",preview_.w,preview_.h);
            preview_.image(ed.device(),side,side);
            ImGui::Separator();
            if(ImGui::Button("Capture PNG") && !recorder_.pending()) {
                const std::filesystem::path directory=std::filesystem::path(project_)/"captures"/"lens";
                const auto state=captureStatus_;
                const uint64_t stableId=[&]{
                    const auto* name=world_->tryGetConst<CameraNameComponent>(selected_);
                    return name?name->stableId:uint64_t(selected_.packed());
                }();
                recorder_.start(1.0,[state,directory,stableId](render::CameraCapturedFrame frame){
                    std::error_code ec;
                    std::filesystem::create_directories(directory,ec);
                    if(ec || !frame.valid()) {
                        state->message=ec?ec.message():"Camera capture returned no pixels";
                        return;
                    }
                    const auto file=directory/("camera-"+std::to_string(stableId)+".png");
                    state->message=frame.writePng(file.string())?
                        ("Saved "+file.string()):("Cannot write "+file.string());
                });
                if(!recorder_.tickTexture(ed.device(),preview_.color,ed.time()))
                    captureStatus_->message="Offscreen RT readback unsupported or busy";
                else captureStatus_->message="Camera capture queued";
            }
            if(recorder_.pending())ImGui::TextDisabled("GPU readback pending...");
            if(captureStatus_&&!captureStatus_->message.empty())
                ImGui::TextWrapped("%s",captureStatus_->message.c_str());
        }
    }
private:
    struct CaptureStatus { std::string message; };
    std::shared_ptr<CaptureStatus> captureStatus_=std::make_shared<CaptureStatus>();
    render::CameraFrameRecorder recorder_;
    std::string project_,error_;
    std::unique_ptr<World> world_;
    std::unique_ptr<SceneHierarchy> hierarchy_;
    std::vector<Entity> cameras_;
    Entity selected_{};
    gfx::Device* device_=nullptr;
    Viewport preview_;
    struct RetiredPreview {
        Viewport viewport;
        uint64_t serial=0;
    };
    std::vector<RetiredPreview> retiredPreviews_;
    // A source texture can still be referenced by already encoded GPU work.
    // The offscreen capture callback may outlive a project switch.
    void retirePreview(gfx::Device& device) {
        if(!preview_.color.valid()&&!preview_.depth.valid())return;
        retiredPreviews_.push_back({preview_,device.frameSerial()});
        preview_={};
    }
    void collectRetiredPreviews(gfx::Device& device) {
        // A device with no completion serial cannot safely reclaim by age.
        const uint64_t completed=device.completedFrameSerial();
        if(!completed)return;
        for(auto it=retiredPreviews_.begin();it!=retiredPreviews_.end();) {
            if(it->serial!=0 && completed>=it->serial) {
                it->viewport.release(device);
                it=retiredPreviews_.erase(it);
            } else ++it;
        }
    }
    uint64_t nextId_=1;

    std::filesystem::path assetPath()const{
        return std::filesystem::path(project_)/"cameras.json";
    }
    void createFromScene(const render::Camera& camera) {
        if(!world_||!hierarchy_)return;
        const Entity e=world_->create();
        LocalTransform local;
        const auto eye=camera.eye();
        local.position={fixedFromUi(eye.x),fixedFromUi(eye.y),fixedFromUi(eye.z)};
        local.rotation=rotationFromCamera(camera);
        if(!hierarchy_->attach(*world_,e,local))return;
        CameraComponent comp;
        comp.verticalFov=fixedFromUi(camera.fovY);
        comp.nearPlane=fixedFromUi(camera.zNear);
        comp.farPlane=fixedFromUi(camera.zFar);
        comp.projection=camera.ortho?CameraProjection::Orthographic:CameraProjection::Perspective;
        comp.orthographicHalfHeight=fixedFromUi(camera.orthoHalfHeight());
        world_->add<CameraComponent>(e,comp);
        world_->add<CameraOutputComponent>(e,CameraOutputComponent{});
        world_->add<CameraUpdateComponent>(e,CameraUpdateComponent{});
        world_->add<CameraViewportComponent>(e,CameraViewportComponent{});
        world_->add<CameraRigComponent>(e,CameraRigComponent{});
        world_->add<CameraLensComponent>(e,CameraLensComponent{});
        world_->add<CameraStackComponent>(e,CameraStackComponent{});
        world_->add<CameraNameComponent>(e,CameraNameComponent{nextId_,"Camera "+std::to_string(nextId_)});
        ++nextId_;cameras_.push_back(e);selected_=e;
        hierarchy_->update(*world_);
    }
    void load() {
        error_.clear();
        auto replacement=std::make_unique<World>();
        auto hierarchy=std::make_unique<SceneHierarchy>();
        std::vector<Entity> entities;
        try {
            if(std::filesystem::exists(assetPath())) {
                const Json data=Json::parseFile(assetPath().string());
                if(!loadCameraScene(*replacement,*hierarchy,data,&entities,&error_))return;
            }
        } catch(const std::exception& e) {error_=e.what();return;}
        world_=std::move(replacement);hierarchy_=std::move(hierarchy);
        cameras_=std::move(entities);
        selected_=cameras_.empty()?Entity{}:cameras_.front();
        nextId_=1;
        for(Entity e:cameras_)
            if(const auto* name=world_->tryGetConst<CameraNameComponent>(e))
                nextId_=std::max(nextId_,name->stableId+1);
    }
    void save(){
        if(!world_||project_.empty())return;
        error_.clear();
        try {
            const auto dest=assetPath();
            const auto temp=dest.string()+".tmp";
            std::ofstream out(temp,std::ios::trunc);
            if(!out){error_="Cannot write cameras.json";return;}
            out<<cameraSceneToJson(*world_).dump(2);
            out.flush();
            if(!out){error_="Camera save failed";return;}
            out.close();
            std::error_code ec;
            std::filesystem::rename(temp,dest,ec);
            if(ec){
                std::filesystem::remove(dest,ec);
                ec.clear();std::filesystem::rename(temp,dest,ec);
            }
            if(ec)error_=ec.message();
        }catch(const std::exception& e){error_=e.what();}
    }
    void useInScene(Editor& ed) {
        render::EcsCameraView v;
        if(!render::resolveEcsCamera(*world_,selected_,v))return;
        const auto eye=v.camera.eye(),f=v.camera.rotation.rotate({0,0,-1});
        auto& cam=ed.sceneCam;
        cam.usePose=false;cam.distance=std::clamp(12.f,cam.minDistance,cam.maxDistance);
        cam.target=eye+f*cam.distance;
        const auto toEye=eye-cam.target;
        cam.yaw=std::atan2(toEye.x,toEye.z);
        cam.pitch=std::asin(std::clamp(toEye.y/std::max(.01f,cam.distance),-1.f,1.f));
        cam.fovY=v.camera.fovY;cam.ortho=v.camera.ortho;
        ed.focusWindow("scene");
    }
};
AIVERSE_EDITOR_WINDOW(StudioCamerasWindow);
} // namespace aiverse::editor
