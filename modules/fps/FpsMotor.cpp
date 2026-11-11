#include "FpsModule.h"
#include "engine/core/FixedMath.h"

namespace aiverse::fps {
Vec3 direction(Fixed yaw,Fixed pitch) {
    auto cp=fxCosTurns(pitch);return {fxSinTurns(yaw)*cp,fxSinTurns(pitch),fxCosTurns(yaw)*cp};
}
Fixed stanceHeight(const Actor& a,const MotorConfig& c){return (a.prone||a.downed)?Fixed::fromRatio(6,10):a.crouched?c.crouchHeight:c.height;}
world::Bounds actorBounds(const Actor& a,const MotorConfig& c) {
    auto height=stanceHeight(a,c);
    return {a.pos-Vec3{c.radius,{},c.radius},a.pos+Vec3{c.radius,height,c.radius}};
}
namespace {
constexpr auto skin=Fixed::fromRatio(1,1000);
Vec3 slide(Vec3 center,Vec3 delta,Vec3 half,const world::SpatialWorld& world,bool& grounded) {
    for(int i=0;i<4&&delta.lengthSq().raw>0;++i) {
        auto hit=world.cast(center,delta,half);
        if(!hit.hit){center+=delta;break;}
        auto length=delta.length();
        auto fraction=fxMax(Fixed{},hit.fraction-(length.raw?skin/length:Fixed{}));
        center+=delta*fraction;
        delta=delta*(1_fx-fraction);
        if(hit.normal.y>Fixed{})grounded=true;
        auto into=delta.dot(hit.normal);
        if(into>=Fixed{})break; // initial penetration is not a valid spawn
        delta=delta-hit.normal*into;
    }
    return center;
}
}
void moveActor(Actor& a,const Input& in,const MotorConfig& c,const world::SpatialWorld& w,Fixed dt,Fixed limit) {
    if(a.health<=0||a.vehicle||a.frozen)return;
    a.yaw=in.yaw;a.pitch=in.pitch;
    bool crouch=(in.buttons&Crouch)!=0;
    bool prone=(in.buttons&Prone)!=0||a.downed;
    if(a.prone&&!prone){Actor standing=a;standing.prone=false;auto b=actorBounds(standing,c);b.min.y+=skin;if(!w.query(b).empty())prone=true;}
    a.prone=prone;
    if(a.crouched&&!crouch) {
        Actor standing=a;standing.crouched=false;
        auto b=actorBounds(standing,c);b.min.y+=skin;
        if(!w.query(b).empty())crouch=true;
    }
    a.crouched=crouch;
    auto height=stanceHeight(a,c);
    Vec3 half{c.radius,height/2,c.radius},center=a.pos+Vec3{{},height/2,{}};
    auto forward=direction(in.yaw,{}),right=direction(in.yaw-Fixed::fromRatio(1,4),{});
    Vec3 wish=forward*Fixed::fromRatio(in.forward,1000)+right*Fixed::fromRatio(in.right,1000);
    auto length=wish.length();if(length>1_fx)wish=wish/length;
    auto speed=(a.prone||a.downed)?c.speed/4:crouch?c.speed/2:(in.buttons&Sprint)?c.sprintSpeed:c.speed;
    if(in.buttons&Aim)speed=speed/2;
    if(a.climbing){bool ground=false;auto end=slide(center,{{},Fixed::fromRatio(in.forward,1000)*c.speed*dt,{}},half,w,ground);a.pos=end-Vec3{{},height/2,{}};a.velocity={};return;}
    auto horizontal=wish*speed*dt;
    if((in.buttons&Jump)&&a.grounded&&!crouch&&!a.prone)a.velocity.y=c.jumpSpeed;
    a.velocity.y-=c.gravity*dt;
    bool ground=false;
    auto before=center;
    center=slide(center,{{},a.velocity.y*dt,{}},half,w,ground);
    if(center.y!=before.y+a.velocity.y*dt)a.velocity.y={};
    auto base=center;
    center=slide(center,horizontal,half,w,ground);
    // Step up only while supported, with ceiling and landing clearance.
    if((a.grounded||ground)&&c.stepHeight>Fixed{}&&(center-base-horizontal).lengthSq()>skin*skin) {
        Vec3 up{{},c.stepHeight,{}};
        if(!w.cast(base,up,half).hit) {
            bool steppedGround=false;
            auto stepped=slide(base+up,horizontal,half,w,steppedGround);
            auto down=w.cast(stepped,{{},-c.stepHeight-skin*2,{}},half);
            if(down.hit&&down.normal.y>Fixed{} && (stepped-base).xz().lengthSq()>(center-base).xz().lengthSq()) {
                stepped.y-=(c.stepHeight+skin*2)*down.fraction-skin;
                center=stepped;ground=true;
            }
        }
    }
    a.pos=center-Vec3{{},height/2,{}};
    a.pos.x=fxClamp(a.pos.x,-limit,limit);a.pos.z=fxClamp(a.pos.z,-limit,limit);
    a.pos.y=fxClamp(a.pos.y,-limit,limit);
    a.velocity.x=wish.x*speed;a.velocity.z=wish.z*speed;a.grounded=ground;
}
Command command(uint8_t slot,const Input& i) {
    Command c;c.type=kInputCommand;c.player=slot;c.seq=i.sequence;
    c.args={i.forward,i.right,i.yaw.raw,i.pitch.raw,int64_t(i.buttons),int64_t(i.viewTick),int64_t(i.action),int64_t(i.argument)};return c;
}
bool decodeInput(const Command& c,Input& out) {
    if(c.type!=kInputCommand||c.args.size()!=8||!c.units.empty()||c.target.valid())return false;
    const auto& a=c.args;
    if(a[0]<-1000||a[0]>1000||a[1]<-1000||a[1]>1000||a[2]<0||a[2]>=Fixed::kOne ||
       a[3]<-Fixed::kOne/4+64||a[3]>Fixed::kOne/4-64||a[4]<0||(a[4]&~int64_t(1023))!=0||((a[4]&224)>>5)>4||a[5]<0||a[5]>UINT32_MAX||a[6]<0||a[6]>int64_t(Action::Vault)||a[7]<0||a[7]>UINT32_MAX)return false;
    out={int32_t(a[0]),int32_t(a[1]),Fixed::fromRaw(a[2]),Fixed::fromRaw(a[3]),uint32_t(a[4]),uint32_t(a[5]),c.seq,Action(a[6]),uint32_t(a[7])};return true;
}
void io(Archive& a,Input& i){a(i.forward,i.right,i.yaw,i.pitch,i.buttons,i.viewTick,i.sequence,i.action,i.argument);}
void io(Archive& a,Actor& p) {
    a(p.slot,p.team,p.pos,p.velocity,p.yaw,p.pitch,p.health,p.ammo,p.reserve,p.kills,p.reloadUntil,p.nextFire,p.respawnAt,
      p.lastInputTick,p.lastSequence,p.life,p.weaponIndex,p.storedAmmo,p.storedReserve,p.shots,p.hits,p.switchUntil,p.fireHeld,p.grounded,p.crouched,p.input,p.armor,p.money,p.owned,p.attachments,p.recoil,p.aiming,p.prone,p.downed,p.climbing,p.frozen,p.bleedUntil,p.blindUntil,p.vehicle,p.lastDamage,p.activeAction,p.actionUntil,p.actionTarget,p.actionOrigin,p.bag);
}
}
