#include "Bench.h"
int aiverseMain(int argc,char** argv) {
    try {
        aiverse::bench::Bench bench(aiverse::bench::Options::parse(argc,argv));
        bench.ownsFrame=false; // platform::run wraps the callback in beginFrame/endFrame
        aiverse::gfx::Device* device=nullptr;
        aiverse::platform::AppConfig c;c.title="AIVERSE — Sponza benchmark";c.width=bench.width;c.height=bench.height;c.highDpi=false;c.resizable=false;
        aiverse::platform::AppCallbacks cb;
        cb.init=[&](aiverse::gfx::Device& d){device=&d;bench.init(d);};
        cb.frame=[&](aiverse::gfx::Device& d,double){bench.draw(d);if(bench.done)aiverse::platform::requestQuit();};
        cb.shutdown=[&]{bench.shutdown(*device);};
        aiverse::platform::run(c,cb);return bench.failed?1:0;
    }catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}
}
