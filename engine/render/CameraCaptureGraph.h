// AIVERSE LENS: explicit dependencies for recursive captures (portal/mirror).
// The plan is CPU-only. A cycle or excessive recursive depth cannot reach GPU.
#pragma once
#include "CameraCaptureViews.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <map>
#include <set>
#include <vector>

namespace aiverse::render {
enum class CameraCaptureIssue : uint8_t {
    InvalidTarget, DuplicateTarget, InvalidInput, MissingInput,
    DependencyCycle, RecursionLimit
};
struct CameraCaptureNode {
    CameraCaptureView capture{};
    std::array<uint64_t,8> samples{};
    uint8_t sampleCount=0;
    uint8_t recursionDepth=0;
};
struct CameraCaptureDiagnostic {
    uint64_t targetKey=0;
    CameraCaptureIssue issue=CameraCaptureIssue::InvalidTarget;
};
struct CameraCaptureGraphPlan {
    std::vector<CameraCaptureView> ordered;
    std::vector<CameraCaptureDiagnostic> diagnostics;
};
inline CameraCaptureGraphPlan makeCameraCaptureGraphPlan(
        const std::vector<CameraCaptureNode>& input,unsigned maxDepth=4) {
    CameraCaptureGraphPlan result;
    const size_t n=input.size();
    if(n>256) {
        result.diagnostics.push_back({0,CameraCaptureIssue::RecursionLimit});
        return result;
    }
    std::map<uint64_t,std::vector<size_t>> owners;
    std::vector<bool> invalid(n,false);
    auto reject=[&](size_t i,CameraCaptureIssue reason) {
        if(!invalid[i])result.diagnostics.push_back({input[i].capture.targetAssetKey,reason});
        invalid[i]=true;
    };
    for(size_t i=0;i<n;++i) {
        const auto& node=input[i];
        if(node.recursionDepth>maxDepth)reject(i,CameraCaptureIssue::RecursionLimit);
        if(!node.capture.targetAssetKey || node.capture.width<=0 ||
           node.capture.height<=0 || node.sampleCount>node.samples.size())
            reject(i,CameraCaptureIssue::InvalidTarget);
        if(node.capture.targetAssetKey)owners[node.capture.targetAssetKey].push_back(i);
    }
    std::map<uint64_t,size_t> producers;
    for(const auto& [key,indices]:owners){
        if(indices.size()!=1){
            for(size_t i:indices)reject(i,CameraCaptureIssue::DuplicateTarget);
        } else if(!invalid[indices[0]])producers.emplace(key,indices[0]);
    }
    std::vector<std::vector<size_t>> edges(n);
    std::vector<size_t> indegree(n,0);
    for(size_t i=0;i<n;++i) {
        if(invalid[i])continue;
        std::set<uint64_t> unique;
        const auto& node=input[i];
        for(size_t k=0;k<node.sampleCount;++k) {
            const uint64_t dep=node.samples[k];
            if(!dep||!unique.insert(dep).second||dep==node.capture.targetAssetKey){
                reject(i,CameraCaptureIssue::InvalidInput);break;
            }
            const auto iter=producers.find(dep);
            if(iter==producers.end()){
                reject(i,CameraCaptureIssue::MissingInput);break;
            }
            edges[iter->second].push_back(i);
            ++indegree[i];
        }
    }
    std::set<std::pair<uint64_t,size_t>> ready;
    for(size_t i=0;i<n;++i)
        if(!invalid[i] && indegree[i]==0)ready.insert({input[i].capture.targetAssetKey,i});
    std::vector<size_t> order;
    while(!ready.empty()) {
        const size_t i=ready.begin()->second;
        ready.erase(ready.begin());order.push_back(i);
        for(size_t child:edges[i])
            if(--indegree[child]==0&&!invalid[child])
                ready.insert({input[child].capture.targetAssetKey,child});
    }
    for(size_t i=0;i<n;++i)
        if(!invalid[i] && indegree[i]>0)
            reject(i,CameraCaptureIssue::DependencyCycle);
    // Reject all consumers of invalid producers (even when they are not part
    // of a cycle themselves). Nothing reads a known bad output.
    bool changed=true;
    while(changed) {
        changed=false;
        for(size_t i=0;i<n;++i) {
            if(invalid[i])continue;
            for(size_t k=0;k<input[i].sampleCount;++k) {
                const auto it=producers.find(input[i].samples[k]);
                if(it==producers.end()||invalid[it->second]) {
                    reject(i,CameraCaptureIssue::MissingInput);
                    changed=true;break;
                }
            }
        }
    }
    for(size_t i:order)
        if(!invalid[i])result.ordered.push_back(input[i].capture);
    return result;
}
} // namespace aiverse::render
