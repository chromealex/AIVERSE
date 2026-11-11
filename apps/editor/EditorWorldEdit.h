// Edits of several map entities at once (Hierarchy context menus, drag onto a player, Inspector details):
// each is one undo step of the map document.
#pragma once
#include <string>
#include <vector>
#include "core/MapDoc.h"

namespace aiverse::editor::worldedit {

inline bool valid(const MapDoc& doc, int i) { return i >= 0 && i < int(doc.data.entities.size()); }

inline void setOwner(MapDoc& doc, const std::vector<int>& idx, int owner) {
    bool any = false;
    for (int i : idx) any |= valid(doc, i) && doc.data.entities[size_t(i)].owner != owner;
    if (!any) return;
    doc.beginEdit();
    for (int i : idx) if (valid(doc, i)) doc.data.entities[size_t(i)].owner = owner;
    doc.touched();
}

inline void setType(MapDoc& doc, const std::vector<int>& idx, const std::string& type) {
    bool any = false;
    for (int i : idx) any |= valid(doc, i) && doc.data.entities[size_t(i)].type != type;
    if (!any) return;
    doc.beginEdit();
    for (int i : idx) if (valid(doc, i)) doc.data.entities[size_t(i)].type = type;
    doc.touched();
}

// Copies next to the originals (+1 cell); map ids of the copies get a fresh unique id. Returns their indices.
inline std::vector<int> duplicate(MapDoc& doc, const std::vector<int>& idx, float dx = 1, float dz = 1) {
    std::vector<int> out;
    if (idx.empty()) return out;
    doc.beginEdit();
    for (int i : idx) {
        if (!valid(doc, i)) continue;
        DocEntity e = doc.data.entities[size_t(i)];
        if (!e.id.empty()) e.id = doc.uniqueId(e.id);
        e.x += dx; e.z += dz;
        if (e.at.isObject()) {   // planets: move along the face
            e.at.set("x", Json::number(double(e.x), 2));
            e.at.set("y", Json::number(double(e.z), 2));
        }
        doc.data.entities.push_back(e);
        out.push_back(int(doc.data.entities.size() - 1));
    }
    doc.touched();
    return out;
}

inline void move(MapDoc& doc, const std::vector<int>& idx, float dx, float dz) {
    for (int i : idx) {
        if (!valid(doc, i)) continue;
        auto& e = doc.data.entities[size_t(i)];
        e.x += dx; e.z += dz;
        if (e.at.isObject()) { e.at.set("x", Json::number(double(e.x), 2)); e.at.set("y", Json::number(double(e.z), 2)); }
    }
    doc.touched();
}

} // namespace aiverse::editor::worldedit
