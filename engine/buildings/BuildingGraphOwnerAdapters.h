#pragma once
// Native adapters for EXISTING graph owners. No duplicate A*/HPA*/flow
// solver, no independent room topology, and no genre-specific commands.
// Register only after authoritative gameplay/physics state is committed.
#include "engine/buildings/BuildingGraphChanges.h"
#include "engine/game/MapGrid.h"
#include "engine/data/Json.h"
#include <algorithm>
#include <cstdint>
#include <functional>
#include <map>
#include <queue>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::building {

// MapGrid's existing cellChanged() already invalidates clearance,
// NavMesh, HPA* clusters and flow-field stamps. The actual grid's
// flags/blocked/etc MUST have been changed by the owning game system
// first. Building geometry is not silently projected into a 2D map.
struct BuildingGridGraphAdapter {
    // Host opts into grid.setPathTelemetryEnabled(true). These are cumulative
    // grid-wide counters, not a claim that one building caused all work.
    static Json telemetry(const game::MapGrid& grid){
        const auto& t=grid.pathTelemetry();
        Json out=Json::object();
        auto work=[](const game::MapPathWorkCounter& c){
            Json j=Json::object();
            j.set("calls",Json::string(std::to_string(c.calls)));
            j.set("items",Json::string(std::to_string(c.items)));
            j.set("lastNs",Json::string(std::to_string(c.lastNs)));
            j.set("totalNs",Json::string(std::to_string(c.totalNs)));return j;
        };
        out.set("scope",Json::string("grid-wide cumulative"));
        out.set("cellInvalidations",Json::string(std::to_string(t.cellInvalidations)));
        out.set("dirtyFlatHpaClusters",Json::number(int64_t(grid.dirtyFlatHpaClusters())));
        out.set("cachedFlowFields",Json::number(int64_t(grid.flowCount())));
        out.set("flatHpa",work(t.flatHpa));
        out.set("fullFlowFields",work(t.flowFull));
        out.set("flowClusterBlocks",work(t.flowCluster));
        return out;
    }
    using CommittedProjection=std::function<bool(const BuildingGraphChange&)>;
    static BuildingGraphChangeRouter::Consumer consumer(
        game::MapGrid& grid,CommittedProjection committed,
        uint32_t maxTouchedCells=4096){
        return [&grid,committed=std::move(committed),maxTouchedCells](
            const BuildingGraphChange& change)->bool{
            if(!committed||!committed(change)||grid.sphere()||
               grid.w<=0||grid.h<=0||maxTouchedCells==0)return false;
            // MapGrid coordinates are integer world X/Z on FLAT maps.
            // If the map uses a different projection, caller supplies an
            // alternative existing-owner adapter instead.
            const auto& b=change.bounds;
            if(b.max.x<b.min.x||b.max.z<b.min.z)return false;
            const int64_t minX=b.min.x.floorInt()-1;
            const int64_t minZ=b.min.z.floorInt()-1;
            const int64_t maxX=b.max.x.floorInt()+1;
            const int64_t maxZ=b.max.z.floorInt()+1;
            if(maxX<0||maxZ<0||minX>=grid.w||minZ>=grid.h)
                return true; // no affected flat-map columns
            const int x0=int(std::clamp<int64_t>(minX,0,grid.w-1));
            const int z0=int(std::clamp<int64_t>(minZ,0,grid.h-1));
            const int x1=int(std::clamp<int64_t>(maxX,0,grid.w-1));
            const int z1=int(std::clamp<int64_t>(maxZ,0,grid.h-1));
            const int64_t count=int64_t(x1-x0+1)*int64_t(z1-z0+1);
            if(count>maxTouchedCells){
                // Broad source replacement: invalidates HPA*/nav pathing
                // AND throws away flow caches. No claim of local refresh.
                grid.invalidatePathing();
                grid.flowDropAll();
                return true;
            }
            for(int z=z0;z<=z1;++z)
                for(int x=x0;x<=x1;++x)
                    grid.cellChanged(x,z);
            return true;
        };
    }
};

// A derived connectivity cache for the generator's EXISTING room
// connections. Game collision/door/ability owner decides if an edge
// is currently traversable; graph structure is never overwritten.
// Every change reevaluates edge access in deterministic graph order.
// It cannot grant access merely because a network message says "open".
class BuildingRoomGraphCache {
public:
    using CanTraverse=std::function<bool(const BuildingGraphConnection&)>;
    bool initialize(const BuildingStructureGraph& graph,CanTraverse allowed,
                    std::string* error=nullptr){
        if(!graph||!allowed){
            if(error)*error="Room graph or authority callback unavailable";
            return false;
        }
        std::map<std::string,size_t> roomIndex;
        for(size_t i=0;i<graph.rooms.size();++i)
            if(graph.rooms[i].id.empty()||
               !roomIndex.emplace(graph.rooms[i].id,i).second){
                if(error)*error="Duplicate/empty building room ID";
                return false;
            }
        std::vector<Edge> edges;
        edges.reserve(graph.connections.size());
        for(const auto& link:graph.connections){
            auto a=roomIndex.find(link.from),b=roomIndex.find(link.to);
            if(a==roomIndex.end()||b==roomIndex.end()||a->second==b->second){
                if(error)*error="Room connection references invalid room";
                return false;
            }
            edges.push_back({a->second,b->second,link});
        }
        std::vector<std::vector<size_t>> adjacent(graph.rooms.size());
        for(const auto& edge:edges)if(allowed(edge.source)){
            adjacent[edge.from].push_back(edge.to);
            adjacent[edge.to].push_back(edge.from);
        }
        for(auto& v:adjacent){
            std::sort(v.begin(),v.end());
            v.erase(std::unique(v.begin(),v.end()),v.end());
        }
        rooms_=std::move(roomIndex);edges_=std::move(edges);
        adjacent_=std::move(adjacent);allowed_=std::move(allowed);
        revision_=1;changedRooms_=0;activeEdges_=0;
        for(const auto& row:adjacent_)activeEdges_+=row.size();
        activeEdges_/=2;
        if(error)error->clear();
        return true;
    }
    bool invalidate(const BuildingGraphChange& change){
        if(!allowed_||change.revision==0||change.sceneId==0)return false;
        // A complete candidate is built before publication. The
        // authoritative predicate reads the already committed scene.
        std::vector<std::vector<size_t>> next(adjacent_.size());
        for(const auto& edge:edges_)if(allowed_(edge.source)){
            next[edge.from].push_back(edge.to);
            next[edge.to].push_back(edge.from);
        }
        for(auto& v:next){
            std::sort(v.begin(),v.end());
            v.erase(std::unique(v.begin(),v.end()),v.end());
        }
        changedRooms_=0;activeEdges_=0;
        for(size_t i=0;i<next.size();++i){if(next[i]!=adjacent_[i])++changedRooms_;activeEdges_+=next[i].size();}
        activeEdges_/=2;
        adjacent_=std::move(next);
        revision_=change.revision;
        return true;
    }
    BuildingGraphChangeRouter::Consumer consumer(){
        return [this](const BuildingGraphChange& change){
            return invalidate(change);
        };
    }
    bool reachable(const std::string& from,const std::string& to)const{
        const auto a=rooms_.find(from),b=rooms_.find(to);
        if(a==rooms_.end()||b==rooms_.end())return false;
        std::vector<uint8_t> visited(adjacent_.size(),0);
        std::queue<size_t> pending;
        visited[a->second]=1;pending.push(a->second);
        while(!pending.empty()){
            const auto i=pending.front();pending.pop();
            if(i==b->second)return true;
            for(size_t next:adjacent_[i])if(!visited[next]){
                visited[next]=1;pending.push(next);
            }
        }
        return false;
    }
    uint64_t revision()const noexcept{return revision_;}
    size_t roomCount()const noexcept{return rooms_.size();}
    Json telemetry()const{
        Json out=Json::object();
        out.set("revision",Json::string(std::to_string(revision_)));
        out.set("roomCount",Json::number(int64_t(rooms_.size())));
        out.set("authoredConnections",Json::number(int64_t(edges_.size())));
        out.set("activeUniqueRoomPairs",Json::number(int64_t(activeEdges_)));
        out.set("changedRoomAdjacency",Json::number(int64_t(changedRooms_)));
        return out;
    }
private:
    struct Edge{
        size_t from=0,to=0;
        BuildingGraphConnection source;
    };
    std::map<std::string,size_t> rooms_;
    std::vector<Edge> edges_;
    std::vector<std::vector<size_t>> adjacent_;
    CanTraverse allowed_;
    uint64_t revision_=0;
    size_t changedRooms_=0,activeEdges_=0;
};

} // namespace aiverse::building
