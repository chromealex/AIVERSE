#pragma once
#include "StudioGameLaunch.h"
#include <vector>

namespace aiverse::studio {
inline std::vector<std::string> studioNetworkArguments(const editor::Workspace& ws,const Json& params) {
    if(!ws.project.actorSession() || ws.project.dir.empty() || params.getStr("project")!=ws.project.dir)
        throw std::runtime_error("Open the actor project again before starting network play.");
    const auto plan=studioGameLaunch(ws,true);
    std::vector<std::string> args{"--project",plan.project,"--map-index",std::to_string(*plan.map)};
    const auto mode=params.getStr("mode");
    if(mode=="listen")args.push_back("--listen");
    else if(mode=="connect"){
        const auto address=params.getStr("address");
        if(address.empty() || address.size()>2048 || std::any_of(address.begin(),address.end(),[](unsigned char c){return c<=32 || c==127;}))
            throw std::runtime_error("Enter a server address without spaces.");
        const auto scheme=address.find("://");
        if(scheme!=std::string::npos && !address.starts_with("udp://") && !address.starts_with("ws://") && !address.starts_with("wss://"))
            throw std::runtime_error("Use a UDP or WebSocket address.");
        if(scheme!=std::string::npos && scheme+3==address.size())throw std::runtime_error("Enter the server hostname.");
        args.insert(args.end(),{"--connect",address});
    }else throw std::runtime_error("Unknown multiplayer mode.");
    return args;
}
inline std::filesystem::path studioNetworkPlayer(const std::filesystem::path& installed) {
    if(std::filesystem::is_regular_file(installed))return installed;
#ifdef AIVERSE_STUDIO_GAME_CLIENT
    if(std::filesystem::is_regular_file(AIVERSE_STUDIO_GAME_CLIENT))return AIVERSE_STUDIO_GAME_CLIENT;
#endif
    throw std::runtime_error("AiverseGame is missing. Install the game client alongside Studio.");
}
}
