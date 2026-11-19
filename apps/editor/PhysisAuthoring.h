// PHYSIS map authoring adapter, independent of game genre or native ECS state.
// Stores the PHYSIS block in the current map document, with ordinary MapDoc
// snapshot Undo/Redo. It is data authoring ONLY: native GameModule must opt in.
#pragma once
#include "core/MapDoc.h"
#include "engine/physis/PhysisFieldAssets.h"
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <algorithm>
#include <stdexcept>
#include <utility>
#include <map>
#include <set>
#include <vector>

namespace aiverse::editor::physis_authoring {

inline const Json& read(const MapDoc& doc) {
    return doc.data.fps.isNull() ? doc.data.extra["physis"] :
           doc.data.fps["physis"];
}

inline Json defaultConfig() {
    Json v = Json::object();
    v.set("enabled", Json::fromBool(true));
    Json grid = Json::object();
    grid.set("originX", Json::number(int64_t(-16)));
    grid.set("originZ", Json::number(int64_t(-16)));
    grid.set("width", Json::number(int64_t(32)));
    grid.set("height", Json::number(int64_t(32)));
    grid.set("cellSizeMeters", Json::number(int64_t(128)));
    grid.set("seed", Json::number(int64_t(1)));
    v.set("grid", grid);
    Json climate = Json::object();
    climate.set("baseTemperature", Json::number(int64_t(15)));
    climate.set("baseHumidity", Json::numberText("0.5"));
    v.set("climate", climate);
    v.set("enableStorms", Json::fromBool(false));
    v.set("enableDisasters", Json::fromBool(false));
    v.set("enableWindZones", Json::fromBool(false));
    v.set("enableGravityFields", Json::fromBool(false));
    v.set("enableForces", Json::fromBool(false));
    v.set("floodMode", Json::string("LocalShallow"));
    return v;
}

// Strict bounded validation before beginning a MapDoc edit; rejects malformed
// data instead of applying it partially. Unknown keys are kept for future
// extensions (zones/events), and must be consumed by a project-owned loader.
inline bool validate(const Json& v, std::string* error = nullptr) {
    auto fail = [&](const char* reason) {
        if (error) *error = reason;
        return false;
    };
    if (!v.isObject()) return fail("PHYSIS block must be a JSON object");
    const auto& grid = v["grid"];
    const auto& climate = v["climate"];
    if (!grid.isObject() || !climate.isObject())
        return fail("PHYSIS grid and climate must be objects");
    const auto fieldInt = [](const Json& obj, const char* key,
                             int64_t minimum, int64_t maximum) {
        const auto& value = obj[key];
        if (!value.isNumber()) return false;
        const auto n = value.integer(std::numeric_limits<int64_t>::min());
        return n >= minimum && n <= maximum &&
               value.numberLiteral().find_first_of(".eE") == std::string::npos;
    };
    for (auto key : {"originX","originZ"}) {
        if (!fieldInt(grid, key, -1000000, 1000000))
            return fail("PHYSIS origin outside supported extent");
    }
    for (auto key : {"width","height"}) {
        if (!fieldInt(grid, key, 1, 1024))
            return fail("PHYSIS grid dimensions must be 1..1024");
    }
    if (grid.getInt("width") * grid.getInt("height") > 1048576)
        return fail("PHYSIS grid too large");
    if (!fieldInt(grid,"cellSizeMeters",1,100000))
        return fail("PHYSIS cellSizeMeters must be 1..100000");
    if (!fieldInt(grid,"seed",0,2147483647))
        return fail("PHYSIS seed must be nonnegative integer");
    const auto number = [](const Json& obj, const char* key,
                           double lo, double hi) {
        const auto& v = obj[key];
        if (!v.isNumber()) return false;
        const auto n = v.toDouble();
        return std::isfinite(n) && n >= lo && n <= hi;
    };
    if (!number(climate,"baseTemperature",-80,80))
        return fail("PHYSIS baseTemperature outside [-80,80]");
    if (!number(climate,"baseHumidity",0,1))
        return fail("PHYSIS baseHumidity outside [0,1]");
    for (auto key : {"enabled","enableStorms","enableDisasters",
                     "enableWindZones","enableGravityFields","enableForces"})
        if (!v[key].isNull() && v[key].type() != Json::Type::Bool)
            return fail("PHYSIS enable flags must be boolean");
    const auto mode = v.getStr("floodMode", "LocalShallow");
    if (mode != "LocalShallow" && mode != "ExternalAuthority")
        return fail("PHYSIS floodMode must be LocalShallow or ExternalAuthority");
    for (auto key : {"storms","windZones","gravityFields"})
        if (!v[key].isNull() && !v[key].isArray())
            return fail("PHYSIS authored field collections must be arrays");
    // Share the exact native parser: editor and runtime agree on IDs, types,
    // coordinate limits, storm radii and required enable/persistence switches.
    try {
        aiverse::physis::InstallOptions options;
        options.enableStorms = v.getBool("enableStorms", false);
        options.enableWindZones = v.getBool("enableWindZones", false);
        options.enableGravityFields = v.getBool("enableGravityFields", false);
        (void)aiverse::physis::parseFieldAssets(v, options);
    } catch (const std::exception& e) {
        if (error) *error = e.what();
        return false;
    }
    return true;
}

// An atomic document edit. The existing MapDoc undo history owns the snapshot;
// there is never an in-place mutation of a live Simulation.
inline bool apply(MapDoc& doc, const Json& config, std::string* error = nullptr) {
    if (!config.isNull() && !validate(config,error)) return false;
    if (!doc.data.fps.isNull() && !doc.data.fps.isObject()) {
        if (error) *error = "PHYSIS FPS document is not an object";
        return false;
    }
    const Json old = read(doc);
    if (old == config) return true;
    doc.beginEdit();
    Json& container = doc.data.fps.isNull() ? doc.data.extra : doc.data.fps;
    if (config.isNull()) container.erase("physis");
    else container.set("physis",config);
    return true;
}


inline Json point(double x, double y, double z) {
    Json result = Json::array();
    result.push(Json::number(x, 3));
    result.push(Json::number(y, 3));
    result.push(Json::number(z, 3));
    return result;
}

inline const char* collectionKey(aiverse::physis::AuthoredFieldKind kind) {
    switch (kind) {
        case aiverse::physis::AuthoredFieldKind::Wind: return "windZones";
        case aiverse::physis::AuthoredFieldKind::Storm: return "storms";
        case aiverse::physis::AuthoredFieldKind::Gravity: return "gravityFields";
    }
    return "windZones";
}
inline const char* enabledKey(aiverse::physis::AuthoredFieldKind kind) {
    switch (kind) {
        case aiverse::physis::AuthoredFieldKind::Wind: return "enableWindZones";
        case aiverse::physis::AuthoredFieldKind::Storm: return "enableStorms";
        case aiverse::physis::AuthoredFieldKind::Gravity: return "enableGravityFields";
    }
    return "enableWindZones";
}

inline Json fieldTemplate(aiverse::physis::AuthoredFieldKind kind,
                          const std::string& id, double x, double y, double z) {
    Json entry = Json::object();
    entry.set("id", Json::string(id));
    entry.set("center", point(x,y,z));
    switch (kind) {
        case aiverse::physis::AuthoredFieldKind::Wind:
            entry.set("radiusMeters", Json::number(int64_t(100)));
            entry.set("velocity", point(10,0,0));
            entry.set("gustAmplitude", Json::number(int64_t(2)));
            break;
        case aiverse::physis::AuthoredFieldKind::Storm:
            entry.set("kind", Json::string("Tornado"));
            entry.set("eyeRadiusMeters", Json::number(int64_t(20)));
            entry.set("outerRadiusMeters", Json::number(int64_t(200)));
            entry.set("heightMeters", Json::number(int64_t(400)));
            entry.set("tangentialWind", Json::number(int64_t(45)));
            entry.set("updraft", Json::number(int64_t(20)));
            break;
        case aiverse::physis::AuthoredFieldKind::Gravity:
            entry.set("mode", Json::string("Uniform"));
            entry.set("radiusMeters", Json::number(int64_t(120)));
            entry.set("acceleration", point(0,-2,0));
            entry.set("overrideBaseline", Json::fromBool(true));
            break;
    }
    return entry;
}

inline bool findField(const Json& config, const std::string& id,
                      std::string& collection, std::size_t& index,
                      Json* result = nullptr) {
    for (const char* key : {"windZones","storms","gravityFields"}) {
        const Json& fields = config[key];
        if (!fields.isArray()) continue;
        for (std::size_t i=0;i<fields.size();++i)
            if (fields[i].getStr("id") == id) {
                collection = key;
                index = i;
                if (result) *result = fields[i];
                return true;
            }
    }
    return false;
}

// Every Add/Remove is one complete validated MapDoc undo transaction.
inline bool addField(MapDoc& doc, aiverse::physis::AuthoredFieldKind kind,
                     double x, double y, double z, std::string& insertedId,
                     std::string* error = nullptr) {
    Json config = read(doc);
    if (!config.isObject()) {
        if (error) *error = "Create PHYSIS map settings before adding fields";
        return false;
    }
    const char* name = collectionKey(kind);
    const char* prefix = kind == aiverse::physis::AuthoredFieldKind::Wind ? "wind-" :
                         kind == aiverse::physis::AuthoredFieldKind::Storm ? "storm-" : "gravity-";
    std::string candidate;
    std::size_t index = 0;
    for (std::size_t serial = 1; serial <= 2049; ++serial) {
        candidate = std::string(prefix) + std::to_string(serial);
        std::string foundKey;
        if (!findField(config, candidate, foundKey, index)) break;
        candidate.clear();
    }
    if (candidate.empty()) {
        if (error) *error = "No stable field ID available";
        return false;
    }
    Json fields = config[name];
    if (fields.isNull()) fields = Json::array();
    if (!fields.isArray()) {
        if (error) *error = "PHYSIS field collection is not an array";
        return false;
    }
    fields.push(fieldTemplate(kind, candidate, x, y, z));
    config.set(name, std::move(fields));
    config.set(enabledKey(kind), Json::fromBool(true));
    if (!apply(doc, config, error)) return false;
    insertedId = candidate;
    return true;
}

inline bool removeField(MapDoc& doc, const std::string& id,
                        std::string* error = nullptr) {
    Json config = read(doc);
    std::string collection;
    std::size_t index = 0;
    if (!findField(config, id, collection, index)) {
        if (error) *error = "PHYSIS field ID not found";
        return false;
    }
    Json fields = config[collection];
    fields.eraseAt(index);
    config.set(collection, std::move(fields));
    return apply(doc, config, error);
}

inline bool replaceField(MapDoc& doc, const std::string& id, const Json& entry,
                         std::string* error = nullptr) {
    Json config = read(doc);
    std::string collection;
    std::size_t index = 0;
    if (!findField(config, id, collection, index)) {
        if (error) *error = "PHYSIS field ID not found";
        return false;
    }
    if (!entry.isObject() || entry.getStr("id") != id) {
        if (error) *error = "PHYSIS field ID must remain stable";
        return false;
    }
    Json entries = config[collection];
    entries.at(index) = entry;
    config.set(collection, std::move(entries));
    return apply(doc, config, error);
}

// Scene gizmo uses one initial snapshot per mouse drag. Later motion updates
// validate before writing, then only call touched() (no extra undo snapshots).
// The caller's 'firstEdit' switches off after the first successful movement.
inline bool moveField(MapDoc& doc, const std::string& id,
                      double x, double y, double z,
                      bool firstEdit, std::string* error = nullptr) {
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) {
        if (error) *error = "PHYSIS field position is not finite";
        return false;
    }
    Json config = read(doc);
    std::string collection;
    std::size_t index = 0;
    if (!findField(config, id, collection, index)) {
        if (error) *error = "PHYSIS field ID not found";
        return false;
    }
    Json entries = config[collection];
    Json entry = entries[index];
    const Json next = point(x,y,z);
    if (entry["center"] == next) return true; // no change, no history
    entry.set("center", next);
    entries.at(index) = std::move(entry);
    config.set(collection, std::move(entries));
    if (!validate(config,error)) return false;
    if (firstEdit) doc.beginEdit();
    else doc.touched();
    Json& container = doc.data.fps.isNull() ? doc.data.extra : doc.data.fps;
    container.set("physis", std::move(config));
    return true;
}

// Editor-only metre-grid snapping. Negative positions round symmetrically;
// the result is stored in map JSON, never applied to live ECS values.
inline double snapMeters(double value, double step) {
    if (!std::isfinite(value) || !std::isfinite(step) || step <= 0.0) return value;
    return std::clamp(std::round(value / step) * step, -1000000.0, 1000000.0);
}

// Radius drag uses the same single-snapshot MapDoc transaction as moveField.
// Storms resize the OUTER radius; their eye radius remains independently authored.
inline bool resizeField(MapDoc& doc, const std::string& id, double radiusMeters,
                        bool firstEdit, std::string* error = nullptr) {
    if (!std::isfinite(radiusMeters)) {
        if (error) *error = "PHYSIS radius is not finite";
        return false;
    }
    Json config = read(doc);
    std::string collection;
    std::size_t index = 0;
    if (!findField(config, id, collection, index)) {
        if (error) *error = "PHYSIS field ID not found";
        return false;
    }
    const char* key = collection == "storms" ? "outerRadiusMeters" : "radiusMeters";
    Json entries = config[collection];
    Json entry = entries[index];
    const Json next = Json::number(radiusMeters, 3);
    if (std::abs(entry[key].toDouble() - next.toDouble()) < 0.0005) return true;
    entry.set(key, next);
    entries.at(index) = std::move(entry);
    config.set(collection, std::move(entries));
    if (!validate(config, error)) return false;
    if (firstEdit) doc.beginEdit();
    else doc.touched();
    Json& container = doc.data.fps.isNull() ? doc.data.extra : doc.data.fps;
    container.set("physis", std::move(config));
    return true;
}


// Batch position write used by the Scene's multi-selection drag. The complete
// batch is validated BEFORE a single map transaction; callers must provide
// absolute coordinates relative to the drag's original snapshot, not frame-to-
// frame deltas, to avoid accumulating quantisation errors.
struct FieldPosition {
    std::string id;
    double x = 0, y = 0, z = 0;
};

inline bool moveFields(MapDoc& doc, const std::vector<FieldPosition>& positions,
                       bool firstEdit, std::string* error = nullptr) {
    if (positions.empty()) return true;
    const auto fail = [&](const char* why) {
        if (error) *error = why;
        return false;
    };
    std::map<std::string, FieldPosition> requested;
    for (const auto& p : positions) {
        if (p.id.empty() || !std::isfinite(p.x) ||
            !std::isfinite(p.y) || !std::isfinite(p.z) ||
            std::abs(p.x) > 1000000 || std::abs(p.y) > 1000000 ||
            std::abs(p.z) > 1000000)
            return fail("PHYSIS invalid group position");
        if (!requested.emplace(p.id,p).second)
            return fail("PHYSIS duplicate group selection ID");
    }
    Json config = read(doc);
    std::size_t found = 0;
    bool changed = false;
    for (const char* key : {"windZones","storms","gravityFields"}) {
        const Json& existing = config[key];
        if (!existing.isArray()) continue;
        Json fields = existing;
        bool modified = false;
        for (std::size_t i = 0; i < fields.size(); ++i) {
            const auto it = requested.find(fields[i].getStr("id"));
            if (it == requested.end()) continue;
            ++found;
            const auto& p = it->second;
            const Json next = point(p.x,p.y,p.z);
            Json field = fields[i];
            if (field["center"] == next) continue;
            field.set("center", next);
            fields.at(i) = std::move(field);
            modified = changed = true;
        }
        if (modified) config.set(key,std::move(fields));
    }
    if (found != requested.size())
        return fail("PHYSIS group contains an unknown field ID");
    if (!changed) return true;
    if (!validate(config,error)) return false;
    if (firstEdit) doc.beginEdit();
    else doc.touched();
    Json& container = doc.data.fps.isNull() ? doc.data.extra : doc.data.fps;
    container.set("physis", std::move(config));
    return true;
}

// Duplicate selected assets across any field types in one Undo step. The
// deterministic IDs are document keys, NOT ECS entity IDs. Copies preserve
// all unknown authored properties and differ only in ID and position.
inline bool duplicateFields(MapDoc& doc, const std::vector<std::string>& ids,
                            double dx, double dy, double dz,
                            std::vector<std::string>& newIds,
                            std::string* error = nullptr) {
    const auto fail = [&](const char* why) {
        if (error) *error = why;
        return false;
    };
    if (ids.empty()) return fail("PHYSIS select fields to duplicate");
    if (!std::isfinite(dx) || !std::isfinite(dy) || !std::isfinite(dz))
        return fail("PHYSIS duplicate offset must be finite");
    Json config = read(doc);
    if (!validate(config,error)) return false;
    std::set<std::string> used;
    std::size_t count = 0;
    for (const char* key : {"windZones","storms","gravityFields"}) {
        const auto& fields = config[key];
        if (!fields.isArray()) continue;
        count += fields.size();
        for (const auto& field : fields.items())
            used.insert(field.getStr("id"));
    }
    std::vector<std::string> ordered = ids;
    std::sort(ordered.begin(),ordered.end());
    if (std::adjacent_find(ordered.begin(),ordered.end()) != ordered.end())
        return fail("PHYSIS group contains duplicate IDs");
    if (ordered.size() > 2048 - count)
        return fail("PHYSIS duplication exceeds field count limit");
    std::vector<std::string> added;
    added.reserve(ordered.size());
    for (const auto& oldId : ordered) {
        std::string collection;
        std::size_t index = 0;
        Json field;
        if (!findField(config,oldId,collection,index,&field))
            return fail("PHYSIS duplicate source ID not found");
        std::string newId;
        for (std::size_t attempt = 1; attempt <= 2049; ++attempt) {
            const std::string suffix = attempt == 1 ?
                "-copy" : "-copy-" + std::to_string(attempt);
            const std::string base = oldId.substr(0,64 - suffix.size());
            const std::string candidate = base + suffix;
            if (used.insert(candidate).second) { newId = candidate; break; }
        }
        if (newId.empty()) return fail("PHYSIS no unique duplicate ID available");
        const auto& previous = field["center"];
        if (!previous.isArray() || previous.size() != 3)
            return fail("PHYSIS duplicate source has no center");
        const double x = previous[0].toDouble()+dx;
        const double y = previous[1].toDouble()+dy;
        const double z = previous[2].toDouble()+dz;
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) ||
            std::abs(x)>1000000 || std::abs(y)>1000000 || std::abs(z)>1000000)
            return fail("PHYSIS duplicate position exceeds bounds");
        field.set("id",Json::string(newId));
        field.set("center",point(x,y,z));
        Json fields = config[collection];
        fields.push(std::move(field));
        config.set(collection,std::move(fields));
        added.push_back(std::move(newId));
    }
    if (!validate(config,error)) return false;
    if (!apply(doc,config,error)) return false;
    newIds = std::move(added);
    return true;
}

// Delete all selected map assets in a single, validated Undo transaction.
inline bool removeFields(MapDoc& doc, const std::vector<std::string>& ids,
                         std::string* error = nullptr) {
    const auto fail = [&](const char* why) {
        if (error) *error = why;
        return false;
    };
    if (ids.empty()) return fail("PHYSIS select fields to delete");
    const std::set<std::string> requested(ids.begin(),ids.end());
    if (requested.size() != ids.size())
        return fail("PHYSIS duplicate group selection ID");
    Json config = read(doc);
    std::size_t removed = 0;
    for (const char* key : {"windZones","storms","gravityFields"}) {
        const Json& old = config[key];
        if (!old.isArray()) continue;
        Json filtered = Json::array();
        for (const auto& field : old.items()) {
            if (requested.count(field.getStr("id"))) ++removed;
            else filtered.push(field);
        }
        config.set(key,std::move(filtered));
    }
    if (removed != requested.size())
        return fail("PHYSIS deletion contains unknown field ID");
    return apply(doc,config,error);
}


// Geometry-only group operation around an explicit world-space pivot.
// Yaw changes the horizontal arrangement; scale changes distances and field
// extents, but NOT wind speed, direction, storm strength or gravity strength.
// The editor operation is deliberately non-authoritative, and all outputs
// are committed as one validated MapDoc Undo transaction.
inline bool transformFields(MapDoc& doc, const std::vector<std::string>& ids,
                            double pivotX, double pivotY, double pivotZ,
                            double yawDegrees, double uniformScale,
                            std::string* error = nullptr,
                            const Json* baseline = nullptr,
                            bool firstEdit = true) {
    const auto fail = [&](const char* why) {
        if (error) *error = why;
        return false;
    };
    if (ids.empty()) return fail("PHYSIS select fields to transform");
    if (!std::isfinite(pivotX) || !std::isfinite(pivotY) ||
        !std::isfinite(pivotZ) || !std::isfinite(yawDegrees) ||
        !std::isfinite(uniformScale) || uniformScale <= 0 ||
        uniformScale > 100 || std::abs(yawDegrees) > 3600)
        return fail("PHYSIS group transform parameters out of bounds");
    const std::set<std::string> selected(ids.begin(),ids.end());
    if (selected.size()!=ids.size())
        return fail("PHYSIS duplicate group selection ID");
    Json config = baseline ? *baseline : read(doc);
    if (!validate(config,error)) return false;
    const double radians = yawDegrees * 3.14159265358979323846 / 180.0;
    const double cosine = std::cos(radians);
    const double sine = std::sin(radians);
    std::size_t found = 0;
    for (const char* key:{"windZones","storms","gravityFields"}) {
        const Json& original = config[key];
        if (!original.isArray()) continue;
        Json fields = original;
        bool modified = false;
        for (std::size_t i=0;i<fields.size();++i) {
            Json field=fields[i];
            if (!selected.count(field.getStr("id"))) continue;
            ++found;
            const auto& center=field["center"];
            const double dx=center[0].toDouble()-pivotX;
            const double dy=center[1].toDouble()-pivotY;
            const double dz=center[2].toDouble()-pivotZ;
            const double x=pivotX+uniformScale*(cosine*dx-sine*dz);
            const double y=pivotY+uniformScale*dy;
            const double z=pivotZ+uniformScale*(sine*dx+cosine*dz);
            if (!std::isfinite(x)||!std::isfinite(y)||!std::isfinite(z)||
                std::abs(x)>1000000||std::abs(y)>1000000||std::abs(z)>1000000)
                return fail("PHYSIS group transform moves field out of bounds");
            field.set("center",point(x,y,z));
            if (uniformScale != 1.0) {
                const auto scaleLength = [&](const char* fieldKey, double fallback) {
                    const double value=field[fieldKey].isNumber() ?
                        field[fieldKey].toDouble() : fallback;
                    field.set(fieldKey,Json::number(value*uniformScale,3));
                };
                if (std::string(key)=="storms") {
                    const aiverse::physis::StormEvent defaults{};
                    scaleLength("eyeRadiusMeters",defaults.eyeRadiusMeters.toDouble());
                    scaleLength("outerRadiusMeters",defaults.outerRadiusMeters.toDouble());
                    scaleLength("heightMeters",defaults.heightMeters.toDouble());
                } else if (std::string(key)=="windZones") {
                    const aiverse::physis::WindZone defaults{};
                    scaleLength("radiusMeters",defaults.radiusMeters.toDouble());
                } else {
                    const aiverse::physis::GravityField defaults{};
                    scaleLength("radiusMeters",defaults.radiusMeters.toDouble());
                }
            }
            fields.at(i)=std::move(field);
            modified=true;
        }
        if (modified) config.set(key,std::move(fields));
    }
    if (found!=selected.size())
        return fail("PHYSIS group transform contains unknown field ID");
    if (!validate(config,error)) return false;
    if (config == read(doc)) return true;
    if (!doc.data.fps.isNull() && !doc.data.fps.isObject())
        return fail("PHYSIS FPS document is not an object");
    if (firstEdit) doc.beginEdit();
    else doc.touched();
    Json& container=doc.data.fps.isNull() ? doc.data.extra:doc.data.fps;
    container.set("physis",std::move(config));
    return true;
}


// Stage 22: deterministic editor-only pivot selection, shared by numeric
// transforms and the camera gizmo. Does not add ECS/map schema fields.
enum class FieldPivotMode : std::uint8_t {
    Active, Median, BoundsCenter, WorldOrigin, Custom
};
struct FieldPivot {
    double x = 0, y = 0, z = 0;
};

inline bool computeFieldPivot(const Json& config,
                              const std::vector<std::string>& ids,
                              const std::string& activeId, FieldPivotMode mode,
                              const FieldPivot& custom, FieldPivot& result,
                              std::string* error = nullptr) {
    const auto fail=[&](const char* message) {
        if (error) *error=message;
        return false;
    };
    if (ids.empty()) return fail("PHYSIS pivot needs at least one selected field");
    const std::set<std::string> wanted(ids.begin(),ids.end());
    if (wanted.size()!=ids.size())
        return fail("PHYSIS pivot has duplicate selected IDs");
    // Pivot is sampled every Scene frame: validate only selected coordinates
    // here. Mutating operations still run the complete native field parser.
    if (!config.isObject()) return fail("PHYSIS pivot map config is not an object");

    FieldPivot sum{},lo{1000001,1000001,1000001};
    FieldPivot hi{-1000001,-1000001,-1000001}, active{};
    bool hasActive=false;
    std::size_t count=0;
    for (const char* key:{"windZones","storms","gravityFields"}) {
        const Json& fields=config[key];
        if (!fields.isArray()) continue;
        for (const Json& field:fields.items()) {
            const std::string id=field.getStr("id");
            if (!wanted.count(id)) continue;
            const auto& coords=field["center"];
            if (!coords.isArray()||coords.size()!=3)
                return fail("PHYSIS selected field lacks a 3D position");
            for (std::size_t i=0;i<3;++i)
                if (!coords[i].isNumber()||
                    !std::isfinite(coords[i].toDouble())||
                    std::abs(coords[i].toDouble())>1000000)
                    return fail("PHYSIS selected field position invalid");
            const FieldPivot p{coords[0].toDouble(),coords[1].toDouble(),
                               coords[2].toDouble()};
            sum.x+=p.x;sum.y+=p.y;sum.z+=p.z;
            lo.x=std::min(lo.x,p.x);lo.y=std::min(lo.y,p.y);lo.z=std::min(lo.z,p.z);
            hi.x=std::max(hi.x,p.x);hi.y=std::max(hi.y,p.y);hi.z=std::max(hi.z,p.z);
            if (id==activeId) {active=p;hasActive=true;}
            ++count;
        }
    }
    if (count!=wanted.size()) return fail("PHYSIS pivot contains unknown field ID");
    FieldPivot selected{};
    switch (mode) {
        case FieldPivotMode::Active:
            if (!hasActive) return fail("PHYSIS active pivot is not selected");
            selected=active;
            break;
        case FieldPivotMode::Median:
            selected={sum.x/double(count),sum.y/double(count),sum.z/double(count)};
            break;
        case FieldPivotMode::BoundsCenter:
            selected={(lo.x+hi.x)*0.5,(lo.y+hi.y)*0.5,(lo.z+hi.z)*0.5};
            break;
        case FieldPivotMode::WorldOrigin:
            break;
        case FieldPivotMode::Custom:
            selected=custom;
            break;
        default:
            return fail("PHYSIS unknown pivot mode");
    }
    if (!std::isfinite(selected.x)||!std::isfinite(selected.y)||
        !std::isfinite(selected.z)||
        std::abs(selected.x)>1000000||std::abs(selected.y)>1000000||
        std::abs(selected.z)>1000000)
        return fail("PHYSIS pivot outside world bounds");
    result=selected; // caller result unchanged on failure
    return true;
}

} // namespace aiverse::editor::physis_authoring
