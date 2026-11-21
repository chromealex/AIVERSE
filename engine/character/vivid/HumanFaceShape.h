#pragma once
#include "HumanAppearance.h"
namespace aiverse::vivid {
// Topology-preserving identity edits in the selected head's world bind frame.
// Landmarks use normalized head bounds; photo XY landmarks preserve depth.
struct HumanFaceShape {
 std::vector<std::string> meshes;
 std::map<std::string,float> values;
 struct Landmark {render::float3 from,to;float radius=.25f,weight=1;};
 std::vector<Landmark> landmarks;
 static HumanFaceShape fromJson(const Json&);
 Json toJson() const;
 static Json schema();
};
void applyHumanFaceShape(render::Model&,const HumanFaceShape&);
// A scan supplies sparse surface correspondences in aligned world coordinates.
// This is bounded template fitting, not topology/rig generation from a scan.
HumanFaceShape faceShapeFromScan(const render::Model& head,const render::Model& scan,
 const HumanFaceShape& selection,float maximumDistance,float strength);
// Viewport sculpt: intersect a model-space ray with the selected face meshes
// in bind pose (same space as the landmarks) and return the hit and surface
// normal in normalized head coordinates (0..1 box of the selected meshes).
struct HumanFaceHit {render::float3 point,normal;float distance=0;};
// CPU triangles of the selected face meshes in bind pose plus their bounds:
// built while the Model still has CPU geometry (before GPU publication).
struct HumanFacePickSurface {std::vector<render::float3> triangles;render::float3 lo{},hi{};bool empty() const noexcept{return triangles.empty();}};
HumanFacePickSurface humanFacePickSurface(const render::Model& model,const HumanFaceShape& selection);
// Skin texture UV at a point of the normalized head box (x,y in 0..1, front
// face preferred): finds the nearest front vertex of the selected meshes whose
// material is one of skinMaterials (empty = any). Used to place makeup layers.
bool humanFaceUvAt(const render::Model& model,const HumanFaceShape& selection,
                   const std::vector<std::string>& skinMaterials,float x,float y,float& u,float& v);
bool raycastHumanFace(const HumanFacePickSurface& surface,render::float3 origin,render::float3 direction,HumanFaceHit& hit);
bool raycastHumanFace(const render::Model& model,const HumanFaceShape& selection,
 render::float3 origin,render::float3 direction,HumanFaceHit& hit);
// One brush stroke: a landmark pushing (strength>0) or pulling (<0) the
// surface along its normal. radius is in normalized head units (0.01..1).
HumanFaceShape::Landmark humanSculptLandmark(const HumanFaceHit& hit,float radius,float strength);
}
