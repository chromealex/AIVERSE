// AIVERSE — genre-neutral attachment layout document for scenes, prefabs
// and native editor/model preview assets. CPU-only authoring representation.
// No gameplay, simulation, graph clocks, GPU handles or Model ownership.
#pragma once
#include "engine/data/Json.h"
#include "engine/render/Math3D.h"
#include <cmath>
#include <cstddef>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::view {

struct AttachmentLayoutItem {
    std::string model;
    std::string socket;
    bool enabled=true;
    // Column-major model-space transform relative to the parent socket,
    // then a child-authored local transform. These match ModelAttachment's
    // existing local and ModelDraw::world semantics exactly.
    render::mat4 local{};
    render::mat4 childWorld{};
    // Optional stable authored child key within ONE parent prefab.
    // Keep appended to preserve legacy aggregate initializers.
    // Empty in old files: position/ordinal remains its only identity.
    std::string id;
    // Independent MotionWeave graph source, never a static parent-pose
    // attachment. Requires a nonempty stable authored ID when enabled.
    bool animated=false;
    // Host-resolved immutable Graph asset key. No runtime Graph pointer.
    std::string animationGraph;
};
struct AttachmentLayout {
    std::string parent;
    std::vector<AttachmentLayoutItem> attachments;
};

// Independent of any file system: each project host chooses its own asset
// source resolver, path sandbox policy and transaction/atomic file write.
class AttachmentLayoutCodec final {
    static void validateTransform(const render::mat4& m) {
        for(float value:m.m)
            if(!std::isfinite(value) || std::abs(value)>1000000.f)
                throw std::invalid_argument(
                    "attachment transform contains nonfinite/out-of-range value");
        // Attachment transforms are affine (not projective). A host may
        // author any rotation, scale or shear; matrices are not decomposed.
        if(std::abs(m.at(3,0))>1.e-5f ||
           std::abs(m.at(3,1))>1.e-5f ||
           std::abs(m.at(3,2))>1.e-5f ||
           std::abs(m.at(3,3)-1.f)>1.e-5f)
            throw std::invalid_argument("attachment matrix must be affine");
    }
    static render::mat4 readMatrix(const Json& field) {
        if(field.isNull())return {};
        if(!field.isArray() || field.size()!=16)
            throw std::invalid_argument("attachment matrix must be 16 numbers");
        render::mat4 result;
        for(size_t i=0;i<16;++i) {
            if(!field[i].isNumber())
                throw std::invalid_argument(
                    "attachment matrix element must be numeric");
            result.m[i]=float(field[i].toDouble());
        }
        validateTransform(result);
        return result;
    }
    static Json writeMatrix(const render::mat4& matrix) {
        validateTransform(matrix);
        Json items=Json::array();
        for(float value:matrix.m)
            items.push(Json::number(double(value),6));
        return items;
    }
public:
    // Missing local/childWorld fields intentionally mean identity, so
    // existing Stage 154/155 files remain readable without migration.
    // Invalid new data throws BEFORE mutating out (last-good document).
    static void read(const Json& source,AttachmentLayout& out) {
        if(!source.isObject() || !source["attachments"].isArray())
            throw std::invalid_argument(
                "attachment document requires object with attachments array");
        AttachmentLayout next;
        next.parent=source.getStr("parent");
        next.attachments.reserve(source["attachments"].size());
        std::set<std::string> authoredIds;
        for(const auto& field:source["attachments"].items()) {
            if(!field.isObject())
                throw std::invalid_argument("attachment entry must be object");
            AttachmentLayoutItem item;
            item.model=field.getStr("model");
            item.socket=field.getStr("socket");
            item.enabled=field.getBool("enabled",true);
            if(field.has("animated")) {
                if(field["animated"].type()!=Json::Type::Bool)
                    throw std::invalid_argument(
                        "attachment animated flag must be boolean");
                item.animated=field["animated"].boolean();
            }
            if(field.has("animationGraph")) {
                if(!field["animationGraph"].isString())
                    throw std::invalid_argument(
                        "attachment animationGraph must be a string");
                item.animationGraph=field["animationGraph"].str();
                if(item.animationGraph.size()>1024)
                    throw std::invalid_argument(
                        "attachment animationGraph asset key too long");
            }
            if(field.has("id")) {
                if(!field["id"].isString())
                    throw std::invalid_argument(
                        "attachment id must be a string");
                item.id=field["id"].str();
                if(item.id.size()>128 ||
                   (!item.id.empty() &&
                    !authoredIds.insert(item.id).second))
                    throw std::invalid_argument(
                        "attachment stable id too long or duplicated");
            }
            if(item.model.empty() || item.socket.empty())
                throw std::invalid_argument(
                    "attachment entry needs model path and named socket");
            if(item.animated && item.enabled &&
               (item.id.empty() || item.animationGraph.empty()))
                throw std::invalid_argument(
                    "enabled animated attachment needs stable id and Graph asset");
            item.local=readMatrix(field["local"]);
            item.childWorld=readMatrix(field["childWorld"]);
            next.attachments.push_back(std::move(item));
        }
        out=std::move(next);
    }
    static Json write(const AttachmentLayout& layout) {
        Json root=Json::object();
        root.set("parent",Json::string(layout.parent));
        Json entries=Json::array();
        std::set<std::string> authoredIds;
        for(const auto& item:layout.attachments) {
            if(item.model.empty() || item.socket.empty())
                throw std::invalid_argument(
                    "cannot save attachment with missing model/socket");
            if(item.id.size()>128 ||
               (!item.id.empty() &&
                !authoredIds.insert(item.id).second))
                throw std::invalid_argument(
                    "attachment stable id too long or duplicated");
            if(item.animated && item.enabled &&
               (item.id.empty() || item.animationGraph.empty()))
                throw std::invalid_argument(
                    "enabled animated attachment needs stable id and Graph asset");
            Json entry=Json::object();
            if(item.animated)entry.set("animated",Json::fromBool(true));
            if(!item.animationGraph.empty())
                entry.set("animationGraph",Json::string(item.animationGraph));
            if(!item.id.empty())
                entry.set("id",Json::string(item.id));
            entry.set("model",Json::string(item.model));
            entry.set("socket",Json::string(item.socket));
            entry.set("enabled",Json::fromBool(item.enabled));
            entry.set("local",writeMatrix(item.local));
            entry.set("childWorld",writeMatrix(item.childWorld));
            entries.push(std::move(entry));
        }
        root.set("attachments",std::move(entries));
        return root;
    }
};

} // namespace aiverse::view
