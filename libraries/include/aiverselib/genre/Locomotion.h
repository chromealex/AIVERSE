#pragma once
#include "engine/core/FixedMath.h"
#include <optional>
namespace aiverse::lib::genre {
struct MotionConfig {
    Fixed groundSpeed=6_fx,airSpeed=6_fx,acceleration=30_fx,braking=40_fx,gravity=20_fx,jumpSpeed=7_fx;
    uint32_t coyoteTicks=6,bufferTicks=6,maxJumps=1;
};
struct MotionState {
    Vec3 velocity{};uint32_t lastTick=0,lastGround=0,jumpTick=0,jumps=0;
    bool initialized=false,hasGround=false,buffered=false;
};
struct MotionInput {Vec2 wish{};bool grounded=false,jumpPressed=false,ceiling=false;};
struct MotionResult {Vec3 velocity{};bool jumped=false;};
inline bool valid(const MotionConfig& c){return c.groundSpeed>=Fixed{}&&c.groundSpeed<=100_fx&&c.airSpeed>=Fixed{}&&c.airSpeed<=100_fx&&c.acceleration>Fixed{}&&c.acceleration<=1000_fx&&c.braking>Fixed{}&&c.braking<=1000_fx&&c.gravity>=Fixed{}&&c.gravity<=1000_fx&&c.jumpSpeed>=Fixed{}&&c.jumpSpeed<=100_fx&&c.maxJumps>=1&&c.maxJumps<=16&&c.coyoteTicks<=3600&&c.bufferTicks<=3600;}
inline std::optional<MotionResult> stepMotion(const MotionConfig& c,MotionState& state,MotionInput in,uint32_t tick,Fixed dt){
    for(auto value:{state.velocity.x,state.velocity.y,state.velocity.z})if(value< -1000_fx||value>1000_fx)return {};
    if(state.jumps>16||(state.hasGround&&state.lastGround>tick)||(state.buffered&&state.jumpTick>tick))return {};
    if(!valid(c)||dt<=Fixed{}||dt>1_fx||(state.initialized&&tick<=state.lastTick)||in.wish.x< -1_fx||in.wish.x>1_fx||in.wish.y< -1_fx||in.wish.y>1_fx)return {};
    auto s=state;s.initialized=true;s.lastTick=tick;
    if(in.grounded){s.lastGround=tick;s.hasGround=true;s.jumps=0;if(s.velocity.y<Fixed{})s.velocity.y={};}
    if(in.ceiling&&s.velocity.y>Fixed{})s.velocity.y={};
    if(in.jumpPressed){s.buffered=true;s.jumpTick=tick;}
    if(s.buffered&&tick-s.jumpTick>c.bufferTicks)s.buffered=false;
    const bool coyote=s.hasGround&&tick-s.lastGround<=c.coyoteTicks&&s.jumps==0;
    if(!in.grounded&&!coyote&&s.jumps==0)s.jumps=1;
    bool jump=s.buffered&&!in.ceiling&&(in.grounded||coyote||s.jumps<c.maxJumps);
    if(jump){s.velocity.y=c.jumpSpeed;++s.jumps;s.buffered=false;}
    auto wish=Vec3{in.wish.x,{},in.wish.y};if(wish.lengthSq()>1_fx)wish=fxNormalize(wish);
    auto desired=wish*(in.grounded?c.groundSpeed:c.airSpeed);auto diff=Vec3{desired.x-s.velocity.x,{},desired.z-s.velocity.z};
    auto length=diff.length(),amount=(wish.lengthSq().raw?c.acceleration:c.braking)*dt;
    if(length>amount)diff=fxNormalize(diff)*amount;
    s.velocity.x+=diff.x;
    s.velocity.z+=diff.z;
    if(!in.grounded||jump)s.velocity.y=fxMax(s.velocity.y-c.gravity*dt,-1000_fx);
    state=s;return MotionResult{s.velocity,jump};
}
// The existing character/physics owner performs collision and reports grounded/ceiling next tick.
// This policy never writes positions or bypasses the FPS motor/authoritative command path.
}
