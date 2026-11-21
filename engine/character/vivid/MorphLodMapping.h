// VIVID versioned, non-destructive facial LOD surface mapping sidecar.
#pragma once
#include "MorphLodSurface.h"
#include "engine/data/Json.h"
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <span>
#include <stdexcept>
#include <vector>

namespace aiverse::vivid {
struct MorphLodMapping {
    static constexpr std::uint32_t kVersion=1;
    std::uint32_t sourceVertices=0;
    std::uint32_t sourceTriangles=0;
    std::string sourceFingerprint; // hexadecimal FNV64 over source vertex bits and triangle indices
    std::vector<LodSurfaceSample> samples;
    // Model-relative vertex IDs, in the exact order of the LOD morph CSR.
    // Optional for legacy generated maps; required for Creator publication.
    std::vector<std::uint32_t> targetVertexIds;

    static std::string fingerprint(std::span<const render::float3> points,
                                   std::span<const std::uint32_t> indices) {
        if(points.empty()||indices.empty()||indices.size()%3)
            throw std::invalid_argument("VIVID LOD mapping: invalid source fingerprint input");
        std::uint64_t hash=14695981039346656037ull;
        auto add=[&](std::uint32_t value) {
            for(int i=0;i<4;++i) {
                hash^=(value>>(8*i))&0xffu;
                hash*=1099511628211ull;
            }
        };
        add(std::uint32_t(points.size()));
        add(std::uint32_t(indices.size()));
        for(const auto& p:points) {
            for(float v:{p.x,p.y,p.z}) {
                if(!std::isfinite(v))
                    throw std::invalid_argument("VIVID LOD mapping: nonfinite source");
                std::uint32_t bits=0;
                std::memcpy(&bits,&v,sizeof(bits));
                add(bits);
            }
        }
        for(auto index:indices) {
            if(index>=points.size())
                throw std::invalid_argument("VIVID LOD mapping: invalid triangle index");
            add(index);
        }
        const char digits[]="0123456789abcdef";
        std::string hex(16,'0');
        for(int i=15;i>=0;--i) {
            hex[std::size_t(i)]=digits[hash&15u];
            hash>>=4;
        }
        return hex;
    }

    void validate() const {
        if(!sourceVertices || sourceVertices>16'000'000 ||
           !sourceTriangles || sourceTriangles>5'000'000 ||
           sourceFingerprint.size()!=16 ||
           samples.empty()||samples.size()>16'000'000)
            throw std::invalid_argument("VIVID LOD mapping: invalid dimensions");
        if(!targetVertexIds.empty() && targetVertexIds.size()!=samples.size())
            throw std::invalid_argument("VIVID LOD mapping: target ID count mismatch");
        for(std::size_t i=1;i<targetVertexIds.size();++i)
            if(targetVertexIds[i]<=targetVertexIds[i-1])
                throw std::invalid_argument("VIVID LOD mapping: target IDs not strictly ordered");
        for(char c:sourceFingerprint)
            if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')))
                throw std::invalid_argument("VIVID LOD mapping: invalid fingerprint");
        for(const auto& sample:samples) {
            if(sample.triangle>=sourceTriangles ||
               !std::isfinite(sample.bary.x)||
               !std::isfinite(sample.bary.y)||
               !std::isfinite(sample.bary.z)||
               !std::isfinite(sample.distanceSquared)||
               sample.distanceSquared<0)
                throw std::invalid_argument("VIVID LOD mapping: invalid projection sample");
            const float total=sample.bary.x+sample.bary.y+sample.bary.z;
            if(std::fabs(total-1)>1.e-3f||
               sample.bary.x< -1.e-4f||sample.bary.y< -1.e-4f||
               sample.bary.z< -1.e-4f)
                throw std::invalid_argument("VIVID LOD mapping: invalid barycentric weights");
        }
    }
    Json toJson() const {
        validate();
        Json root=Json::object();
        root.set("version",Json::number(std::int64_t(kVersion)));
        root.set("sourceVertices",Json::number(std::int64_t(sourceVertices)));
        root.set("sourceTriangles",Json::number(std::int64_t(sourceTriangles)));
        root.set("sourceFingerprint",Json::string(sourceFingerprint));
        Json records=Json::array();
        for(const auto& sample:samples) {
            Json record=Json::object();
            record.set("triangle",Json::number(std::int64_t(sample.triangle)));
            Json weights=Json::array();
            for(float value:{sample.bary.x,sample.bary.y,sample.bary.z})
                weights.push(Json::number(double(value),9));
            record.set("bary",std::move(weights));
            record.set("distanceSquared",Json::number(double(sample.distanceSquared),9));
            records.push(std::move(record));
        }
        root.set("samples",std::move(records));
        if(!targetVertexIds.empty()) {
            Json ids=Json::array();
            for(const auto id:targetVertexIds)
                ids.push(Json::number(std::int64_t(id)));
            root.set("targetVertexIds",std::move(ids));
        }
        return root;
    }
    static MorphLodMapping fromJson(const Json& root) {
        if(!root.isObject() || !root["version"].isNumber() ||
           root["version"].integer(-1)!=kVersion ||
           !root["sourceVertices"].isNumber()||
           !root["sourceTriangles"].isNumber()||
           !root["sourceFingerprint"].isString()||
           !root["samples"].isArray()||
           root["samples"].size()>16'000'000)
            throw std::invalid_argument("VIVID LOD mapping: invalid schema/version");
        const auto vertices=root["sourceVertices"].integer(-1);
        const auto triangles=root["sourceTriangles"].integer(-1);
        if(vertices<=0||vertices>16'000'000||
           triangles<=0||triangles>5'000'000)
            throw std::invalid_argument("VIVID LOD mapping: invalid source dimensions");
        MorphLodMapping result;
        result.sourceVertices=std::uint32_t(vertices);
        result.sourceTriangles=std::uint32_t(triangles);
        result.sourceFingerprint=root["sourceFingerprint"].str();
        result.samples.reserve(root["samples"].size());
        if(root.has("targetVertexIds")) {
            const auto& ids=root["targetVertexIds"];
            if(!ids.isArray() || ids.size()!=root["samples"].size())
                throw std::invalid_argument("VIVID LOD mapping: malformed target IDs");
            result.targetVertexIds.reserve(ids.size());
            for(const auto& value:ids.items()) {
                if(!value.isNumber())
                    throw std::invalid_argument("VIVID LOD mapping: invalid target ID");
                const auto id=value.integer(-1);
                if(id<0 || id>std::numeric_limits<std::uint32_t>::max())
                    throw std::invalid_argument("VIVID LOD mapping: out-of-range target ID");
                result.targetVertexIds.push_back(std::uint32_t(id));
            }
        }
        for(const auto& record:root["samples"].items()) {
            if(!record.isObject() || !record["triangle"].isNumber() ||
               !record["bary"].isArray() || record["bary"].size()!=3 ||
               !record["distanceSquared"].isNumber())
                throw std::invalid_argument("VIVID LOD mapping: malformed sample");
            const auto id=record["triangle"].integer(-1);
            const auto& weights=record["bary"];
            if(id<0||id>=triangles ||
               !weights[0].isNumber()||!weights[1].isNumber()||
               !weights[2].isNumber())
                throw std::invalid_argument("VIVID LOD mapping: invalid sample values");
            LodSurfaceSample sample;
            sample.triangle=std::uint32_t(id);
            sample.bary={
                float(weights[0].toDouble()),
                float(weights[1].toDouble()),
                float(weights[2].toDouble())};
            sample.distanceSquared=float(record["distanceSquared"].toDouble());
            result.samples.push_back(sample);
        }
        result.validate();
        return result;
    }
    SparseMorphLibrary transfer(const SparseMorphLibrary& morphs,
                                std::span<const render::float3> sourcePositions,
                                std::span<const std::uint32_t> indices) const {
        validate();
        // Import preparation may append generated LOD vertices to the
        // full-model CSR, while the original source prefix remains stable.
        if(morphs.vertices()<sourceVertices ||
           sourcePositions.size()!=sourceVertices ||
           indices.size()/3!=sourceTriangles ||
           indices.size()%3 ||
           fingerprint(sourcePositions,indices)!=sourceFingerprint)
            throw std::invalid_argument("VIVID LOD mapping: source topology mismatch");
        return MorphLodSurface::transfer(morphs,indices,samples);
    }
};
} // namespace aiverse::vivid
