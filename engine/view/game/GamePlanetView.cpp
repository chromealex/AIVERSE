// GameView on planet maps (docs/planets.md §5): bodies from the map's `world` block, the orbit camera,
// objects placed on the sphere (oriented to the local up), selection rings, bars, fog of war, picking.
// The flat path (GameView.cpp / GameTerrain.cpp) is untouched: planetMode() is false without a world block.
//
// Match maps: bodies, orbits, corner heights and cell flags come from the simulation's Surface
// (setSimWorld); objects carry their body and body-local position (sceneFromSphere), units in transit their
// world position. Editor maps without a simulation read the map JSON (setWorld).
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include "../../game/GameCore.h"
#include "GameView.h"
#include "engine/view/View.h"

namespace aiverse::game {

using namespace render;

namespace {
void applyLiveSeaLevels(PlanetWorld& world,const MapGrid& grid){
    for(size_t i=0;i<world.bodies.size();++i){
        const auto radius=grid.liveSeaRadius(int(i));
        if(radius>Fixed{})world.bodies[i].seaLevel=float(radius.toDouble())-world.bodies[i].radius;
    }
}
uint32_t ringColor(const SceneObject& o) { return o.selected ? (o.owner == 0 ? rgba(255, 255, 160, 230) : rgba(120, 255, 120, 230)) : rgba(255, 255, 255, 130); }
}

void GameView::setWorld(const Json& map) {
    planetDetailBaseline_.clear();
    fog_.setBlur({});
    PlanetWorld w = loadPlanetWorld(map, &warnings_);
    loadPlanetDetailFiles(w, map, read_, &warnings_);
    discardPendingSurfaceLease();framePublication_.invalidate();
    if(dev_)planetFlora_.setAuthoredSpecies(*dev_,w.forestManifest,read_);
    planets_.setWorld(w);
    planetVision_.clear();
    rebuildPlanetFlora();
    if (!planets_.active()) return;
    planetCam_.reset(planets_.world(), w.start, planets_.surface(w.start), 0);
}

void GameView::rebuildPlanetFlora() {
    if (!dev_) return;
    std::vector<const PlanetSurface*> surfaces;
    for (size_t i = 0; i < planets_.world().bodies.size(); ++i) surfaces.push_back(planets_.surface(int(i)));
    planetFlora_.setWorld(*dev_, planets_.world(), surfaces);
}

void GameView::updateWorld(const Json& map) {
    PlanetWorld w = loadPlanetWorld(map);
    loadPlanetDetailFiles(w, map, read_, nullptr);
    const PlanetWorld& cur = planets_.world();
    bool same = w.bodies.size() == cur.bodies.size() && !w.empty() &&
        w.forestManifest.dump(0)==cur.forestManifest.dump(0);
    for (size_t i = 0; same && i < w.bodies.size(); ++i) {
        const PlanetBody &a = w.bodies[i], &b = cur.bodies[i];
        same = a.id == b.id && a.kind == b.kind && a.faceSize == b.faceSize && a.radius == b.radius && a.seaLevel == b.seaLevel &&
               a.parent == b.parent && a.orbitRadius == b.orbitRadius && a.period == b.period && a.phase == b.phase && a.atmosphere == b.atmosphere &&
               a.style == b.style && a.detailScale == b.detailScale && a.roughness == b.roughness && a.craters == b.craters &&
               a.detailK == b.detailK && a.detail.empty() == b.detail.empty();
    }
    if (!same) {
        const int body = planetCam_.body();
        const float3 dir = planetCam_.dir();
        const float dist = planetCam_.distance;
        setWorld(map);
        if (planets_.active() && body < int(planets_.world().bodies.size())) { // keep the view while editing
            planetCam_.reset(planets_.world(), body, planets_.surface(body), planetSeconds_, dir);
            planetCam_.distance = dist;
        }
        return;
    }
    for (size_t i = 0; i < w.bodies.size(); ++i) {
        if (!w.bodies[i].surface()) continue;
        planets_.updateFlags(int(i), w.bodies[i].flags);
        planets_.updateHeights(int(i), w.bodies[i].corners);
        if (!w.bodies[i].detail.empty() && w.bodies[i].detail != cur.bodies[i].detail && planets_.detailScale(int(i)) &&
            !planets_.updateDetail(int(i), w.bodies[i].detail)) { // drawn at another scale (tier cap): rebuild the body
            const int body = planetCam_.body();
            const float3 dir = planetCam_.dir();
            const float dist = planetCam_.distance;
            setWorld(map);
            if (planets_.active() && body < int(planets_.world().bodies.size())) {
                planetCam_.reset(planets_.world(), body, planets_.surface(body), planetSeconds_, dir);
                planetCam_.distance = dist;
            }
            return;
        }
    }
    rebuildPlanetFlora();   // the ground moved: sites follow the new heights
}

void GameView::setSimWorld(std::shared_ptr<const Surface> surf, const MapGrid& grid, const Json& map) {
    if (surf) { // fog blur over the cube-sphere neighbourhood (across face seams, never into other bodies / void cells)
        auto blur = std::make_shared<PlanetFieldBlur>(surf);
        fog_.setBlur([blur](std::vector<uint8_t>& f, int radius) { (*blur)(f, radius); });
    } else fog_.setBlur({});
    PlanetWorld visual = loadPlanetWorld(map, &warnings_);
    loadPlanetDetailFiles(visual, map, read_, &warnings_);
    PlanetWorld w = planetWorldFromSim(std::move(surf), grid.flags, visual);
    applyLiveSeaLevels(w,grid);
    discardPendingSurfaceLease();framePublication_.invalidate();
    if(dev_)planetFlora_.setAuthoredSpecies(*dev_,w.forestManifest,read_);
    planetDetailBaseline_.clear();planetDetailBaseline_.resize(w.bodies.size());
    for(size_t i=0;i<w.bodies.size();++i){
        const auto& body=w.bodies[i];
        if(!body.detail.empty())planetDetailBaseline_[i]={body.corners,body.detail,body.detailK};
    }
    planets_.setWorld(w);
    planetVision_.clear();
    rebuildPlanetFlora();
    if (!planets_.active()) return;
    planetCam_.reset(planets_.world(), w.start, planets_.surface(w.start), 0);
}

bool GameView::syncSimPlanetTerrain(const MapGrid& grid){
    const auto& current=planets_.world();
    if(!current.sim)return true; // editor world has its own explicit update path
    if(!grid.surf||!dev_||grid.surf->tab!=current.sim->tab)return false;
    bool seaChanged=false;
    for(size_t i=0;i<current.bodies.size();++i){
        const auto radius=grid.liveSeaRadius(int(i));
        if(radius>Fixed{}&&float(radius.toDouble())-current.bodies[i].radius!=current.bodies[i].seaLevel)seaChanged=true;
    }
    if(grid.surf==current.sim&&!seaChanged)return true;
    auto next=planetWorldFromSim(grid.surf,grid.flags,current);
    applyLiveSeaLevels(next,grid);
    bool rebuild=false;
    for(size_t i=0;i<next.bodies.size();++i){
        auto& body=next.bodies[i];
        if(body.corners==current.bodies[i].corners)continue;
        if(i<planetDetailBaseline_.size()&&planetDetailBaseline_[i].scale>0){
            // Explicit authored detail is absolute, not a delta. Avoid
            // drawing stale fine relief over edited ground. Restore exact
            // authored detail when rollback returns to its original corners.
            const auto& base=planetDetailBaseline_[i];
            if(body.corners==base.corners){body.detail=base.detail;body.detailK=base.scale;}
            else {body.detail.clear();body.detailK=0;}
            rebuild=true;
        }
    }
    if(rebuild){
        planets_.setWorld(next);planetVision_.clear();
    }else{
        for(size_t i=0;i<next.bodies.size();++i){
            if(!next.bodies[i].surface())continue;
            planets_.updateSeaLevel(int(i),next.bodies[i].seaLevel);
            if(next.bodies[i].corners!=current.bodies[i].corners)
                planets_.updateHeights(int(i),next.bodies[i].corners);
            planets_.updateFlags(int(i),next.bodies[i].flags);
        }
        planets_.bindSimSurface(grid.surf);
    }
    rebuildPlanetFlora(); // existing cosmetic scatter owner; not rooted ECS lifecycle
    planetCam_.update(planets_.world(),planets_.surface(planetCam_.body()),planetSeconds_,0);
    pframe_=planetCam_.frame();
    return true;
}
bool GameView::simToBody(float3 sim, int& body, float3& local) {
    if (!onSphereF(sim)) return false;
    body = bodyAtF(sim);
    local = sim - anchorF(body);
    return true;
}

bool GameView::facePoint(int body, int face, float x, float y, float3& local) const {
    const PlanetSurface* s = planets_.surface(body);
    if (!s || face < 0 || face > 5) return false;
    local = s->surfacePoint(s->dirFromFace(face, x, y));
    return true;
}

mat4 GameView::planetFxSpace() const {
    return pframe_.matrix() * mat4::translate(planets_.world().center(planetCam_.body(), planetSeconds_));
}

bool GameView::planetFxPlace(float3 sim, float lift, float3& out, float3& up) const {
    int body; float3 local;
    if (!simToBody(sim, body, local) || body != planetCam_.body()) return false;
    const PlanetSurface* s = planets_.surface(body);
    up = normalize(local);
    if (!s || lift < -1e8f) { out = local; return true; }
    const float3 ground = s->surfacePoint(up);
    const float above = length(local) - length(ground);
    out = above > 0.6f ? local + up * lift : ground + up * lift;   // orbit / flying: from where it is
    return true;
}

float GameView::planetFxAltitude(float3 local) const {
    const PlanetSurface* s = planets_.surface(planetCam_.body());
    if (!s) return 1e9f;
    const float l = length(local);
    return l - (s->radius() + s->heightAt(local * (1.0f / std::max(l, 1e-6f))));
}

float3 GameView::bodyToView(int body, float3 local) const {
    return pframe_.toView(planets_.world().center(body, planetSeconds_) + local);
}

const Camera& GameView::updatePlanetCamera(double seconds, float dt, int viewH) {
    (void)viewH;
    planetSeconds_ = seconds;
    const int b = planetCam_.body();
    planetCam_.update(planets_.world(), planets_.surface(b), seconds, dt);
    pframe_ = planetCam_.frame();
    return planetCam_.camera();
}

bool GameView::pickPlanet(const Camera& cam, float sx, float sy, float w, float h, int& body, float3& local) const {
    float3 o, d;
    if (!planets_.active() || !cam.screenRay(sx, sy, w, h, o, d)) return false;
    float best = 1e30f;
    const PlanetWorld& W = planets_.world();
    for (size_t i = 0; i < W.bodies.size(); ++i) {
        const PlanetSurface* s = planets_.surface(int(i));
        if (!s) continue;
        const float3 c = bodyToView(int(i), {0, 0, 0});
        // body-local ray (world orientation)
        const float3 lo = pframe_.dirToWorld(o - c), ld = pframe_.dirToWorld(d);
        float3 hit;
        if (!s->raycast(lo, ld, hit, W.bodies[i].seaLevel)) continue;
        const float t = length(hit - lo);
        if (t < best) { best = t; body = int(i); local = hit; }
    }
    return best < 1e30f;
}

bool GameView::placeObject(const SceneObject& o, int& body, float3& ground, float3& up, float3& east, float3& south, float& lift) const {
    float3 local;
    lift = 0;
    if (o.body < 0) return false;
    body = o.body; local = o.pos;
    const PlanetSurface* s = planets_.surface(body);
    if (!s) return false;
    up = normalize(local);
    ground = s->surfacePoint(up);
    const float sea = planets_.world().bodies[size_t(body)].seaLevel;
    if (sea > -1e8f && length(ground) < s->radius() + sea) ground = up * (s->radius() + sea);   // naval units float
    lift = std::max(0.0f, length(local) - length(ground));   // orbit layer above the ground (sim positions)
    if (lift < 0.6f) lift = 0;                                 // ground units: on the drawn surface
    // tangent frame: flat-map axes (x east = +a, z south = +b of the face grid) so flat facings keep working
    int f; float a, b;
    s->faceFromDir(up, f, a, b);
    const float e = 0.25f;
    float3 da = s->dirFromFace(f, std::min(a + e, float(s->faceSize())), b) - s->dirFromFace(f, std::max(a - e, 0.0f), b);
    da = da - up * dot(da, up);
    east = normalize(da);
    south = cross(east, up);
    return true;
}

bool GameView::planetFxObjectPose(const SceneObject& o,float3& position,float3& direction) const {
    if(o.body!=planetCam_.body() || o.body<0 || dot(o.pos,o.pos)<=1e-10f)return false;
    int body;float3 g,up,east,south;float lift;
    if(!placeObject(o,body,g,up,east,south,lift))return false;
    position=g+up*lift;
    const float len=length(o.facing);
    direction=std::isfinite(len)&&len>1e-5f?o.facing*(1.f/len):up;
    return std::isfinite(position.x)&&std::isfinite(position.y)&&std::isfinite(position.z)&&
           std::isfinite(direction.x)&&std::isfinite(direction.y)&&std::isfinite(direction.z);
}
bool GameView::planetFxTerrain(float3 local,float& relief,float& clearance,float3& up) const {
    const auto* surface=planets_.surface(planetCam_.body());
    const float radius=length(local);
    if(!surface||!std::isfinite(radius)||radius<=1e-5f)return false;
    up=local*(1.f/radius);
    relief=surface->heightAt(up);
    clearance=radius-surface->radius()-relief;
    return std::isfinite(relief)&&std::isfinite(clearance);
}

bool GameView::planetPlacement(const SceneObject& o, mat4& m) const {
    int body; float3 g, up, east, south; float lift;
    if (!placeObject(o, body, g, up, east, south, lift)) return false;
    const float3 p = bodyToView(body, g + up * lift), U = pframe_.dirToView(up), E = pframe_.dirToView(east), S = pframe_.dirToView(south);
    m = mat4{};
    m.at(0, 0) = E.x; m.at(1, 0) = E.y; m.at(2, 0) = E.z;
    m.at(0, 1) = U.x; m.at(1, 1) = U.y; m.at(2, 1) = U.z;
    m.at(0, 2) = S.x; m.at(1, 2) = S.y; m.at(2, 2) = S.z;
    m.at(0, 3) = p.x; m.at(1, 3) = p.y; m.at(2, 3) = p.z;
    return true;
}

float3 GameView::objectViewPos(const SceneObject& o, float lift) const {
    if (!planets_.active()) return {o.pos.x, lift, o.pos.z};
    int body; float3 g, up, e, sth; float l;
    if (o.body == SceneObject::kBodyTransit) return pframe_.toView(o.pos);
    if (!placeObject(o, body, g, up, e, sth, l)) return {o.pos.x, lift, o.pos.z};
    return bodyToView(body, g + up * (l + lift));
}

void GameView::drawPlanets(Renderer3D& r, const GameScene& s, double time, const std::vector<uint8_t>* vision, const MapGrid& grid) {
    const float dt = lastTime_ < 0 ? 0.0f : float(std::clamp(time - lastTime_, 0.0, 0.1));
    lastTime_ = time;
    ++frame_;
    viewProj_ = r.viewProj();
    camPos_ = r.cameraPos();
    events_.clear();
    effects_.clear();
    bars_.assign(s.objects.size(), {});
    barOk_.assign(s.objects.size(), 0);
    planetSeconds_ = s.seconds;
    const float blend = std::max(planetCam_.planetBlend(), planetCam_.systemBlend());
    planetNoShadow_ = blend > 0.5f;
    // fog of war per body: the viewer's vision of the stacked grid, faded by RtsFog and blurred over the
    // cube-sphere neighbourhood (setSimWorld), then cut per body
    const PlanetWorld& Wd = planets_.world();
    planetVision_.resize(Wd.bodies.size());
    const bool fogOn = vision && !vision->empty() && Wd.sim && grid.w > 0 && vision->size() == size_t(grid.w * grid.h);
    if (fogOn) fog_.update(*vision, grid.w, grid.h, time);
    for (size_t bi = 0; bi < Wd.bodies.size(); ++bi) {
        if (!Wd.bodies[bi].surface()) continue;
        if (!fogOn) { planetVision_[bi].clear(); planets_.setVision(int(bi), nullptr, fog_.look()); continue; }
        const int N = Wd.bodies[bi].faceSize;
        const std::vector<uint8_t>& fld = fog_.field();
        std::vector<uint8_t>& pv = planetVision_[bi];
        pv.assign(size_t(6 * N * N), 0);
        for (int f = 0; f < 6; ++f)
            for (int y = 0; y < N; ++y)
                for (int x = 0; x < N; ++x) {
                    const size_t c = size_t(Wd.sim->gridCell(int(bi), f, x, y));
                    pv[(size_t(f) * size_t(N) + size_t(y)) * size_t(N) + size_t(x)] = c < fld.size() ? fld[c] : 0;
                }
        planets_.setVision(int(bi), &pv, fog_.look());
    }
    if (std::getenv("AIVERSE_PLANET_DEBUG")) { // AIVERSE_PLANET_DEBUG: what the scene holds (screenshot set-ups)
        int tr = 0, orbit = 0, moon = 0;
        for (auto& o : s.objects) {
            if (o.isProjectile) continue;
            if (o.body == SceneObject::kBodyTransit) ++tr;
            else if (o.body >= 0 && o.body != Wd.start) ++moon;
            else if (o.type < rules_->types.size() && (rules_->types[o.type].layer & kLayerOrbit)) ++orbit;
        }
        std::fprintf(stderr, "[planet] t=%.0f transit %d orbit-units %d on-other-bodies %d\n", s.seconds, tr, orbit, moon);
    }
    planets_.draw(r, pframe_, s.seconds, planetCam_.body(), blend);
    drawPlanetCaves(r,pframe_,s.seconds);
    if (dev_ && rules_ && planetGroundFx_.draw(*dev_, r, planets_, pframe_, s, time, vision, grid, groundFx_, *rules_))
        for (size_t bi = 0; bi < Wd.bodies.size(); ++bi) { // rock dug out (seen): the terrain under it becomes ground
            if (!Wd.bodies[bi].surface()) continue;
            const int N = Wd.bodies[bi].faceSize;
            std::vector<uint8_t> fl(size_t(6 * N * N), kCellGround);
            for (int f = 0; f < 6; ++f)
                for (int y = 0; y < N; ++y)
                    for (int x = 0; x < N; ++x) {
                        const size_t c = size_t(Wd.sim->gridCell(int(bi), f, x, y));
                        if (c < grid.flags.size()) fl[(size_t(f) * size_t(N) + size_t(y)) * size_t(N) + size_t(x)] = grid.flags[c];
                    }
            planets_.updateFlags(int(bi), fl);
        }
    handleEvents(s);
    const float3 eye = r.camera().eye();
    const PlanetWorld& W = planets_.world();
    // visibility of a body-local point: not behind its body (horizon test against the sphere)
    auto occluded = [&](int body, float3 pv) {
        const float3 c = bodyToView(body, {0, 0, 0});
        const float R = W.bodies[size_t(body)].radius * 0.985f;
        const float3 d = pv - eye;
        const float len = length(d);
        if (len < 1e-4f) return false;
        const float3 dir = d * (1.0f / len), oc = eye - c;
        const float bq = dot(oc, dir), cq = dot(oc, oc) - R * R, disc = bq * bq - cq;
        if (disc <= 0) return false;
        const float t = -bq - std::sqrt(disc);
        return t > 0 && t < len;
    };
    auto frameAt = [&](int body, float3 g, float3 up, float3 east, float3 south) {
        const float3 p = bodyToView(body, g), U = pframe_.dirToView(up), E = pframe_.dirToView(east), S = pframe_.dirToView(south);
        mat4 m;
        m.at(0, 0) = E.x; m.at(1, 0) = E.y; m.at(2, 0) = E.z;
        m.at(0, 1) = U.x; m.at(1, 1) = U.y; m.at(2, 1) = U.z;
        m.at(0, 2) = S.x; m.at(1, 2) = S.y; m.at(2, 2) = S.z;
        m.at(0, 3) = p.x; m.at(1, 3) = p.y; m.at(2, 3) = p.z;
        return m;
    };
    auto ring = [&](const mat4& m, float rad, uint32_t col, float lift) {
        const int seg = 32;
        float3 prev;
        for (int i = 0; i <= seg; ++i) {
            const float a = 6.2831853f * float(i) / float(seg);
            const float3 p = m.transformPoint({std::cos(a) * rad, lift, std::sin(a) * rad});
            if (i) r.line(prev, p, col);
            prev = p;
        }
    };
    auto tangentFrame = [&](int body, float3 up, float3& east, float3& south) {
        const PlanetSurface* sf = planets_.surface(body);
        int f; float a, b;
        sf->faceFromDir(up, f, a, b);
        float3 da = sf->dirFromFace(f, std::min(a + 0.25f, float(sf->faceSize())), b) - sf->dirFromFace(f, std::max(a - 0.25f, 0.0f), b);
        east = normalize(da - up * dot(da, up));
        south = cross(east, up);
    };
    // TerraWeave trees and rocks: on the visible side of their body, near enough to read
    if (blend < 0.5f) {
        const float floraFar = 260.0f;
        for (const PlanetFlora::Instance& in : planetFlora_.instances()) {
            if(PlanetFlora::treeProto(in.proto)&&size_t(in.body)<s.planetForest.managedBodies.size()&&
               s.planetForest.managedBodies[size_t(in.body)])continue;
            const render::Model* m = planetFlora_.proto(in.proto);
            if (!m) continue;
            const float3 pv = bodyToView(in.body, in.local);
            const float3 d = pv - eye;
            if (dot(d, d) > floraFar * floraFar || occluded(in.body, pv + pframe_.dirToView(in.up) * 0.05f)) continue;
            const float3 U = pframe_.dirToView(in.up);
            float3 E = cross(U, std::fabs(U.y) < 0.9f ? float3{0, 1, 0} : float3{1, 0, 0});
            E = normalize(E);
            const float3 S0 = cross(E, U);
            const float c = std::cos(in.yaw), sn = std::sin(in.yaw);
            const float3 X = E * c + S0 * sn, Z = S0 * c - E * sn;
            ModelDraw md;
            md.model = m;
            mat4 w;
            w.at(0, 0) = X.x * in.scale; w.at(1, 0) = X.y * in.scale; w.at(2, 0) = X.z * in.scale;
            w.at(0, 1) = U.x * in.scale; w.at(1, 1) = U.y * in.scale; w.at(2, 1) = U.z * in.scale;
            w.at(0, 2) = Z.x * in.scale; w.at(1, 2) = Z.y * in.scale; w.at(2, 2) = Z.z * in.scale;
            w.at(0, 3) = pv.x; w.at(1, 3) = pv.y; w.at(2, 3) = pv.z;
            md.world = w;
            if (planetNoShadow_) md.flags |= kDrawNoCastShadow;
            r.drawModel(md);
        }
    }
    if(blend<0.5f){
        for(const auto& plant:s.planetForest.plants){
            if(plant.body<0||size_t(plant.body)>=W.bodies.size())continue;
            const auto* model=planetFlora_.forestProto(plant.species,plant.proto);
            if(!model)continue;
            const auto center=bodyToView(plant.body,plant.local);
            const auto d=center-eye;
            if(dot(d,d)>260.f*260.f||occluded(plant.body,center))continue;
            const auto U=normalize(pframe_.dirToView(plant.up));
            const auto hint=pframe_.dirToView(plant.forward);
            auto Z=hint-U*dot(hint,U);
            if(dot(Z,Z)<1e-6f)continue;
            Z=normalize(Z);
            const auto X=normalize(cross(U,Z));
            const float scale=plant.size*W.bodies[size_t(plant.body)].floraScale;
            if(!(scale>0.f))continue;
            auto position=center;
            // ARBOR meshes have an authored root pivot, whereas a physical
            // checkpoint describes centre of mass. Approximate that offset
            // with the shared prototype's bind-space height.
            if(plant.physical)position=position-U*(plant.physicalRootOffset>=0.f?plant.physicalRootOffset:
                (model->boundsMax.y+model->boundsMin.y)*.5f*scale);
            ModelDraw md;md.model=model;
            mat4 matrix;
            const float transverse=scale*plant.thickness;
            if(!(transverse>0.f))continue;
            const float3 axes[]={X*transverse,U*scale,Z*transverse,position};
            for(int col=0;col<4;++col){
                matrix.at(0,col)=axes[col].x;matrix.at(1,col)=axes[col].y;matrix.at(2,col)=axes[col].z;
            }
            md.world=matrix;md.tint={plant.tint.x,plant.tint.y,plant.tint.z,1};
            if(planetNoShadow_)md.flags|=kDrawNoCastShadow;
            md.primitiveMaterialFilter=0; // shared bark material
            r.drawModel(md);
            // Shared foliage geometry; vigor tint and threshold, no per-tree
            // GPU mesh mutation or fabricated fractional leaf topology.
            if(plant.foliage>.05f){
                const float vigor=std::clamp(plant.foliage,0.f,1.f);
                md.primitiveMaterialFilter=1;
                md.tint={.75f+.25f*vigor,.45f+.55f*vigor,.55f+.45f*vigor,1};
                r.drawModel(md);
            }
        }
    }
    for (size_t i = 0; i < s.objects.size(); ++i) {
        const SceneObject& o = s.objects[i];
        const uint64_t key = o.entity.valid() ? o.entity.packed() : (1ull << 63) | uint64_t(i);
        int body = -1; float3 g, up, east, south; float lift = 0;
        mat4 place;
        if (o.body == SceneObject::kBodyTransit) { // between bodies: world position, nose along the travel direction
            const float3 U = pframe_.dirToView({0, 1, 0});
            float3 F = pframe_.dirToView(o.facing);
            F = F - U * dot(F, U);
            if (dot(F, F) < 1e-6f) F = pframe_.dirToView({1, 0, 0});
            const float3 E = normalize(F), S = cross(E, U), P = pframe_.toView(o.pos);
            place.at(0, 0) = E.x; place.at(1, 0) = E.y; place.at(2, 0) = E.z;
            place.at(0, 1) = U.x; place.at(1, 1) = U.y; place.at(2, 1) = U.z;
            place.at(0, 2) = S.x; place.at(1, 2) = S.y; place.at(2, 2) = S.z;
            place.at(0, 3) = P.x; place.at(1, 3) = P.y; place.at(2, 3) = P.z;
        } else {
            if (!placeObject(o, body, g, up, east, south, lift)) continue;
            place = frameAt(body, g + up * lift, up, east, south);
        }
        const float3 pv = place.transformPoint({0, 0, 0});
        if (o.isProjectile) {
            if (o.visible) {
                const bool custom=o.shooterType<looks_.size() && o.shotWeapon>=0 &&
                    size_t(o.shotWeapon)<looks_[o.shooterType].projectiles.size() &&
                    looks_[o.shooterType].projectiles[size_t(o.shotWeapon)].model;
                if(custom) {
                    SceneObject shot=o;
                    shot.pos=body>=0?bodyToView(body,o.pos):pv;
                    shot.facing=pframe_.dirToView(o.facing);
                    const float3 radial=body>=0?pframe_.dirToView(normalize(o.pos)):float3{0,1,0};
                    if(body<0||!occluded(body,shot.pos))
                        drawProjectile(r,key,shot,dt,radial,false);
                } else {
                    // Preserve the original planet-ball fallback transform.
                    ::aiverse::view::MeshObject packet;
                    packet.mesh=&projectile_;
                    packet.world=mat4::translate(place.transformPoint({0,0.3f,0}));
                    packet.color={1,0.95f,0.8f,1}; packet.flags=kDrawUnlit;
                    ::aiverse::View::submitMesh(r,packet);
                }
            }
            continue;
        }
        if (o.type >= looks_.size()) continue;
        // the object in its tangent frame (flat-map style vectors: x east, y up, z south), so the flat
        // animation code (speed, yaw, aim) works unchanged; the frame turns slowly with the position
        SceneObject lo = o;
        if (body >= 0) {
            lo.pos = {dot(g, east), dot(g, up), dot(g, south)};
            lo.facing = {dot(o.facing, east), 0, dot(o.facing, south)};
            const float3 t = o.target - o.pos;
            lo.target = lo.pos + float3{dot(t, east), dot(t, up), dot(t, south)};
            planetLast_[key] = {body, g, lift};
        } else { // transit: the frame above has east = travel direction
            lo.pos = o.pos; lo.facing = {1, 0, 0}; lo.hasTarget = false;
        }
        EntityAnim& st = stateFor(key, lo);
        st.last = lo;
        if (!o.visible) continue;
        const TypeLook& L = looks_[o.type];
        animate(st, lo, dt, nullptr, &pv);
        drawObject(r, st, lo, 0, 1.0f, nullptr, &place);
        if (o.selected || o.hovered) ring(place, selectionRadius(o.type), ringColor(o), 0.05f);
        if (lift > 0 && !planetNoShadow_) ring(frameAt(body, g, up, east, south), 0.35f, rgba(0, 0, 0, 110), 0.04f);   // orbit units: mark below
        const UnitType& t = rules_->types[o.type];
        float top = t.structure ? 1.6f * L.scale * std::max(1.0f, std::max(t.footprintW, t.footprintH) * 0.6f) : 1.2f * L.scale;
        if (L.air) top += L.flyHeight;
        bars_[i] = place.transformPoint({0, top, 0});
        barOk_[i] = (body < 0 || !occluded(body, bars_[i])) && blend < 0.6f;
    }
    drawTracers(r, dt);
    // corpses: last known placement, fading out
    for (auto it = anims_.begin(); it != anims_.end();) {
        EntityAnim& st = it->second;
        if (st.seen == frame_ && !st.dead) { ++it; continue; }
        if ((!st.dead || !st.initialized) && !fxCorpse(it->first, st)) {
            if (st.seen + 2 < frame_) { planetLast_.erase(it->first); it = anims_.erase(it); } else ++it;
            continue;
        }
        float adt = dt;
        st.deathAge = corpseAge(it->first, st.deathAge, dt, adt);
        if (st.type >= looks_.size() || st.deathAge > looks_[st.type].corpse) { planetLast_.erase(it->first); it = anims_.erase(it); continue; }
        const TypeLook& L = looks_[st.type];
        auto pl = planetLast_.find(it->first);
        if (st.seen != frame_ && st.last.visible && pl != planetLast_.end() && planets_.surface(pl->second.body)) {
            SceneObject o = st.last;
            o.selected = o.hovered = false;
            const float3 up = normalize(pl->second.ground);
            float3 east, south;
            tangentFrame(pl->second.body, up, east, south);
            const mat4 place = frameAt(pl->second.body, pl->second.ground + up * pl->second.lift, up, east, south);
            const float3 pv = place.transformPoint({0, 0, 0});
            animate(st, o, adt, nullptr, &pv);
            drawObject(r, st, o, 0, L.dissolveDeath ? 1.0f : L.corpseAlpha(st.deathAge), nullptr, &place);
        }
        ++it;
    }
    for (auto& mk : s.moveMarkers) { // sim-space points (client picking)
        int body; float3 l;
        if (!simToBody(mk, body, l) || !planets_.surface(body)) continue;
        const float3 up = normalize(l);
        float3 east, south;
        tangentFrame(body, up, east, south);
        ring(frameAt(body, planets_.surface(body)->surfacePoint(up), up, east, south), 0.35f, rgba(120, 255, 120, 200), 0.06f);
    }
}

// ------------------------------------------------------------------ scene from the simulation

void sceneFromSphere(Simulation& sim, GameCore& m, const Surface& S, GameScene& out, float alpha, uint8_t viewer) {
    World& w = sim.world();
    const Rules& R = m.rules();
    GameState& gs = m.state(sim);
    auto toF = [](const Vec3& v) { return float3{float(v.x.toDouble()), float(v.y.toDouble()), float(v.z.toDouble())}; };
    // body-local float position of a sim position; prev/cur on different bodies (landed): take cur
    auto local = [&](Vec3 prev, Vec3 cur, int& body) {
        body = S.bodyAt(cur);
        const float3 c = toF(cur - Surface::anchor(body));
        if (S.bodyAt(prev) != body) return c;
        return lerp(toF(prev - Surface::anchor(body)), c, alpha);
    };
    const std::vector<uint8_t> missingVision;
    const auto* playerVision=viewer<gs.vision.size()?&gs.vision[viewer]:&missingVision;
    auto visibleAt = [&](Vec3 p) -> uint8_t {
        const int c = S.cellAt(p);
        return c >= 0 && size_t(c) < playerVision->size() ? (*playerVision)[size_t(c)] : 0;
    };
    // Filter before the GameScene lease, not by blurred GPU fog or camera.
    // A malformed owner snapshot must not restore cosmetic trees in fog.
    if(!collectPlanetForestVisuals(w,S,out.planetForest,viewer?playerVision:nullptr)){
        out.planetForest={};
        if(viewer)out.planetForest.managedBodies.assign(S.bodies.size(),1);
        out.planetForestMotion.clear();
    }
    const uint32_t tick = sim.tick();
    out.planetForestMotion.interpolate(out.planetForest,S,&sim,sim.stateRestoreSerial(),tick,viewer,alpha,
                                      viewer?playerVision:nullptr);
    w.each<Unit, Owner, Transform>([&](Entity e, Unit& u, Owner& o, Transform& t) {
        SceneObject so;
        so.entity = e; so.type = u.type; so.owner = o.player;
        const UnitType& ut = R.types[u.type];
        if (auto* tx = w.tryGet<Transit>(e)) { // between bodies: world position (interpolated like worldPosition)
            const float span = float(std::max<uint32_t>(1, tx->arrive - tx->depart));
            const float k = std::clamp((float(tick > tx->depart ? tick - tx->depart : 0) + alpha) / span, 0.0f, 1.0f);
            const float3 a = toF(tx->fromWorld), b = toF(tx->toWorld);
            so.body = SceneObject::kBodyTransit;
            so.pos = lerp(a, b, k);
            so.facing = normalize(b - a);
            so.moving = true; so.action = kActMove;
            so.hp = ut.maxHp.raw > 0 ? float((u.hp / ut.maxHp).toDouble()) : 1.0f;
            so.visible = viewer == 0 || o.player == viewer || sim.allied(o.player, viewer);
            out.objects.push_back(so);
            return;
        }
        if ((u.flags & kUnitInHand) || !Surface::onSphere(t.pos)) return;
        if (ut.view.getBool("teamFromTerritory", false)) {
            const int cell = S.cellAt(t.pos);
            if (cell >= 0 && size_t(cell) < gs.grid.territory.size()) so.teamOwner = gs.grid.territory[size_t(cell)];
        }
        int body;
        so.pos = local(t.prevPos, t.pos, body);
        so.body = int16_t(body);
        so.facing = toF(u.facing);
        so.hp = ut.maxHp.raw > 0 ? float((u.hp / ut.maxHp).toDouble()) : 1.0f;
        if (auto* c = w.tryGet<Construction>(e)) so.build = float(c->progress.toDouble());
        if (auto* g = w.tryGet<Garrison>(e)) so.garrison = g->count;
        if (auto* c = w.tryGet<Cargo>(e)) { so.carry = float(c->amount.toDouble()); so.carryRes = c->res; }
        if (auto* od = w.tryGet<Orders>(e); od && !od->queue.empty()) so.order = uint8_t(od->queue.front().type) + 1;
        if (auto* mb = w.tryGet<Mobile>(e)) so.moving = mb->moving;
        if (auto* pr = w.tryGet<Production>(e)) so.producing = !pr->queue.empty();
        if (auto* cb = w.tryGet<Combat>(e); cb && cb->target.valid() && w.alive(cb->target)) {
            if (auto* tt = w.tryGet<Transform>(cb->target); tt && S.bodyAt(tt->pos) == body) {
                int tb;
                so.hasTarget = true; so.target = local(tt->prevPos, tt->pos, tb);
            }
        }
        if (so.moving) so.action = kActMove;
        else if (so.hasTarget) so.action = kActAttack;
        else if (so.order) {
            OrderType ot = OrderType(so.order - 1);
            if (ot == OrderType::Gather || ot == OrderType::Build || ot == OrderType::Repair) so.action = kActWork;
        }
        if (viewer != 0 && o.player != viewer && !sim.allied(o.player, viewer)) {
            const uint8_t v = visibleAt(t.pos);
            so.visible = ut.structure && o.player == 0 ? v >= 1 : v == 2;
        }
        out.objects.push_back(so);
    });
    w.each<Projectile, Transform>([&](Entity e, Projectile& pr, Transform& t) {
        if (!Surface::onSphere(t.pos)) return;
        SceneObject so;
        so.entity = e; so.isProjectile = true; so.source = pr.source;
        if(pr.sourceType<R.types.size())so.shooterType=pr.sourceType;
        else if(auto* su=w.tryGet<Unit>(pr.source))so.shooterType=su->type;
        so.shotWeapon=int8_t(view_adapter::projectileAppearanceSlot(R,pr));
        const Vec3 delta=t.pos-t.prevPos;
        so.facing=toF(delta.lengthSq().raw>0?delta:pr.aim-t.pos);
        int body;
        so.pos = local(t.prevPos, t.pos, body);
        so.body = int16_t(body);
        if (viewer != 0) so.visible = visibleAt(t.pos) == 2;
        out.objects.push_back(so);
    });
    for (size_t i = 0; i < gs.roads.nodes.size(); ++i)   // map roads between buildings (sim positions; GamePlanetGroundFx drapes them)
        for (uint16_t j : gs.roads.adj[i]) {
            if (j <= i) continue;
            auto* ta = w.tryGet<Transform>(gs.roads.nodes[i]);
            auto* tb = w.tryGet<Transform>(gs.roads.nodes[j]);
            if (ta && tb) out.roads.push_back({toF(ta->pos), toF(tb->pos)});
        }
}

} // namespace aiverse::game
