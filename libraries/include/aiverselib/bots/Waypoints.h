#pragma once
#include "Config.h"
#include "Navigation.h"
namespace aiverse::lib::bots {
inline Waypoints readWaypoints(const Json& j){
    if(!j.isArray()||j.size()>512)throw JsonError("bots.waypoints: expected at most 512 nodes");
    Waypoints result;
    for(const auto& item:j.items()){
        keys(item,{"point","cover","links"});Waypoint node;const auto& p=item["point"];
        if(!p.isArray()||p.size()!=3)throw JsonError("bots.waypoints.point: expected [x,y,z]");
        Fixed* fields[]={&node.point.x,&node.point.y,&node.point.z};
        for(size_t i=0;i<3;++i){if(!p[i].isNumber())throw JsonError("bots.waypoints.point: expected numbers");*fields[i]=p[i].fx();}
        if(item.has("cover")&&item["cover"].type()!=Json::Type::Bool)throw JsonError("bots.waypoints.cover: expected bool");
        node.cover=item.getBool("cover",false);
        const auto& links=item["links"];if(!links.isArray())throw JsonError("bots.waypoints.links: expected array");
        for(const auto& link:links.items()){
            if(!link.isNumber()||link.fx().raw%Fixed::kOne||link.fx()<Fixed{}||link.fx()>=Fixed::fromInt(int64_t(j.size())))throw JsonError("bots.waypoints.links: invalid node index");
            node.links.push_back(size_t(link.fx().floorInt()));
        }
        std::sort(node.links.begin(),node.links.end());node.links.erase(std::unique(node.links.begin(),node.links.end()),node.links.end());result.push_back(std::move(node));
    }
    if(!valid(result))throw JsonError("bots.waypoints: unsupported coordinates");
    return result;
}
}
