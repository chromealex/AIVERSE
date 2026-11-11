#include "Bench.h"
int rtsMain(int argc,char** argv) {
    try {
        rts::bench::Bench bench(rts::bench::Options::parse(argc,argv));
        bench.ownsFrame=false; // platform::run wraps the callback in beginFrame/endFrame
        rts::gfx::Device* device=nullptr;
        rts::platform::AppConfig c;c.title="AIVERSE — Sponza benchmark";c.width=bench.width;c.height=bench.height;c.highDpi=false;c.resizable=false;
        rts::platform::AppCallbacks cb;
        cb.init=[&](rts::gfx::Device& d){device=&d;bench.init(d);};
        cb.frame=[&](rts::gfx::Device& d,double){bench.draw(d);if(bench.done)rts::platform::requestQuit();};
        cb.shutdown=[&]{bench.shutdown(*device);};
        rts::platform::run(c,cb);return bench.failed?1:0;
    }catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}
}
