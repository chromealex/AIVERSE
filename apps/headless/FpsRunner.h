#pragma once
#include "modules/fps/FpsModule.h"
#include "modules/fps/Project.h"
// Included after Args; keeps the RTS runner's behavior and checksums unchanged.
static int playFps(const Args& args) {
    auto data=fps::readProject([&](const std::string& file,std::string& text){std::ifstream f(args.project+"/"+file);if(!f)return false;text.assign(std::istreambuf_iterator<char>(f),{});return true;});
    if(!args.map.empty())data.map=Json::parseFile((fs::path(args.project)/args.map).string());
    if(!args.record.empty()||!args.generate.empty())throw std::runtime_error("FPS runner: recording and RTS map generation are not supported");
    auto cfg=fps::readConfig(data.rules);Simulation sim({args.seed,cfg.tickRate,0},std::make_unique<fps::Module>());sim.setup(data.rules,data.map);
    auto read=[](const std::string& path){std::ifstream f(path,std::ios::binary);if(!f)throw std::runtime_error("cannot read FPS save");return std::string(std::istreambuf_iterator<char>(f),{});};
    if(!args.loadFile.empty()){std::string error;if(!sim.deserialize(read(args.loadFile),&error))throw std::runtime_error(error);}
    const auto start=std::chrono::steady_clock::now();bool saved=false,verified=false;
    uint64_t limit=uint64_t(args.maxSeconds)*cfg.tickRate;
    while(sim.tick()<limit&&!sim.result()) {
        sim.step();
        if(args.verifyRestore&&!verified&&sim.tick()>=uint64_t(args.restoreAt)*cfg.tickRate) {
            auto bytes=sim.serialize();Simulation check({args.seed,cfg.tickRate,0},std::make_unique<fps::Module>());check.setup(data.rules,data.map);
            if(!check.deserialize(bytes)||check.checksum()!=sim.checksum())throw std::runtime_error("FPS restore mismatch");
            SimSnapshot snapshot;sim.saveState(snapshot);
            for(uint32_t i=0;i<args.restoreTicks;++i){sim.step();check.step();if(sim.checksum()!=check.checksum())throw std::runtime_error("FPS continuation mismatch");}
            sim.loadState(snapshot);verified=true;
        }
        if(!args.saveFile.empty()&&!saved&&sim.tick()>=uint64_t(args.saveAt)*cfg.tickRate) {std::ofstream out(args.saveFile,std::ios::binary);out<<sim.serialize();if(!out)throw std::runtime_error("cannot write FPS save");saved=true;}
    }
    auto ms=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count();
    std::printf("Result: winner=%d ticks=%u sim=%lld ms\nChecksum: %llu\n",sim.result(),sim.tick(),(long long)ms,(unsigned long long)sim.checksum());return 0;
}
