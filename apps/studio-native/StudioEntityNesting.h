#pragma once
#include "apps/editor/core/WorldInspect.h"
#include "engine/sim/CommonComponents.h"
#include <unordered_set>
#include <unordered_map>

namespace aiverse::studio {
inline uint64_t nestingParent(const editor::WorldRow& row, const World& world) {
    if (row.parent) return row.parent;
    const auto* parent = world.tryGetConst<Parent>(row.e);
    return parent ? editor::worldRef(parent->value) : 0;
}

struct NestedEntityRow {
    const editor::WorldRow* entity;
    uint64_t parent;
    size_t depth, children;
};

// Missing parents become roots. A cycle gets one deterministic root and every
// matched entity remains reachable. Iteration avoids consuming stack per level.
inline std::vector<NestedEntityRow> nestedEntities(const std::vector<const editor::WorldRow*>& matched,
        const World& world, const std::unordered_set<uint64_t>& collapsed = {}) {
    std::unordered_map<uint64_t,uint64_t> parents;
    for (const auto* row : matched) parents.emplace(row->ref, nestingParent(*row, world));
    std::unordered_set<uint64_t> done;
    for (const auto* row : matched) {
        if (done.contains(row->ref)) continue;
        std::vector<uint64_t> chain;
        std::unordered_set<uint64_t> visiting;
        for (auto ref = row->ref; ref && !done.contains(ref); ref = parents.at(ref)) {
            if (!visiting.insert(ref).second) {
                auto start = std::find(chain.begin(),chain.end(),ref);
                parents.at(*std::min_element(start,chain.end())) = 0;
                break;
            }
            chain.push_back(ref);
            if (!parents.contains(parents.at(ref))) parents.at(ref) = 0;
        }
        done.insert(chain.begin(),chain.end());
    }
    std::unordered_map<uint64_t,std::vector<const editor::WorldRow*>> children;
    for (const auto* row : matched) children[parents.at(row->ref)].push_back(row);
    std::vector<NestedEntityRow> result, stack;
    auto pushChildren = [&](uint64_t parent,size_t depth) {
        auto it=children.find(parent);
        if(it==children.end())return;
        for(auto child=it->second.rbegin();child!=it->second.rend();++child){
            const auto next=children.find((*child)->ref);
            stack.push_back({*child,parent,depth,next==children.end()?0:next->second.size()});
        }
    };
    pushChildren(0,0);
    while(!stack.empty()){
        const auto row=stack.back();stack.pop_back();
        result.push_back(row);
        if(row.children && !collapsed.contains(row.entity->ref))pushChildren(row.entity->ref,row.depth+1);
    }
    return result;
}
}
