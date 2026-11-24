#pragma once
#include "apps/client/HostedClient.h"
#include "apps/studio-native/StudioGameLaunch.h"
#include "engine/data/Json.h"
#include "apps/studio-native/StudioGameDevice.h"
#include "apps/studio-native/StudioRuntimeInspect.h"

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
    Json state() const;
    Json regions() const;
    Json profile(const std::string& action,uint32_t steps);
    Json entities(int offset,int limit,const std::string& query);
    Json inspect(const std::string& epoch,const std::string& ref);
    Json editRuntime(const Json& params);
    // Keep play changes: where each map entity is now (index into the map's entities), or removed:true.
    Json mapEntityStates();
private:
    std::unique_ptr<gfx::Device> device_;
    std::unique_ptr<render::RenderDeviceLifetime> lifetime_;
    std::unique_ptr<client::HostedClient> host_;
    std::string error_, assets_;
    StudioGameLaunch launch_;
    StudioGameDevice profile_;
    float inputScale_=1;
    StudioRuntimeInspect inspector_;
    uint64_t runtimeEpoch_=0;
    bool matchObserved_=false;
    bool runtimeDebugEdited_=false;
};
}
