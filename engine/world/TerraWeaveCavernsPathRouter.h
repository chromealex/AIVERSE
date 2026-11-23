#pragma once
// Stage41: solver-agnostic query router for existing A*, HPA*, FlowField,
// NavMesh, Hybrid and third-party C++ algorithms. The resolver CALLBACK
// calls the existing engine algorithm. Caverns owns no duplicate solver.
#include "engine/world/TerraWeaveCavernsNavigationPublication.h"
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::terraweave::caverns {
enum class CavePathBackend : uint8_t {
    AStar, HPAStar, FlowField, NavMesh, Hybrid, Custom
};
struct CavePathQuery {
    PointCm start{},goal{};
    CaveAgentProfile agent{};
    uint64_t sourceRevision=0;
    uint32_t maxVisits=8192;
};
struct CavePathResult {
    std::vector<PointCm> waypoints;
    uint64_t sourceRevision=0;
    uint32_t totalCost=0;
    bool reached=false;
};
class CavePathRouter {
public:
    using Solver=std::function<CavePathResult(
        const CavePathQuery&,
        std::shared_ptr<const CaveNavigationDataProvider>)>;
    // Registered backend names disambiguate multiple custom algorithms and
    // multiple policies for the SAME existing engine solver.
    bool registerSolver(std::string name,CavePathBackend kind,Solver algorithm){
        if(name.empty()||name.size()>128||!algorithm)return false;
        return solvers_.emplace(std::move(name),
                                Registered{kind,std::move(algorithm)}).second;
    }
    bool unregisterSolver(const std::string& name){
        return solvers_.erase(name)>0;
    }
    bool hasSolver(const std::string& name)const{
        return solvers_.contains(name);
    }
    CavePathResult query(
        const std::string& name,
        const CavePathQuery& query,
        const CaveNavigationPublication& publication)const {
        const auto it=solvers_.find(name);
        if(it==solvers_.end() || publication.hasPending())
            return {};
        if(!query.maxVisits||query.maxVisits>1048576)
            throw std::invalid_argument("Caverns path search visit budget");
        CaveNavigationDataProvider::validateProfile(query.agent);
        const auto provider=publication.active();
        if(!provider||provider->revision()!=query.sourceRevision)
            return {}; // obsolete query cannot use an earlier world epoch
        // Do NOT invent a route here. Invoke the registered engine solver.
        auto result=it->second.solve(query,provider);
        if(result.sourceRevision!=query.sourceRevision)
            return {};
        return result;
    }
    size_t registeredCount()const noexcept{return solvers_.size();}
    CavePathBackend backend(const std::string& name)const {
        const auto it=solvers_.find(name);
        if(it==solvers_.end())
            throw std::out_of_range("Caverns unknown route backend");
        return it->second.kind;
    }
private:
    struct Registered{
        CavePathBackend kind;
        Solver solve;
    };
    std::map<std::string,Registered> solvers_;
};
} // namespace aiverse::terraweave::caverns
