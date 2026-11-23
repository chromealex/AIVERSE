#include "engine/world/TerraWeaveCavernsSpatialShadow.h"
#include "engine/world/TerraWeaveCavernsBox3DShadow.h"
#include <stdexcept>
#include <iostream>
using namespace aiverse;
using namespace aiverse::terraweave::caverns;

static void check(bool pass,const char* message) {
    if(!pass)throw std::runtime_error(message);
}
class FlatSource final:public nav::Source {
public:
    void spans(int32_t,int32_t,std::vector<nav::Span>& out)const override {
        out.push_back({Fixed::fromInt(0),0});
    }
    bool link(int32_t,int32_t,uint32_t,int32_t,int32_t,uint32_t)const override {
        return true;
    }
};
int main() {
    FlatSource flat;
    nav::Config cfg;
    cfg.width=4;cfg.depth=4;cfg.tile=2;cfg.cell=Fixed::fromInt(1);
    CaveNavShadow navOwner;
    check(navOwner.prepare(flat,cfg,10),"prepare nav");
    check(navOwner.active()==nullptr,"nav must be invisible during prepare");
    check(!navOwner.commit(9),"wrong nav revision");
    check(navOwner.commit(10),"commit nav");
    check(navOwner.active()&&navOwner.active()->built(),"nav resident");
    check(navOwner.prepare(flat,cfg,11),"prepare second nav");
    check(navOwner.abort(11),"abort staged nav");
    check(navOwner.activeRevision()==10,"abort retained original nav");

    CaveCollisionPrepared prepared;
    prepared.revision=10;prepared.cell={0,0,0};
    prepared.triangles={
        {{0_fx,0_fx,0_fx},{1_fx,0_fx,0_fx},{0_fx,0_fx,1_fx}}
    };
    prepared.body.motion=physics::Motion::Static;
    prepared.body.shape=physics::ShapeKind::Mesh;
    prepared.body.vertices={{0,0,0},{1,0,0},{0,0,1}};
    prepared.body.indices={0,1,2};
    world::SpatialWorld collision;
    CaveSpatialShadow spatial;
    check(spatial.prepare(prepared,10000,collision),"prepare spatial");
    check(collision.colliders().empty(),"spatial prepare must not mutate world");
    check(!spatial.commit(collision,9),"wrong spatial revision");
    check(spatial.commit(collision,10),"spatial commit");
    check(spatial.activeRevision()==10 && spatial.activeCount()==1,"spatial resident");
    check(collision.colliders().size()==1,"expected one collider");
    prepared.revision=11;
    check(spatial.prepare(prepared,11000,collision),"new spatial candidate");
    check(spatial.abort(11),"abort spatial");
    check(collision.colliders().size()==1,"aborted edit kept previous collision");

    CaveBox3DShadow boxOwner;
    check(boxOwner.prepare(prepared),"Box3D descriptor staging");
    check(!boxOwner.abort(10),"stale Box3D abort forbidden");
    check(boxOwner.abort(11),"Box3D abort");

    std::cout<<"caverns stage26 shadow owners PASS\n";
}
