#pragma once
#include "StudioGameLaunch.h"
#include <algorithm>
#include <charconv>
#include <string_view>
#include <vector>

namespace aiverse::studio {
inline void validateStudioServerAddress(std::string_view address) {
    if(address.empty() || address.size()>2048 || std::any_of(address.begin(),address.end(),[](unsigned char c){return c<=32 || c==127;}))
        throw std::runtime_error("Enter a server address without spaces.");
    const auto scheme=address.find("://");
    const bool websocket=address.starts_with("ws://") || address.starts_with("wss://");
    if(scheme!=address.npos){
        if(!websocket && !address.starts_with("udp://"))throw std::runtime_error("Use a UDP or WebSocket address.");
        address.remove_prefix(scheme+3);
    }
    const auto path=address.find_first_of("/?#");
    if(path!=address.npos && !websocket)throw std::runtime_error("UDP addresses take a host and optional port.");
    auto host=address.substr(0,path);
    if(host.empty() || host.front()==':'){
        if(!host.starts_with("::"))throw std::runtime_error("Enter the server hostname.");
    }
    std::string_view port;
    if(host.starts_with("[")){
        const auto end=host.find(']');
        if(end==host.npos || end==1)throw std::runtime_error("Invalid bracketed server address.");
        host.remove_prefix(end+1);
        if(host.empty())return;
        if(host.front()!=':')throw std::runtime_error("Expected a port after the server hostname.");
        port=host.substr(1);
    }else{
        const auto colon=host.find(':');
        if(colon==host.npos || colon!=host.rfind(':'))return;
        port=host.substr(colon+1);
    }
    unsigned number=0;
    auto result=std::from_chars(port.data(),port.data()+port.size(),number);
    if(result.ec!=std::errc{} || result.ptr!=port.data()+port.size() || number==0 || number>65535)
        throw std::runtime_error("Use a server port from 1 to 65535.");
}
inline std::vector<std::string> studioNetworkArguments(const editor::Workspace& ws,const Json& params) {
    if(!ws.project.actorSession() || ws.project.dir.empty() || params.getStr("project")!=ws.project.dir)
        throw std::runtime_error("Open the actor project again before starting network play.");
    const auto plan=studioGameLaunch(ws,true);
    std::vector<std::string> args{"--project",plan.project,"--map-index",std::to_string(*plan.map)};
    const auto mode=params.getStr("mode");
    if(mode=="listen")args.push_back("--listen");
    else if(mode=="connect"){
        const auto address=params.getStr("address");
        validateStudioServerAddress(address);
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
