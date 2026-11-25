#pragma once
#include "apps/fps/HostedActorClient.h"
#include "apps/client/HostedClient.h"
#include "apps/studio-native/StudioGameLaunch.h"
#include "engine/data/Json.h"
#include "apps/studio-native/StudioGameDevice.h"
#include "apps/studio-native/StudioRuntimeInspect.h"
#include "apps/studio-native/StudioWorldProcessDiagnostics.h"
#include "apps/studio-native/StudioWorldProcessLiveOptions.h"
#include "apps/studio-native/StudioForestCameraFocus.h"

namespace aiverse::studio {
// Independent native surface; the authoring Scene keeps its own device/epoch.
class StudioGameSurface final {
public:
    explicit StudioGameSurface(std::unique_ptr<gfx::Device> device);
    ~StudioGameSurface();
    void start(const StudioGameLaunch& plan,const std::string& engineAssets);
    void stop();
    void setDevice(const StudioGameDevice& device);
    void control(const std::string& action);
    void frame(int width,int height,float pointsW,float pointsH,double dt,bool focused);
    void suspend();
    void event(const platform::Event& event);
    Json scriptLog(bool errorsOnly) const;
    Json state(bool includeScriptErrors = false) const;
    std::function<Json(uint64_t)> buildingGraphReader();
    Json regions() const;
    Json profile(const std::string& action,uint32_t steps);
    Json entities(int offset,int limit,const std::string& query,const StudioRuntimeInspect::GroupQuery& group);
    void selectEntity(const std::string& epoch,const std::string& ref);
    Json inspect(const std::string& epoch,const std::string& ref);
    Json editRuntime(const Json& params);
    // Keep play changes: where each map entity is now (index into the map's entities), or removed:true.
    Json mapEntityStates();
    Json ecologyState(const StudioWorldProcessLiveOptions& options={},const std::string& focusBody={},const std::optional<StudioForestCameraFocus>& camera={},Json* forestRender=nullptr);
    Json planetForestState(const std::string& focusBody={},const std::optional<StudioForestCameraFocus>& camera={});
    bool planetMatch() const;
    bool launchedMapCurrent() const;
    bool planetSnapshotCurrent(const Json& snapshot) const;
    Json worldProcessDiagnostics(int cap,const std::string& focus);
    const StudioGameLaunch& launchPlan() const{return launch_;}
    bool canReview(const editor::Workspace& ws) const;
private:
    std::unique_ptr<gfx::Device> device_;
    std::unique_ptr<render::RenderDeviceLifetime> lifetime_;
    Simulation* liveSimulation() const;
    bool paused() const;
    bool canControl() const;
    bool canStep() const;
    std::unique_ptr<client::HostedClient> host_;
    std::unique_ptr<client::HostedActorClient> actor_;
    std::string error_, assets_;
    StudioGameLaunch launch_;
    StudioGameDevice profile_;
    float inputScale_=1;
    StudioRuntimeInspect inspector_;
    std::vector<uint64_t> setupRefs_;
    StudioWorldProcessDiagnostics worldProcessDiagnostics_;
    uint64_t runtimeEpoch_=0;
    bool matchObserved_=false;
    bool runtimeDebugEdited_=false;
};
}
