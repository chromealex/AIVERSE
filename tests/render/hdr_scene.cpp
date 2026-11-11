#include "HdrScene.h"
#include "engine/platform/Platform.h"
int aiverseMain(int, char**) {
    aiverse::test::HdrScene scene;scene.ownsFrame=false;
    aiverse::gfx::Device* device=nullptr;
    aiverse::platform::AppConfig cfg; cfg.title="HDR validation";cfg.width=960;cfg.height=640;cfg.highDpi=false;
    aiverse::platform::AppCallbacks cb;
    cb.init=[&](aiverse::gfx::Device& d){device=&d;scene.init(d);};
    cb.frame=[&](aiverse::gfx::Device& d,double){scene.draw(d,aiverse::platform::framebufferWidth(),aiverse::platform::framebufferHeight());if(scene.captured||scene.failed||scene.frame>60)aiverse::platform::requestQuit();};
    cb.shutdown=[&]{scene.shutdown(*device);};
    aiverse::platform::run(cfg,cb);
    return scene.failed||!scene.captured?1:0;
}
