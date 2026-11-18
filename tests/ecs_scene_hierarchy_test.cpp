// Universal hierarchy regression (no graphics, no float simulation).
#include "engine/sim/SceneHierarchy.h"
#include <cstdlib>
#include <iostream>
// Always-on regression checks: Release CI defines NDEBUG, which removes
// standard assert() calls and can hide test side effects and fail -Werror.
#define CHECK(...) do { if (!(__VA_ARGS__)) { \
    std::cerr << "ECS regression failed at " << __FILE__ << ":" << __LINE__ \
              << ": " << #__VA_ARGS__ << "\n"; std::abort(); } } while(false)
using namespace aiverse;
static LocalTransform local(int x,int y=0,int z=0){
    LocalTransform t;t.position={Fixed::fromInt(x),Fixed::fromInt(y),Fixed::fromInt(z)};
    return t;
}
static int x(const World& w,Entity e){
    auto* t=w.tryGetConst<WorldTransform>(e);
    CHECK(t);return int(t->position.x.toInt());
}
static void nested() {
    World w;SceneHierarchy graph;
    Entity a=w.create(),b=w.create(),c=w.create(),root2=w.create();
    CHECK(graph.attach(w,a,local(10)));
    CHECK(graph.attach(w,b,local(2)));
    CHECK(graph.attach(w,c,local(3)));
    CHECK(graph.attach(w,root2,local(100)));
    CHECK(graph.setParent(w,b,a));
    CHECK(graph.setParent(w,c,b));
    CHECK(graph.update(w)==4);
    CHECK(x(w,a)==10 && x(w,b)==12 && x(w,c)==15);
    CHECK(graph.update(w)==0);
    CHECK(graph.lastVisited()==0); // no root scan on a clean frame
    // Deeply nested child dirty while ancestors remain clean.
    CHECK(graph.setLocal(w,c,local(5)));
    CHECK(graph.update(w)==1);
    CHECK(graph.lastVisited()==3); // only root -> parent -> leaf visited
    CHECK(x(w,c)==17);
    CHECK(graph.setLocal(w,a,local(20)));
    CHECK(graph.update(w)==3);
    CHECK(x(w,b)==22&&x(w,c)==27);
    CHECK(!graph.setParent(w,a,c)); // cycle
    CHECK(!graph.setParent(w,a,a)); // self
    Entity stale{999999,0};
    CHECK(!graph.setParent(w,c,stale));
    CHECK(graph.setParent(w,b,root2));
    CHECK(graph.update(w)==2);
    CHECK(x(w,b)==102 && x(w,c)==107);
    // Detach parent from graph before destroying its Entity.
    CHECK(graph.detach(w,b));
    w.destroy(b);
    CHECK(!graph.contains(b));
    CHECK(graph.update(w)==1);
    CHECK(x(w,c)==5);
    Entity reused=w.create();
    CHECK(reused.index==b.index&&reused.gen==b.gen+1);
    CHECK(graph.attach(w,reused,local(7)));
    CHECK(graph.update(w)==1);
    CHECK(!graph.setParent(w,reused,b)); // old generation forbidden
}
static void rotateScale() {
    World w;SceneHierarchy g;
    Entity a=w.create(),b=w.create();
    LocalTransform root=local(10);
    root.scale={2_fx,2_fx,2_fx};
    root.rotation={Fixed{},1_fx,Fixed{},Fixed{}}; // 180deg Y
    CHECK(g.attach(w,a,root));
    CHECK(g.attach(w,b,local(1)));
    CHECK(g.setParent(w,b,a));
    CHECK(g.update(w)==2);
    CHECK(x(w,b)==8);
}
static void nonuniformScaleShear() {
    World w;SceneHierarchy g;
    Entity parent=w.create(),child=w.create();
    LocalTransform p;
    p.scale={2_fx,1_fx,1_fx};
    LocalTransform l;
    // 45 degrees around Z: combining this rotation with parent X-scale
    // produces shear that cannot be represented by quaternion + scale alone.
    l.rotation.z=Fixed::fromRatio(382683,1000000);
    l.rotation.w=Fixed::fromRatio(923880,1000000);
    CHECK(g.attach(w,parent,p));
    CHECK(g.attach(w,child,l));
    CHECK(g.setParent(w,child,parent));
    CHECK(g.update(w)==2);
    const auto* world=w.tryGetConst<WorldTransform>(child);
    CHECK(world);
    auto result=hierarchy_detail::transformPoint(world->affine,Vec3{1_fx,Fixed{},Fixed{}});
    CHECK(fxAbs(result.x-Fixed::fromRatio(1414214,1000000))<Fixed::fromRatio(1,100));
    CHECK(fxAbs(result.y-Fixed::fromRatio(707107,1000000))<Fixed::fromRatio(1,100));
    // The transformed basis vectors are non-orthogonal: real affine shear.
    const auto dot=world->affine.at(0,0)*world->affine.at(0,1)
                  +world->affine.at(1,0)*world->affine.at(1,1);
    CHECK(fxAbs(dot)>Fixed::fromRatio(1,2));
}
static void rollback() {
    World w;SceneHierarchy graph;
    Entity a=w.create(),b=w.create();
    graph.attach(w,a,local(1));graph.attach(w,b,local(2));
    graph.setParent(w,b,a);graph.update(w);
    World snapshot=w;
    graph.setLocal(w,a,local(100));graph.update(w);
    CHECK(x(w,b)==102);
    w.copyFrom(snapshot);
    graph.rebuild(w);
    CHECK(graph.update(w)==2);
    CHECK(x(w,b)==3);
    CHECK(graph.update(w)==0);
    // Bad cycle injected by external ECS producer is repaired on rebuild.
    w.add<Parent>(a,Parent{b});
    graph.rebuild(w);
    CHECK(graph.update(w)==2);
    CHECK(!w.has<Parent>(b));
}
static void cowSnapshotHierarchy() {
    World w;
    SceneHierarchy graph;
    Entity parent=w.create(),child=w.create();
    CHECK(graph.attach(w,parent,local(10)));
    CHECK(graph.attach(w,child,local(3)));
    CHECK(graph.setParent(w,child,parent));
    graph.update(w);
    w.enableCowSnapshots(true);
    World snapshot=w;
    CHECK(graph.setLocal(w,parent,local(70)));
    CHECK(graph.update(w)==2);
    CHECK(x(w,child)==73);
    CHECK(static_cast<const World&>(snapshot).tryGetConst<WorldTransform>(child)
           ->position.x==13_fx);
    w.copyFrom(snapshot);
    graph.rebuild(w);
    CHECK(graph.update(w)==2);
    CHECK(x(w,child)==13);
    CHECK(graph.update(w)==0);
}
static void deep() {
    World w;SceneHierarchy graph;
    Entity first=kNullEntity,previous=kNullEntity;
    constexpr int n=3000;
    for(int i=0;i<n;++i) {
        Entity e=w.create();
        CHECK(graph.attach(w,e,local(1)));
        if(previous.valid())CHECK(graph.setParent(w,e,previous));
        if(!first.valid())first=e;
        previous=e;
    }
    CHECK(graph.update(w)==n);
    CHECK(x(w,previous)==n);
    CHECK(graph.update(w)==0);
    CHECK(graph.lastVisited()==0);
    graph.setLocal(w,previous,local(4));
    CHECK(graph.update(w)==1);
    CHECK(graph.lastVisited()==n); // ancestor path, one written leaf
    CHECK(x(w,previous)==n+3);
    graph.setLocal(w,first,local(2));
    CHECK(graph.update(w)==n);
    CHECK(x(w,previous)==n+4);
}
int main(){
    nested();rotateScale();nonuniformScaleShear();rollback();cowSnapshotHierarchy();deep();
    std::cout<<"PASS hierarchy parent links, deep updates, dirty branches, cycles, rollback and generations\n";
}
