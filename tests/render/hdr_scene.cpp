#include "HdrScene.h"
#include "engine/platform/Platform.h"
int rtsMain(int, char**) {
    rts::test::HdrScene scene;scene.ownsFrame=false;
    rts::gfx::Device* device=nullptr;
    rts::platform::AppConfig cfg; cfg.title="HDR validation";cfg.width=960;cfg.height=640;cfg.highDpi=false;
    rts::platform::AppCallbacks cb;
    cb.init=[&](rts::gfx::Device& d){device=&d;scene.init(d);};
    cb.frame=[&](rts::gfx::Device& d,double){scene.draw(d,rts::platform::framebufferWidth(),rts::platform::framebufferHeight());if(scene.captured||scene.failed||scene.frame>60)rts::platform::requestQuit();};
    cb.shutdown=[&]{scene.shutdown(*device);};
    rts::platform::run(cfg,cb);
    return scene.failed||!scene.captured?1:0;
}
