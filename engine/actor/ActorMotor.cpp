#include "ActorModule.h"
#include "engine/core/FixedMath.h"

namespace aiverse::actor {
Vec3 direction(Fixed yaw,Fixed pitch) {
    auto cp=fxCosTurns(pitch);return {fxSinTurns(yaw)*cp,fxSinTurns(pitch),fxCosTurns(yaw)*cp};
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
void moveBody(MotorBody& a,const Input& in,const MotorConfig& c,const world::SpatialWorld& w,Fixed dt,Fixed limit) {
    if(a.health<=0||a.vehicle||a.frozen)return;
    a.yaw=in.yaw;a.pitch=in.pitch;
    bool crouch=(in.buttons&Crouch)!=0;
    bool prone=(in.buttons&Prone)!=0||a.downed;
    if(a.prone&&!prone){auto b=bodyBounds(a.pos,stanceHeight(false,a.downed,a.crouched,c),c);b.min.y+=skin;if(w.blocked(b))prone=true;}
    a.prone=prone;
    if(a.crouched&&!crouch) {
        auto b=bodyBounds(a.pos,stanceHeight(a.prone,a.downed,false,c),c);b.min.y+=skin;
        if(w.blocked(b))crouch=true;
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
namespace {
// slide() with the support (normal.y > 0) and ceiling (normal.y < 0) normals the sweep touched.
Vec3 slideReport(Vec3 center,Vec3 delta,Vec3 half,const world::SpatialWorld& world,bool& grounded,Vec3& groundNormal,bool& ceiling) {
    for(int i=0;i<4&&delta.lengthSq().raw>0;++i) {
        auto hit=world.cast(center,delta,half);
        if(!hit.hit){center+=delta;break;}
        auto length=delta.length();
        auto fraction=fxMax(Fixed{},hit.fraction-(length.raw?skin/length:Fixed{}));
        center+=delta*fraction;
        delta=delta*(1_fx-fraction);
        if(hit.normal.y>Fixed{}){grounded=true;groundNormal=hit.normal;}
        if(hit.normal.y<Fixed{})ceiling=true;
        auto into=delta.dot(hit.normal);
        if(into>=Fixed{})break;
        delta=delta-hit.normal*into;
    }
    return center;
}
}
void io(Archive& a,LocomotionBody& l){lib::mechanics::io(a,l.state);a(l.anchor,l.groundNormal,l.buttons,l.life,l.anchored,l.ceiling);}
bool valid(const LocomotionBody& l){
    auto inside=[](Vec3 v,Fixed m){return fxAbs(v.x)<=m&&fxAbs(v.y)<=m&&fxAbs(v.z)<=m;};
    return lib::mechanics::valid(l.state)&&inside(l.anchor,100000_fx)&&inside(l.groundNormal,2_fx)&&(l.buttons&~kButtonBits)==0&&
        l.anchored==(l.state.mode==lib::mechanics::LocomotionMode::Grapple);
}
void hashLocomotionBody(Hasher& h,const LocomotionBody& l){
    lib::mechanics::hashLocomotion(h,l.state);h.v3(l.anchor);h.v3(l.groundNormal);h.u32(l.buttons);h.u32(l.life);h.u32(uint32_t(l.anchored)|uint32_t(l.ceiling)<<1);
}
void moveBodyModes(MotorBody& a,LocomotionBody& l,const Input& in,const MotorConfig& c,const lib::mechanics::LocomotionModeConfig& modes,const world::SpatialWorld& w,Fixed dt,Fixed limit) {
    using namespace lib::mechanics;
    if(a.health<=0||a.vehicle||a.frozen)return;
    if(l.life!=a.life){l=LocomotionBody{};l.life=a.life;}   // a new life starts a new locomotion state
    a.yaw=in.yaw;a.pitch=in.pitch;
    // Stance: the same rules as moveBody.
    bool crouch=(in.buttons&Crouch)!=0;
    bool prone=(in.buttons&Prone)!=0||a.downed;
    if(a.prone&&!prone){auto b=bodyBounds(a.pos,stanceHeight(false,a.downed,a.crouched,c),c);b.min.y+=skin;if(w.blocked(b))prone=true;}
    a.prone=prone;
    if(a.crouched&&!crouch){auto b=bodyBounds(a.pos,stanceHeight(a.prone,a.downed,false,c),c);b.min.y+=skin;if(w.blocked(b))crouch=true;}
    a.crouched=crouch;
    auto height=stanceHeight(a,c);
    Vec3 half{c.radius,height/2,c.radius},center=a.pos+Vec3{{},height/2,{}};
    auto forward=direction(in.yaw,{}),right=direction(in.yaw-Fixed::fromRatio(1,4),{});
    Vec3 wish=forward*Fixed::fromRatio(in.forward,1000)+right*Fixed::fromRatio(in.right,1000);
    auto length=wish.length();if(length>1_fx)wish=wish/length;
    auto speed=(a.prone||a.downed)?c.speed/4:crouch?c.speed/2:(in.buttons&Sprint)?c.sprintSpeed:c.speed;
    if(in.buttons&Aim)speed=speed/2;
    const uint32_t buttons=in.buttons,previous=l.buttons;l.buttons=buttons;
    if(a.climbing){bool ground=false;auto end=slide(center,{{},Fixed::fromRatio(in.forward,1000)*c.speed*dt,{}},half,w,ground);a.pos=end-Vec3{{},height/2,{}};a.velocity={};return;}
    // The motor's speed, jump and gravity drive the policy's ground/air core; the rest comes from rules `locomotion`.
    auto config=modes;config.motion.groundSpeed=config.motion.airSpeed=fxMin(speed,100_fx);config.motion.jumpSpeed=c.jumpSpeed;config.motion.gravity=c.gravity;
    LocomotionInput li;li.wish={wish.x,wish.z};li.facing={forward.x,forward.z};
    li.jumpPressed=(buttons&Jump)&&!crouch&&!a.prone;
    li.dashPressed=(buttons&Dash)&&!(previous&Dash);
    li.jumpHeld=(buttons&Glide)!=0;
    auto& e=li.env;e.grounded=a.grounded;e.groundNormal=a.grounded?l.groundNormal:Vec3{Fixed{},1_fx,Fixed{}};e.ceiling=l.ceiling;
    // Side walls: short probes along the wish and both sides of the horizontal velocity (wall slide / wall run).
    if(!a.grounded){
        const auto v=l.state.motion.velocity;Vec3 along{v.x,{},v.z};
        Vec3 probes[3]={Vec3{wish.x,{},wish.z},{},{}};
        if(along.lengthSq()>Fixed::fromRatio(1,100)){along=fxNormalize(along);probes[1]={along.z,{},Fixed{}-along.x};probes[2]={Fixed{}-along.z,{},along.x};}
        for(const auto& d:probes){
            if(d.lengthSq()<Fixed::fromRatio(1,100))continue;
            auto hit=w.cast(center,fxNormalize(d)*(c.radius/2),half);
            if(hit.hit&&fxAbs(hit.normal.y)<Fixed::fromRatio(1,2)){e.wallContact=true;e.wallId=int64_t(hit.id);e.contactNormal=fxNormalize(Vec3{hit.normal.x,{},hit.normal.z});break;}
        }
    }
    // Grapple: a press casts the view ray; while attached the stored point is reported every tick.
    if(l.anchored&&l.state.mode==LocomotionMode::Grapple){
        e.grappleAnchor=true;e.grappleOffset=l.anchor-center;li.grappleReleased=!(buttons&Grapple);
        if(e.grappleOffset==Vec3{})e.grappleOffset={Fixed{},Fixed::fromRaw(1),Fixed{}};
    }else if((buttons&Grapple)&&!(previous&Grapple)&&modes.grappleEnabled&&modes.grappleMaxLength>Fixed{}){
        const auto eye=a.pos+Vec3{{},height*Fixed::fromRatio(9,10),{}},ray=direction(in.yaw,in.pitch)*modes.grappleMaxLength;
        auto hit=w.cast(eye,ray,{});
        if(hit.hit){
            const auto point=eye+ray*hit.fraction;
            if(point!=center){li.grapplePressed=true;e.grappleAnchor=true;e.grappleOffset=point-center;l.anchor=point;}
        }
    }
    const uint32_t tick=l.state.initialized?l.state.lastTick+1:1;
    auto result=stepLocomotion(config,l.state,li,tick,dt);
    if(!result){a.velocity={};return;}   // rejected report (out-of-range geometry): stand still, state untouched
    l.anchored=result->mode==LocomotionMode::Grapple;
    if(!l.anchored)l.anchor={};
    // Sweep: vertical, then horizontal with the motor's step-up (same order as moveBody).
    auto v=result->velocity;bool ground=false,ceiling=false;Vec3 normal{Fixed{},1_fx,Fixed{}};
    auto before=center;
    center=slideReport(center,{{},v.y*dt,{}},half,w,ground,normal,ceiling);
    if(center.y!=before.y+v.y*dt)v.y={};
    auto base=center;auto horizontal=Vec3{v.x,{},v.z}*dt;
    center=slideReport(center,horizontal,half,w,ground,normal,ceiling);
    if((a.grounded||ground)&&c.stepHeight>Fixed{}&&(center-base-horizontal).lengthSq()>skin*skin) {
        Vec3 up{{},c.stepHeight,{}};
        if(!w.cast(base,up,half).hit) {
            bool steppedGround=false;
            auto stepped=slide(base+up,horizontal,half,w,steppedGround);
            auto down=w.cast(stepped,{{},-c.stepHeight-skin*2,{}},half);
            if(down.hit&&down.normal.y>Fixed{} && (stepped-base).xz().lengthSq()>(center-base).xz().lengthSq()) {
                stepped.y-=(c.stepHeight+skin*2)*down.fraction-skin;
                center=stepped;ground=true;normal=down.normal;
            }
        }
    }
    // The policy does not pull a grounded body down, so support is probed: a short downward cast keeps contact
    // (and snaps onto it) while the body is not rising.
    if(!ground&&v.y<=Fixed{}){
        const Fixed probe=Fixed::fromRatio(5,100);auto down=w.cast(center,{{},Fixed{}-probe,{}},half);
        if(down.hit&&down.normal.y>Fixed{}){center.y-=fxMax(Fixed{},probe*down.fraction-skin);ground=true;normal=down.normal;}
    }
    a.pos=center-Vec3{{},height/2,{}};
    a.pos.x=fxClamp(a.pos.x,-limit,limit);a.pos.z=fxClamp(a.pos.z,-limit,limit);a.pos.y=fxClamp(a.pos.y,-limit,limit);
    a.velocity=v;a.grounded=ground;l.groundNormal=ground?normal:Vec3{Fixed{},1_fx,Fixed{}};l.ceiling=ceiling&&v.y==Fixed{};
}
Command command(uint8_t slot,const Input& i) {
    Command c;c.type=kInputCommand;c.player=slot;c.seq=i.sequence;
    c.args={i.forward,i.right,i.yaw.raw,i.pitch.raw,int64_t(i.buttons),int64_t(i.viewTick),int64_t(i.action),int64_t(i.argument)};return c;
}
bool decodeInput(const Command& c,Input& out) {
    if(c.type!=kInputCommand||c.args.size()!=8||!c.units.empty()||c.target.valid())return false;
    const auto& a=c.args;
    if(a[0]<-1000||a[0]>1000||a[1]<-1000||a[1]>1000||a[2]<0||a[2]>=Fixed::kOne ||
       a[3]<-Fixed::kOne/4+64||a[3]>Fixed::kOne/4-64||a[4]<0||(a[4]&~int64_t(kButtonBits))!=0||((a[4]&224)>>5)>4||a[5]<0||a[5]>UINT32_MAX||a[6]<0||a[6]>int64_t(Action::Vault)||a[7]<0||a[7]>UINT32_MAX)return false;
    out={int32_t(a[0]),int32_t(a[1]),Fixed::fromRaw(a[2]),Fixed::fromRaw(a[3]),uint32_t(a[4]),uint32_t(a[5]),c.seq,Action(a[6]),uint32_t(a[7])};return true;
}
void io(Archive& a,Input& i){a(i.forward,i.right,i.yaw,i.pitch,i.buttons,i.viewTick,i.sequence,i.action,i.argument);}
void io(Archive& a,CharacterSnapshot& p) {
    a(p.slot,p.team,p.pos,p.velocity,p.yaw,p.pitch,p.health,p.ammo,p.reserve,p.kills,p.reloadUntil,p.nextFire,p.respawnAt,
      p.lastInputTick,p.lastSequence,p.life,p.weaponIndex,p.storedAmmo,p.storedReserve,p.shots,p.hits,p.switchUntil,p.fireHeld,p.grounded,p.crouched,p.input,p.armor,p.money,p.owned,p.attachments,p.recoil,p.aiming,p.prone,p.downed,p.climbing,p.frozen,p.bleedUntil,p.blindUntil,p.vehicle,p.lastDamage,p.activeAction,p.actionUntil,p.actionTarget,p.actionOrigin,p.bag);
}
void ioMelee(Archive& a,CharacterSnapshot& p) {
    a(p.melee.attack,p.melee.queued,p.melee.started,p.melee.queuedAt,p.melee.lastTick,p.melee.initialized,p.melee.hitTargets,
      p.parryUntil,p.staggerUntil,p.lockTarget,p.blockHeld,p.meleeHasTip,p.meleeTip);
}
}
