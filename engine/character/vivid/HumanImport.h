// VIVID open asset pipeline: existing glTF/FBX/OBJ model import plus
// versioned authored morph CSR and DNA rig/material mapping. No proprietary
// Epic MetaHuman DNA parser is required for the public interchange format.
#pragma once
#include "HumanPreview.h"
#include "HumanAppearance.h"
#include <optional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace aiverse::vivid {
struct HumanLodEntry {
    std::string meshName;
    std::string sourceMeshName;
    std::string mappingPath;
};
struct HumanImportManifest {
    std::uint32_t version=1;
    std::string modelPath;
    std::string dnaPath;
    std::string appearancePath; // optional versioned imported-surface recipe
    std::string morphPath; // optional; rig-only face is supported
    // Optional named closed proxy mesh, authored directly in groom-local space.
    // It must have no skin and must not require a node transform.
    std::string scalpColliderMesh;
    std::vector<HumanLodEntry> facialLods; // 0..8 immutable mapping sidecars
    std::vector<ShapeRoute> routes;
    std::map<std::string,HumanSurface> materials; // imported material name -> type
    bool authoredHair=false; // baked/imported groom: do not generate an extra wig
    void validate() const;
    static HumanImportManifest fromJson(const Json& src);
    Json toJson() const;
};
struct HumanImportedAsset {
    HumanDNA dna;
    std::optional<HumanAppearance> appearance;
    std::size_t sourceVertexCount=0;
    std::unique_ptr<render::Model> model;
    SparseMorphLibrary facialShapes;
    FaceShapeBinding facialBinding;
    HumanRigBinding rig;
};
// Reads an external user-authored package with relative model, DNA and
// morph paths using render::FileReader. Vertex IDs in morph JSON refer
// to Model::vertices after prepareModelUpload() has stabilized topology.
HumanImportedAsset importHumanAsset(const HumanImportManifest& manifest,
                                    const render::FileReader& reader);
// Morph file schema v1: {version:1, vertices:N,
// targets:[{name:"jawOpen",deltas:[{vertex:0,position:[x,y,z],
// normal:[x,y,z],tangent:[x,y,z]}]}]}.
SparseMorphLibrary parseHumanMorphs(const Json& data,std::size_t vertices);
} // namespace aiverse::vivid
