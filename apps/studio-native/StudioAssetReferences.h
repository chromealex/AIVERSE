// Studio reference graph v1. Read-only discovery of documented asset-bearing
// fields, independent of UI and genre runtime; no speculative string scanning.
// materials.json stores project-relative paths; GUID is resolved via .meta index.
#pragma once
#include "apps/studio-native/StudioAssetCatalog.h"
#include "engine/data/Json.h"
#include <map>
#include <string>
#include <vector>

namespace aiverse::studio {

struct StudioAssetReference {
    std::string document; // materials.json or rules.json (logical merged rules)
    std::string owner;    // library material name or rules type name
    std::string field;    // explicitly documented asset-bearing slot
    std::string path;     // runtime-compatible project-relative path
    std::string guid;     // empty for unresolved/legacy paths
    bool writable = false; // currently only materials.json slots support repair
};

inline constexpr const char* kStudioTextureFields[] = {
    "texture", "normalMap", "emissiveMap", "teamMask",
    "metallicRoughnessMap", "occlusionMap"
};

// Build GUID lookup once; no implicit rebuild or filesystem write in discovery.
inline std::map<std::string, std::string> studioReferenceGuids(
    const std::vector<StudioAssetEntry>& assets) {
    std::map<std::string, std::string> result;
    for (const auto& asset : assets)
        if (!asset.directory && !asset.guid.empty())
            result.emplace("content/" + asset.path, asset.guid);
    return result;
}

inline std::vector<StudioAssetReference> studioCollectAssetReferences(
    const Json& materials, const Json& rules,
    const std::vector<StudioAssetEntry>& assets) {
    const auto guids = studioReferenceGuids(assets);
    std::vector<StudioAssetReference> out;
    auto append = [&](const char* document, const std::string& owner,
                      const std::string& field, const Json& value, bool writable) {
        if (!value.isString() || value.str().empty()) return;
        const auto found = guids.find(value.str());
        StudioAssetReference ref;
        ref.document = document; ref.owner = owner; ref.field = field;
        ref.path = value.str(); ref.writable = writable;
        if (found != guids.end()) ref.guid = found->second;
        out.push_back(std::move(ref));
    };
    for (const auto& [name, material] : materials.members()) {
        if (!material.isObject()) continue;
        for (const char* field : kStudioTextureFields)
            append("materials.json", name, field, material[field], true);
    }
    // Actual rules schema: types.<type>.view.model may be string,
    // string-array, or entries with a "file" string. The runtime uses
    // those paths directly; never rewrite rules/includes here.
    for (const auto& [name, type] : rules["types"].members()) {
        const Json& model = type["view"]["model"];
        if (model.isString()) {
            append("rules.json", name, "view.model", model, false);
        } else if (model.isArray()) {
            std::size_t index = 0;
            for (const auto& entry : model.items()) {
                const std::string key = "view.model[" + std::to_string(index++) + "]";
                if (entry.isString()) append("rules.json", name, key, entry, false);
                else if (entry.isObject())
                    append("rules.json", name, key + ".file", entry["file"], false);
            }
        }
    }
    return out;
}

// Apply only validated old->new mappings to known material slots. Caller
// persists via existing engine material validation + save routine.
inline std::size_t studioRemapMaterialReferences(
    Json& materials, const std::map<std::string, std::string>& relocated) {
    if (relocated.empty()) return 0;
    std::vector<std::pair<std::string, Json>> edits;
    std::size_t updated = 0;
    for (const auto& [name, material] : materials.members()) {
        if (!material.isObject()) continue;
        Json candidate = material;
        bool changed = false;
        for (const char* field : kStudioTextureFields) {
            const Json& current = candidate[field];
            if (!current.isString()) continue;
            const auto match = relocated.find(current.str());
            if (match == relocated.end()) continue;
            candidate.set(field, Json::string(match->second));
            ++updated;
            changed = true;
        }
        if (changed) edits.emplace_back(name, std::move(candidate));
    }
    for (auto& edit : edits) materials.set(edit.first, std::move(edit.second));
    return updated;
}

// Resolve the set of known dependencies affected by an internal move.
// This is a preview, not a rename executor. Exact paths and folder descendants
// are matched; an unaffected resource in another folder must not match.
inline Json studioMoveReferenceImpact(const Json& transfer,
    const std::vector<StudioAssetReference>& references) {
    Json result=Json::array();
    for (const auto& item:transfer["items"].items()) {
        if (!item["id"].isNumber() || !item["from"].isString() ||
            !item["to"].isString()) continue;
        const std::string source="content/"+item.getStr("from");
        const std::string destination="content/"+item.getStr("to");
        if (source==destination) continue;
        const bool folder=item.getBool("directory");
        for (const auto& ref:references) {
            const bool exact=ref.path==source;
            const bool inside=folder && ref.path.size()>source.size() &&
                ref.path.compare(0,source.size(),source)==0 &&
                ref.path[source.size()]=='/';
            if (!exact && !inside) continue;
            Json row=Json::object();
            row.set("id",item["id"]);
            row.set("document",Json::string(ref.document));
            row.set("owner",Json::string(ref.owner));
            row.set("field",Json::string(ref.field));
            row.set("from",Json::string(ref.path));
            row.set("to",Json::string(destination+ref.path.substr(source.size())));
            row.set("writable",Json::fromBool(ref.writable));
            result.push(std::move(row));
        }
    }
    return result;
}

inline Json studioReferenceGraphJson(const std::vector<StudioAssetReference>& refs) {
    Json result = Json::array();
    for (const auto& ref : refs) {
        Json row = Json::object();
        row.set("document", Json::string(ref.document));
        row.set("owner", Json::string(ref.owner));
        row.set("field", Json::string(ref.field));
        row.set("path", Json::string(ref.path));
        row.set("guid", Json::string(ref.guid));
        row.set("writable", Json::fromBool(ref.writable));
        result.push(std::move(row));
    }
    return result;
}

} // namespace aiverse::studio
