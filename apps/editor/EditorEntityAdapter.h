// Editor-facing document entity adapter. Genre modules map their native
// serialized objects to this identity; rendering and UI do not own selection.
#pragma once
#include "apps/editor/AuthoringComponents.h"
#include "core/MapDoc.h"
#include <cstddef>
#include <cstdint>
#include <limits>
#include <cmath>
#include <vector>
#include <set>

namespace aiverse::editor {

struct DocumentEntityRef {
    enum class Source : uint8_t { MapEntity, FpsCollider, FpsSpawn, MapLight, FpsLight };
    Source source = Source::MapEntity;
    size_t index = 0;

    static DocumentEntityRef mapEntity(size_t i) { return {Source::MapEntity, i}; }
    static DocumentEntityRef fpsCollider(size_t i) { return {Source::FpsCollider, i}; }
    static DocumentEntityRef fpsSpawn(size_t i) { return {Source::FpsSpawn, i}; }

    static DocumentEntityRef light(size_t i,bool fps) { return {fps?Source::FpsLight:Source::MapLight,i}; }
    bool isLight() const { return source==Source::MapLight || source==Source::FpsLight; }

    // FPS refs use the high bit for spawns; zero is reserved for no entity.
    uint64_t fpsKey() const {
        if (source == Source::MapEntity || isLight()) return 0;
        if (uint64_t(index) >= (uint64_t(1) << 63) - 1) return 0;
        return (source == Source::FpsSpawn ? (uint64_t(1) << 63) : 0)
               | (uint64_t(index) + 1);
    }
    static bool fromFpsKey(uint64_t key, DocumentEntityRef& out) {
        if (!key || (key & ~(uint64_t(1) << 63)) == 0) return false;
        out.source = (key & (uint64_t(1) << 63)) ? Source::FpsSpawn : Source::FpsCollider;
        out.index = size_t((key & ~(uint64_t(1) << 63)) - 1);
        return true;
    }
};

// RTS map objects and FPS JSON objects resolve through the same document
// reference, without changing either game's existing serialization format.
inline const DocEntity* documentMapEntity(const MapDoc& map, DocumentEntityRef ref) {
    if (ref.source != DocumentEntityRef::Source::MapEntity ||
        ref.index >= map.data.entities.size()) return nullptr;
    return &map.data.entities[ref.index];
}
inline DocEntity* documentMapEntity(MapDoc& map, DocumentEntityRef ref) {
    if (ref.source != DocumentEntityRef::Source::MapEntity ||
        ref.index >= map.data.entities.size()) return nullptr;
    return &map.data.entities[ref.index];
}

inline const char* fpsEntityCollection(DocumentEntityRef::Source source) {
    return source == DocumentEntityRef::Source::FpsSpawn ? "spawns" : "colliders";
}

// Shared JSON component access: a genre adapter can supply a different
// component store without requiring a separate Inspector selection model.
inline const Json* fpsDocumentEntity(const Json& fps, DocumentEntityRef ref) {
    if (ref.source != DocumentEntityRef::Source::FpsCollider &&
        ref.source != DocumentEntityRef::Source::FpsSpawn) return nullptr;
    const Json& entries = fps[fpsEntityCollection(ref.source)];
    if (!entries.isArray() || ref.index >= entries.size()) return nullptr;
    return &entries[ref.index];
}
inline Json* fpsDocumentEntity(Json& fps, DocumentEntityRef ref) {
    if (ref.source != DocumentEntityRef::Source::FpsCollider &&
        ref.source != DocumentEntityRef::Source::FpsSpawn) return nullptr;
    const char* collection = fpsEntityCollection(ref.source);
    if (!fps[collection].isArray() || ref.index >= fps[collection].size()) return nullptr;
    return &fps.member(collection).at(ref.index);
}

// Legacy storage adapter: Transform and Light are views of one light payload.
inline const Json* documentLight(const MapDoc& map,DocumentEntityRef ref) {
    if(!ref.isLight())return nullptr;
    const auto& list=(ref.source==DocumentEntityRef::Source::FpsLight?map.data.fps:map.data.extra)["lights"];
    return list.isArray() && ref.index<list.size() && list[ref.index].isObject()?&list[ref.index]:nullptr;
}
inline Json* documentLight(MapDoc& map,DocumentEntityRef ref) {
    if(!documentLight(static_cast<const MapDoc&>(map),ref))return nullptr;
    return &(ref.source==DocumentEntityRef::Source::FpsLight?map.data.fps:map.data.extra).member("lights").at(ref.index);
}

struct DocumentTransform {
    double x = 0, y = 0, z = 0;
};

// All genre-specific document layouts expose position through this API.
// The returned value is a copy, never a pointer into a mutable JSON array.
inline bool readDocumentTransform(const MapDoc& map, DocumentEntityRef ref,
                                  DocumentTransform& out) {
    if(ref.isLight()) {
        const auto* light=documentLight(map,ref);if(!light)return false;
        // Legacy map lights store "pos":[x,z]; the runtime reads it before x/z
        // (RtsLights::parseMapLights), so the editor must too or a move teleports to 0.
        const Json& pos=(*light)["pos"];
        const bool legacy=pos.isArray() && pos.size()>=2 && pos[0].isNumber() && pos[1].isNumber();
        out={legacy?pos[0].toDouble():(*light)["x"].toDouble(),(*light)["height"].toDouble(1),
             legacy?pos[1].toDouble():(*light)["z"].toDouble()};
    } else if (ref.source == DocumentEntityRef::Source::MapEntity) {
        const DocEntity* entity = documentMapEntity(map, ref);
        if (!entity) return false;
        if (!std::isfinite(entity->x) || !std::isfinite(entity->z)) return false;
        try { out = {entity->x, authoredEntity(*entity)?authoredLocalTransform(*entity).position.y.toDouble():0.0, entity->z}; }
        catch(const std::exception&) { return false; }
    } else {
        const Json* object = fpsDocumentEntity(map.data.fps, ref);
        if (!object) return false;
        if (ref.source == DocumentEntityRef::Source::FpsSpawn) {
            if (!object->isArray() || object->size() < 3 ||
                !(*object)[0].isNumber() || !(*object)[1].isNumber() || !(*object)[2].isNumber()) return false;
            out = {(*object)[0].toDouble(), (*object)[1].toDouble(), (*object)[2].toDouble()};
        } else {
            // FPS also supports rotated box colliders stored as center/half/yaw.
            // Preserve their real serialization instead of fabricating min/max.
            if ((*object)["yaw"].isNumber()) {
                const Json& centre = (*object)["center"];
                const Json& half = (*object)["half"];
                if (!centre.isArray() || !half.isArray() ||
                    centre.size() != 3 || half.size() != 3 ||
                    !std::isfinite((*object)["yaw"].toDouble())) return false;
                for (size_t axis = 0; axis < 3; ++axis)
                    if (!centre[axis].isNumber() || !half[axis].isNumber() ||
                        !std::isfinite(centre[axis].toDouble()) ||
                        !std::isfinite(half[axis].toDouble()) ||
                        half[axis].toDouble() <= 0.0) return false;
                out = {centre[0].toDouble(), centre[1].toDouble(),
                       centre[2].toDouble()};
                return true;
            }
            const Json& lo = (*object)["min"], &hi = (*object)["max"];
            if (!lo.isArray() || !hi.isArray() || lo.size() < 3 || hi.size() < 3) return false;
            for (size_t axis = 0; axis < 3; ++axis) {
                if (!lo[axis].isNumber() || !hi[axis].isNumber()) return false;
                const double minimum = lo[axis].toDouble(), maximum = hi[axis].toDouble();
                if (!std::isfinite(minimum) || !std::isfinite(maximum) ||
                    minimum >= maximum) return false;
            }
            // Midpoint without summing two potentially huge finite endpoints.
            // (lo + hi) / 2 can overflow even when the midpoint is finite.
            out = {lo[0].toDouble() * 0.5 + hi[0].toDouble() * 0.5,
                   lo[1].toDouble() * 0.5 + hi[1].toDouble() * 0.5,
                   lo[2].toDouble() * 0.5 + hi[2].toDouble() * 0.5};
        }
    }
    return std::isfinite(out.x) && std::isfinite(out.y) && std::isfinite(out.z);
}

inline bool translateDocumentEntity(MapDoc& map, DocumentEntityRef ref,
                                    DocumentTransform delta, bool markDirty = true) {
    if (!std::isfinite(delta.x) || !std::isfinite(delta.y) || !std::isfinite(delta.z))
        return false;
    DocumentTransform before;
    if (!readDocumentTransform(map, ref, before)) return false;
    const DocumentTransform after{before.x + delta.x, before.y + delta.y, before.z + delta.z};
    if (!std::isfinite(after.x) || !std::isfinite(after.y) || !std::isfinite(after.z))
        return false;
    if (delta.x == 0 && delta.y == 0 && delta.z == 0) return false;
    if(ref.isLight()) {
        if(after.x < -1000000 || after.x > 1000000 || after.z < -1000000 || after.z > 1000000 ||
           after.y < -1000 || after.y > 10000)return false;
        auto* light=documentLight(map,ref);if(!light)return false;
        light->set("x",Json::number(after.x,3));light->set("z",Json::number(after.z,3));
        light->set("height",Json::number(after.y,3));light->erase("pos");
    } else if (ref.source == DocumentEntityRef::Source::MapEntity) {
        // RTS maps are 2D in their serialized format. Never silently discard Y.
        DocEntity* entity = documentMapEntity(map, ref);
        if (!entity || (delta.y!=0&&!authoredEntity(*entity))) return false;
        const float x = float(after.x), z = float(after.z);
        if (!std::isfinite(x) || !std::isfinite(z)) return false;
        if(delta.y!=0){try{*entity=editAuthoredPlacement(*entity,"y",Json::number(after.y,4));}catch(const std::exception&){return false;}}
        entity->x = x;
        entity->z = z;
        if (entity->at.isObject()) {
            entity->at.set("x", Json::number(double(x), 3));
            entity->at.set("y", Json::number(double(z), 3));
        }
    } else {
        Json* entity = fpsDocumentEntity(map.data.fps, ref);
        if (!entity) return false;
        auto shifted = [&](const Json& xyz, Json& result) {
            if (!xyz.isArray() || xyz.size() < 3) return false;
            for (size_t i = 0; i < 3; ++i)
                if (!xyz[i].isNumber()) return false;
            const double coords[3] = {xyz[0].toDouble() + delta.x,
                                      xyz[1].toDouble() + delta.y,
                                      xyz[2].toDouble() + delta.z};
            result = Json::array();
            for (double value : coords) {
                if (!std::isfinite(value)) return false;
                result.push(Json::number(value, 3));
            }
            return true;
        };
        if (ref.source == DocumentEntityRef::Source::FpsSpawn) {
            Json moved;
            if (!shifted(*entity, moved)) return false;
            *entity = moved;
        } else if ((*entity)["yaw"].isNumber()) {
            Json centre;
            if (!shifted((*entity)["center"], centre)) return false;
            entity->member("center") = centre;
        } else {
            const Json& originalMin = (*entity)["min"];
            const Json& originalMax = (*entity)["max"];
            if (!originalMin.isArray() || !originalMax.isArray() ||
                originalMin.size() < 3 || originalMax.size() < 3) return false;
            for (size_t axis = 0; axis < 3; ++axis) {
                if (!originalMin[axis].isNumber() || !originalMax[axis].isNumber() ||
                    !std::isfinite(originalMin[axis].toDouble()) ||
                    !std::isfinite(originalMax[axis].toDouble()) ||
                    !(originalMin[axis].toDouble() < originalMax[axis].toDouble()))
                    return false;
            }
            Json lo, hi;
            if (!shifted(originalMin, lo) || !shifted(originalMax, hi))
                return false;
            entity->member("min") = lo;
            entity->member("max") = hi;
        }
    }
    if (markDirty) map.touched();
    return true;
}

inline bool setDocumentPosition(MapDoc& map, DocumentEntityRef ref,
                                DocumentTransform position) {
    if (!std::isfinite(position.x) || !std::isfinite(position.y) ||
        !std::isfinite(position.z)) return false;
    DocumentTransform current;
    if (!readDocumentTransform(map, ref, current)) return false;
    return translateDocumentEntity(map, ref,
        {position.x - current.x, position.y - current.y, position.z - current.z});
}

inline bool setDocumentColliderBounds(MapDoc& map, DocumentEntityRef ref,
                                      DocumentTransform minimum,
                                      DocumentTransform maximum) {
    if (ref.source != DocumentEntityRef::Source::FpsCollider) return false;
    const double lo[3] = {minimum.x, minimum.y, minimum.z};
    const double hi[3] = {maximum.x, maximum.y, maximum.z};
    for (size_t axis = 0; axis < 3; ++axis)
        if (!std::isfinite(lo[axis]) || !std::isfinite(hi[axis]) ||
            !(lo[axis] < hi[axis])) return false;
    Json* collider = fpsDocumentEntity(map.data.fps, ref);
    if (!collider || !(*collider)["min"].isArray() ||
        !(*collider)["max"].isArray() ||
        (*collider)["min"].size() < 3 || (*collider)["max"].size() < 3)
        return false;
    Json minJson = Json::array(), maxJson = Json::array();
    for (size_t axis = 0; axis < 3; ++axis) {
        minJson.push(Json::number(lo[axis], 3));
        maxJson.push(Json::number(hi[axis], 3));
    }
    if ((*collider)["min"] == minJson && (*collider)["max"] == maxJson)
        return false;
    collider->member("min") = minJson;
    collider->member("max") = maxJson;
    map.touched();
    return true;
}

// Rotated FPS boxes have an explicit center, half-extents and yaw in turns.
// These fields are already supported by fps::Module::setup and are persisted
// without converting the collider to a different map schema.
inline bool setDocumentOrientedCollider(MapDoc& map, DocumentEntityRef ref,
                                       DocumentTransform centre,
                                       DocumentTransform half, double yawTurns) {
    if (ref.source != DocumentEntityRef::Source::FpsCollider) return false;
    const double c[3] = {centre.x, centre.y, centre.z};
    const double h[3] = {half.x, half.y, half.z};
    if (!std::isfinite(yawTurns)) return false;
    for (size_t axis = 0; axis < 3; ++axis)
        if (!std::isfinite(c[axis]) || !std::isfinite(h[axis]) ||
            h[axis] <= 0.0 || (axis != 1 && h[axis] > 2048.0))
            return false;
    Json* object = fpsDocumentEntity(map.data.fps, ref);
    if (!object || !(*object)["yaw"].isNumber()) return false;
    Json centerJson = Json::array(), halfJson = Json::array();
    for (size_t axis = 0; axis < 3; ++axis) {
        centerJson.push(Json::number(c[axis], 3));
        halfJson.push(Json::number(h[axis], 3));
    }
    double wrapped = std::fmod(yawTurns, 1.0);
    if (wrapped < 0) wrapped += 1.0;
    const Json yaw = Json::number(wrapped, 6);
    if ((*object)["center"] == centerJson &&
        (*object)["half"] == halfJson && (*object)["yaw"] == yaw) return false;
    object->member("center") = centerJson;
    object->member("half") = halfJson;
    object->member("yaw") = yaw;
    map.touched();
    return true;
}

inline bool translateDocumentEntities(MapDoc& map,
                                      const std::vector<DocumentEntityRef>& refs,
                                      DocumentTransform delta) {
    if (refs.empty() || !std::isfinite(delta.x) || !std::isfinite(delta.y) ||
        !std::isfinite(delta.z) ||
        (delta.x == 0 && delta.y == 0 && delta.z == 0)) return false;
    // A selection may contain duplicate references; move each source once.
    std::set<std::pair<uint8_t, size_t>> seen;
    std::vector<DocumentEntityRef> unique;
    unique.reserve(refs.size());
    for (const auto& ref : refs)
        if (seen.insert({uint8_t(ref.source), ref.index}).second) unique.push_back(ref);
    // Validate the entire selection before modifying the map. No partial moves.
    for (const auto& ref : unique) {
        DocumentTransform current;
        if (!readDocumentTransform(map, ref, current)) return false;
        const DocumentTransform next{current.x + delta.x, current.y + delta.y,
                                     current.z + delta.z};
        if (!std::isfinite(next.x) || !std::isfinite(next.y) || !std::isfinite(next.z))
            return false;
        if(ref.isLight()) {
            if(next.x < -1000000 || next.x > 1000000 || next.z < -1000000 || next.z > 1000000 ||
               next.y < -1000 || next.y > 10000)return false;
        } else if (ref.source == DocumentEntityRef::Source::MapEntity) {
            if (delta.y != 0 || !std::isfinite(float(next.x)) ||
                !std::isfinite(float(next.z))) return false;
        } else {
            const Json* object = fpsDocumentEntity(map.data.fps, ref);
            if (!object) return false;
            auto validCoords = [&](const Json& coords) {
                if (!coords.isArray() || coords.size() < 3) return false;
                const double offsets[3] = {delta.x, delta.y, delta.z};
                for (size_t axis = 0; axis < 3; ++axis) {
                    if (!coords[axis].isNumber()) return false;
                    const double value = coords[axis].toDouble();
                    if (!std::isfinite(value) ||
                        !std::isfinite(value + offsets[axis])) return false;
                }
                return true;
            };
            if (ref.source == DocumentEntityRef::Source::FpsSpawn) {
                if (!validCoords(*object)) return false;
            } else if ((*object)["yaw"].isNumber()) {
                const Json& centre = (*object)["center"];
                const Json& half = (*object)["half"];
                if (!validCoords(centre) || !half.isArray() ||
                    half.size() != 3 ||
                    !std::isfinite((*object)["yaw"].toDouble())) return false;
                for (size_t axis = 0; axis < 3; ++axis)
                    if (!half[axis].isNumber() ||
                        !std::isfinite(half[axis].toDouble()) ||
                        half[axis].toDouble() <= 0.0) return false;
            } else {
                const Json& lo = (*object)["min"];
                const Json& hi = (*object)["max"];
                if (!validCoords(lo) || !validCoords(hi)) return false;
                for (size_t axis = 0; axis < 3; ++axis)
                    if (lo[axis].toDouble() >= hi[axis].toDouble()) return false;
            }
        }
    }
    bool moved = false;
    for (const auto& ref : unique)
        moved = translateDocumentEntity(map, ref, delta, false) || moved;
    if (moved) map.touched();
    return moved;
}

enum class DocumentComponentKind : uint8_t {
    Transform, Renderable, Collider, Spawn, Gameplay, Light
};
struct DocumentComponentDescriptor {
    DocumentComponentKind kind;
    const char* id;
    const char* title;
    bool editable;
};

// Editor-facing component inventory. This describes document-backed data,
// not live ECS pools; runtime reflection/writeback is a separate next stage.
inline std::vector<DocumentComponentDescriptor> documentComponents(
    const MapDoc& map, DocumentEntityRef ref) {
    std::vector<DocumentComponentDescriptor> components;
    DocumentTransform transform;
    if (!readDocumentTransform(map, ref, transform)) return components;
    components.push_back({DocumentComponentKind::Transform, "Transform", "Transform", true});
    switch (ref.source) {
    case DocumentEntityRef::Source::MapLight:
    case DocumentEntityRef::Source::FpsLight:
        components.push_back({DocumentComponentKind::Light,"Light","Light",true});break;
    case DocumentEntityRef::Source::MapEntity: {
        const DocEntity* entity = documentMapEntity(map, ref);
        if (!entity) return {};
        components.push_back({DocumentComponentKind::Renderable, "Renderable", "Renderable", false});
        break;
    }
    case DocumentEntityRef::Source::FpsCollider: {
        const Json* object = fpsDocumentEntity(map.data.fps, ref);
        const bool rotated = object && (*object)["yaw"].isNumber();
        components.push_back({DocumentComponentKind::Collider,
                              rotated ? "OrientedBoxCollider" : "BoxCollider",
                              rotated ? "Oriented Box Collider" : "Box Collider", true});
        break;
    }
    case DocumentEntityRef::Source::FpsSpawn:
        components.push_back({DocumentComponentKind::Spawn, "SpawnPoint", "Spawn Point", true});
        break;
    }
    return components;
}

inline bool readDocumentComponent(const MapDoc& map, DocumentEntityRef ref,
                                  DocumentComponentKind kind, Json& result) {
    DocumentTransform position;
    if (!readDocumentTransform(map, ref, position)) return false;
    result = Json::object();
    if (kind == DocumentComponentKind::Transform) {
        Json xyz = Json::array();
        xyz.push(Json::number(position.x, 3));
        xyz.push(Json::number(position.y, 3));
        xyz.push(Json::number(position.z, 3));
        result.set("position", xyz);
        if (ref.source == DocumentEntityRef::Source::MapEntity) {
            const DocEntity* entity = documentMapEntity(map, ref);
            if(entity&&authoredEntity(*entity)){const auto& t=entity->extra["components"]["Transform"];result.set("rotationY",t.has("yaw")?t["yaw"]:Json::number(0.0));result.set("uniformScale",t.has("scale")?t["scale"]:Json::number(1.0));}
            if (entity && !authoredEntity(*entity) && entity->extra["facing"].isNumber() &&
                std::isfinite(entity->extra["facing"].toDouble()))
                result.set("rotationY",
                    Json::number(entity->extra["facing"].toDouble(), 3));
        }
        if (ref.source == DocumentEntityRef::Source::FpsCollider) {
            const Json* object = fpsDocumentEntity(map.data.fps, ref);
            if (object && (*object)["yaw"].isNumber()) {
                result.set("rotationY",
                    Json::number((*object)["yaw"].toDouble() * 360.0, 3));
                Json scale = Json::array();
                for (size_t axis = 0; axis < 3; ++axis)
                    scale.push(Json::number(
                        (*object)["half"][axis].toDouble() * 2.0, 3));
                result.set("scale", scale);
            }
        }
        return true;
    }
    if (ref.source == DocumentEntityRef::Source::MapEntity) {
        const DocEntity* entity = documentMapEntity(map, ref);
        if (!entity) return false;
        if (kind == DocumentComponentKind::Renderable) {
            result.set("type", Json::string(entity->type));
            if(authoredEntity(*entity))result.set("asset",Json::string(authoredModelAsset(*entity)));
            return true;
        }
        if (kind == DocumentComponentKind::Gameplay) {
            result.set("type", Json::string(entity->type));
            result.set("owner", Json::number(int64_t(entity->owner)));
            return true;
        }
        return false;
    }
    if(ref.isLight() && kind==DocumentComponentKind::Light) {
        const auto* light=documentLight(map,ref);if(!light)return false;
        result=*light;
        for(const char* key:{"entityId","name","x","z","height","pos"})result.erase(key);
        return true;
    }
    const Json* object = fpsDocumentEntity(map.data.fps, ref);
    if (!object) return false;
    if (kind == DocumentComponentKind::Collider &&
        ref.source == DocumentEntityRef::Source::FpsCollider) {
        if ((*object)["yaw"].isNumber()) {
            result.set("center", (*object)["center"]);
            result.set("half", (*object)["half"]);
            result.set("yaw", (*object)["yaw"]);
        } else {
            result.set("min", (*object)["min"]);
            result.set("max", (*object)["max"]);
        }
        return true;
    }
    if (kind == DocumentComponentKind::Spawn &&
        ref.source == DocumentEntityRef::Source::FpsSpawn) {
        result.set("position", *object);
        return true;
    }
    return false;
}

inline bool writeDocumentComponent(MapDoc& map, DocumentEntityRef ref,
                                   DocumentComponentKind kind, const Json& value) {
    if (!value.isObject()) return false;
    auto readVec3 = [](const Json& array, DocumentTransform& out) {
        if (!array.isArray() || array.size() != 3) return false;
        for (size_t axis = 0; axis < 3; ++axis)
            if (!array[axis].isNumber() || !std::isfinite(array[axis].toDouble()))
                return false;
        out = {array[0].toDouble(), array[1].toDouble(), array[2].toDouble()};
        return true;
    };
    if(ref.isLight() && kind==DocumentComponentKind::Light) {
        auto* light=documentLight(map,ref);if(!light)return false;
        Json next=*light;
        for(const auto& [key,item]:value.members()) {
            if(key=="type") {if(!item.isString() || (item.str()!="point" && item.str()!="spot"))return false;}
            else if(key=="bake") {if(!item.isString() || (item.str()!="realtime" && item.str()!="mixed" && item.str()!="baked"))return false;}
            else if(key=="electric" || key=="nightOnly") {if(item.type()!=Json::Type::Bool)return false;}
            else if(key=="color") {
                if(!item.isArray() || item.size()!=3)return false;
                for(const auto& c:item.items())if(!c.isNumber() || !std::isfinite(c.toDouble()) || c.toDouble()<0 || c.toDouble()>1)return false;
            } else {
                if(!item.isNumber() || !std::isfinite(item.toDouble()))return false;
                const double n=item.toDouble();
                if(key=="radius") {if(n<=0 || n>100000)return false;}
                else if(key=="intensity") {if(n<0 || n>10000)return false;}
                else return false;
            }
            next.set(key,item);
        }
        if(next==*light)return false;
        *light=std::move(next);map.touched();return true;
    }
    if (kind == DocumentComponentKind::Transform) {
        if (ref.source == DocumentEntityRef::Source::MapEntity) {
            auto* entity=documentMapEntity(map,ref);
            if(entity && authoredEntity(*entity)) {
                auto candidate=*entity;
                try {
                    if(value.has("position")) {
                        DocumentTransform p;
                        if(!readVec3(value["position"],p)||std::abs(p.x)>1000000||std::abs(p.z)>1000000)return false;
                        candidate.x=float(p.x);candidate.z=float(p.z);
                        candidate=editAuthoredPlacement(candidate,"y",Json::number(p.y,4));
                    }
                    if(value.has("rotationY"))candidate=editAuthoredPlacement(candidate,"yaw",value["rotationY"]);
                    if(value.has("uniformScale"))candidate=editAuthoredPlacement(candidate,"scale",value["uniformScale"]);
                    (void)authoredLocalTransform(candidate);
                } catch(const std::exception&) {return false;}
                if(candidate.x==entity->x && candidate.z==entity->z && candidate.extra==entity->extra)return false;
                *entity=std::move(candidate);map.touched();return true;
            }
        }
        if (ref.source == DocumentEntityRef::Source::MapEntity &&
            value.has("rotationY")) {
            // A patch may carry rotationY and position together: validate both,
            // then apply both (position was silently dropped before). Objects
            // without authored "facing" do not get one invented, but their
            // position part is still written.
            DocEntity* entity = documentMapEntity(map, ref);
            if (!entity || !value["rotationY"].isNumber()) return false;
            const double degrees = value["rotationY"].toDouble();
            if (!std::isfinite(degrees)) return false;
            DocumentTransform position;
            const bool hasPosition = value.has("position");
            if (hasPosition && !readVec3(value["position"], position)) return false;
            if (hasPosition && position.y != 0) return false;   // RTS maps are 2D
            const Json rotated = Json::number(degrees, 3);
            bool changed = false;
            if (entity->extra["facing"].isNumber() && !(entity->extra["facing"] == rotated)) {
                entity->extra.set("facing", rotated);
                map.touched();
                changed = true;
            }
            if (hasPosition) changed = setDocumentPosition(map, ref, position) || changed;
            return changed;
        }
        if (ref.source == DocumentEntityRef::Source::FpsCollider) {
            const Json* object = fpsDocumentEntity(map.data.fps, ref);
            if (object && (*object)["yaw"].isNumber()) {
                DocumentTransform position, half;
                if (!readDocumentTransform(map, ref, position) ||
                    !readVec3((*object)["half"], half)) return false;
                double yaw = (*object)["yaw"].toDouble();
                if (value.has("position") &&
                    !readVec3(value["position"], position)) return false;
                if (value.has("rotationY")) {
                    if (!value["rotationY"].isNumber() ||
                        !std::isfinite(value["rotationY"].toDouble())) return false;
                    yaw = value["rotationY"].toDouble() / 360.0;
                }
                if (value.has("scale")) {
                    DocumentTransform scale;
                    if (!readVec3(value["scale"], scale)) return false;
                    half = {scale.x * 0.5, scale.y * 0.5, scale.z * 0.5};
                }
                return setDocumentOrientedCollider(map, ref, position, half, yaw);
            }
        }
        DocumentTransform position;
        return readVec3(value["position"], position) &&
               setDocumentPosition(map, ref, position);
    }
    if (kind == DocumentComponentKind::Collider &&
        ref.source == DocumentEntityRef::Source::FpsCollider) {
        DocumentTransform lo, hi;
        return readVec3(value["min"], lo) && readVec3(value["max"], hi) &&
               setDocumentColliderBounds(map, ref, lo, hi);
    }
    if (kind == DocumentComponentKind::Spawn &&
        ref.source == DocumentEntityRef::Source::FpsSpawn) {
        DocumentTransform position;
        return readVec3(value["position"], position) &&
               setDocumentPosition(map, ref, position);
    }
    // Renderable and RTS gameplay are deliberately read-only until their
    // runtime/schema writeback contract is implemented and tested.
    return false;
}

} // namespace aiverse::editor
