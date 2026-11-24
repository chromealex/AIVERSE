#include "VoxelGame.h"
#include "engine/core/FixedMath.h"
#include "engine/core/Archive.h"
#include <aiverselib/systems/voxel/VoxelData.h>
#include <aiverselib/systems/voxel/VoxelPersistence.h>
#include <aiverselib/systems/voxellight/VoxelLight.h>
#include <aiverselib/systems/voxelgen/VoxelGenData.h>
#include <aiverselib/systems/voxelticks/VoxelTicksData.h>
#include <aiverselib/systems/voxelticks/VoxelTicksPersistence.h>
#include <set>
#include <tuple>
namespace aiverse::voxelgame {
using namespace lib::systems;
namespace {
Fixed ratio(int64_t a,int64_t b){return Fixed::fromRatio(a,b);}
int64_t dist2(int cx,int cz,const Vec3& p){int64_t px=p.x.floorInt()>>4,pz=p.z.floorInt()>>4;return (cx-px)*(cx-px)+(cz-pz)*(cz-pz);}
BlockPos cellOf(const Vec3& p){return {int32_t(p.x.floorInt()),int32_t(p.y.floorInt()),int32_t(p.z.floorInt())};}
bool rayBox(Vec3 o,Vec3 d,const VoxelBox& b,Fixed& t){
    Fixed lo{},hi=1_fx;
    for(int a=0;a<3;++a){auto oa=voxelAxis(o,a),da=voxelAxis(d,a),mn=voxelAxis(b.min,a),mx=voxelAxis(b.max,a);
        if(da.raw==0){if(oa<mn||oa>mx)return false;continue;}
        auto t1=(mn-oa)/da,t2=(mx-oa)/da;if(t1>t2)std::swap(t1,t2);if(t1>lo)lo=t1;if(t2<hi)hi=t2;if(lo>hi)return false;}
    t=lo;return true;
}
bool overlaps(const VoxelBox& a,const VoxelBox& b){return a.min.x<b.max.x&&a.max.x>b.min.x&&a.min.y<b.max.y&&a.max.y>b.min.y&&a.min.z<b.max.z&&a.max.z>b.min.z;}
}
Command command(uint8_t player,const Input& in){
    Command c;c.player=player;c.type=kInputCommand;c.seq=in.sequence;
    c.args={in.forward,in.right,in.yaw.raw,in.pitch.raw,int64_t(in.buttons),int64_t(in.slot),int64_t(in.sequence)};return c;
}
bool decodeInput(const Command& c,Input& out){
    if(c.type!=kInputCommand||c.args.size()!=7)return false;const auto& a=c.args;
    if(a[0]< -1000||a[0]>1000||a[1]< -1000||a[1]>1000||a[4]<0||a[4]>31||a[5]<0||a[5]>kHotbar||a[6]<0||a[6]>INT32_MAX)return false;
    if(a[2]< -(int64_t(1)<<20)||a[2]>(int64_t(1)<<20)||a[3]< -(int64_t(1)<<16)||a[3]>(int64_t(1)<<16))return false;
    out.forward=int32_t(a[0]);out.right=int32_t(a[1]);out.yaw=Fixed::fromRaw(a[2]);out.pitch=Fixed::fromRaw(a[3]);
    out.buttons=uint32_t(a[4]);out.slot=uint8_t(a[5]);out.sequence=uint32_t(a[6]);return true;
}
Content readContent(const Json& rules){
    Content c;c.blocks=readBlockRegistry(rules);c.gen=readVoxelGen(rules,c.blocks);c.ticks=readVoxelTicks(rules,c.blocks);
    const auto& g=rules["voxelGame"];auto& cfg=c.config;
    auto num=[&](const char* key,int64_t def,int64_t lo,int64_t hi){auto v=g.getInt(key,def);if(v<lo||v>hi)throw JsonError(std::string("voxelGame.")+key+": "+std::to_string(lo)+".."+std::to_string(hi));return v;};
    cfg.tickRate=uint32_t(num("tickRate",20,10,60));cfg.snapshotRate=uint32_t(num("snapshotRate",cfg.tickRate,1,cfg.tickRate));cfg.maxPlayers=uint32_t(num("maxPlayers",8,1,64));
    cfg.simRadius=int(num("simRadius",4,2,12));cfg.keepRadius=int(num("keepRadius",cfg.simRadius+2,cfg.simRadius,16));cfg.viewRadius=int(num("viewRadius",6,2,16));
    cfg.columnsPerTick=uint32_t(num("columnsPerTick",2,1,16));cfg.creative=g.getBool("creative",false);cfg.dayTicks=uint32_t(num("dayTicks",12000,200,1<<22));
    cfg.reach=g.getFx("reach",5_fx);if(cfg.reach<1_fx||cfg.reach>16_fx)throw JsonError("voxelGame.reach: 1..16");
    cfg.maxMobs=uint32_t(num("maxMobs",10,0,256));cfg.spawnEvery=uint32_t(num("spawnEvery",60,1,1<<20));cfg.mobHealth=uint32_t(num("mobHealth",20,1,1000));
    cfg.mobDamage=uint32_t(num("mobDamage",3,0,100));cfg.playerHealth=uint32_t(num("playerHealth",20,1,1000));cfg.respawnTicks=uint32_t(num("respawnTicks",60,1,1<<20));
    cfg.mobSpeed=g.getFx("mobSpeed",ratio(26,10));cfg.mobPathNodes=uint32_t(num("mobPathNodes",600,16,1<<16));
    const auto& items=g["startItems"];
    for(size_t i=0;i<items.size()&&i<kHotbar;++i){auto name=items[i].getStr("block");auto id=c.blocks.find(name);if(!id)throw JsonError("voxelGame.startItems["+std::to_string(i)+"].block: unknown block '"+name+"'");
        cfg.startItems.push_back({*id,uint32_t(std::clamp<int64_t>(items[i].getInt("count",kStack),1,kStack))});}
    const auto& w=g["world"];c.bounds.minChunkY=int32_t(w.getInt("minChunkY",-4));c.bounds.maxChunkY=int32_t(w.getInt("maxChunkY",7));
    if(c.bounds.minChunkY>c.bounds.maxChunkY||c.bounds.maxChunkY-c.bounds.minChunkY>32)throw JsonError("voxelGame.world: minChunkY <= maxChunkY, at most 32 chunks high");
    return c;
}
VoxelBox playerBox(Vec3 p,const MotorConfig& m){return {{p.x-m.radius,p.y,p.z-m.radius},{p.x+m.radius,p.y+m.height,p.z+m.radius}};}
VoxelBox mobBox(Vec3 p){auto r=ratio(3,10);return {{p.x-r,p.y,p.z-r},{p.x+r,p.y+ratio(19,10),p.z+r}};}
Vec3 lookDirection(Fixed yaw,Fixed pitch){auto cp=fxCosTurns(pitch);return {fxSinTurns(yaw)*cp,fxSinTurns(pitch),fxCosTurns(yaw)*cp};}
uint8_t skyDarkening(uint64_t time,uint32_t dayTicks){
    // Day: first half (sunrise -> sunset), night: second half; smooth ramps of 1/12 day at dusk and dawn.
    int64_t t=int64_t(time%dayTicks),ramp=std::max<int64_t>(1,dayTicks/12),half=int64_t(dayTicks/2);
    if(t<half-ramp/2)return 0;if(t<half+ramp/2)return uint8_t((t-(half-ramp/2))*11/ramp);
    if(t<int64_t(dayTicks)-ramp/2)return 11;return uint8_t(11-(t-(int64_t(dayTicks)-ramp/2))*11/ramp);
}
float daylight(uint64_t time,uint32_t dayTicks){return 1.0f-0.8f*float(skyDarkening(time,dayTicks))/11.0f;}
void movePlayer(Player& p,const Input& in,const Config& c,const VoxelWorld& w,const BlockRegistry& r,Fixed dt){
    const auto& m=c.motor;
    p.yaw=in.yaw;p.pitch=std::clamp(in.pitch,-ratio(248,1000),ratio(248,1000));
    auto fluidAt=[&](Vec3 at){auto b=w.block(cellOf(at));return b&&r[*b].fluid;};
    p.inFluid=fluidAt(p.pos+Vec3{{},ratio(4,10),{}})||fluidAt(p.pos+Vec3{{},m.eye,{}});
    Fixed sy=fxSinTurns(p.yaw),cy=fxCosTurns(p.yaw);
    Vec3 wish{sy*Fixed::fromRatio(in.forward,1000)-cy*Fixed::fromRatio(in.right,1000),{},cy*Fixed::fromRatio(in.forward,1000)+sy*Fixed::fromRatio(in.right,1000)};
    if(wish.lengthSq()>1_fx)wish=fxNormalize(wish);
    Fixed speed=(in.buttons&Sprint)?m.sprintSpeed:m.speed;if(in.buttons&Sneak)speed=speed*ratio(3,10);if(p.inFluid)speed=speed*ratio(6,10);
    p.vel.x=wish.x*speed;p.vel.z=wish.z*speed;
    if(p.inFluid){p.vel.y-=m.gravity*ratio(1,4)*dt;if(p.vel.y< -2_fx)p.vel.y=-2_fx;if(in.buttons&Jump)p.vel.y=m.swimSpeed;}
    else{p.vel.y-=m.gravity*dt;if(p.vel.y< -50_fx)p.vel.y=-50_fx;if((in.buttons&Jump)&&p.grounded)p.vel.y=m.jumpSpeed;}
    Vec3 delta=p.vel*dt;bool wasGrounded=p.grounded;Fixed fallSpeed=-p.vel.y;
    auto box=playerBox(p.pos,m);auto res=voxelMoveBox(w,r,box,delta);
    // Step up one block when walking into a ledge (no manual jump needed on stairs and slopes).
    if((res.hit[0]||res.hit[2])&&wasGrounded&&!p.inFluid&&!(in.buttons&Sneak)){
        auto up=voxelMoveBox(w,r,box,{Fixed{},1_fx,Fixed{}});
        if(up.delta.y==1_fx){auto raised=playerBox(p.pos+Vec3{{},1_fx,{}},m);auto side=voxelMoveBox(w,r,raised,{delta.x,Fixed{},delta.z});
            auto gain=fxAbs(side.delta.x)+fxAbs(side.delta.z),before=fxAbs(res.delta.x)+fxAbs(res.delta.z);
            if(gain>before+ratio(1,100)){auto moved=p.pos+Vec3{side.delta.x,1_fx,side.delta.z};auto down=voxelMoveBox(w,r,playerBox(moved,m),{Fixed{},-1_fx,Fixed{}});
                res.delta={side.delta.x,1_fx+down.delta.y,side.delta.z};res.hit[1]=true;res.grounded=true;}}}
    // Sneaking does not walk off edges.
    if((in.buttons&Sneak)&&wasGrounded&&!p.inFluid){auto moved=playerBox(p.pos+Vec3{res.delta.x,Fixed{},res.delta.z},m);if(!voxelGrounded(w,r,moved,ratio(6,10))){res.delta.x={};res.delta.z={};}}
    p.pos+=res.delta;
    if(res.hit[1])p.vel.y={};
    p.grounded=res.grounded||(p.vel.y<=Fixed{}&&voxelGrounded(w,r,playerBox(p.pos,m)));
    p.fallFrom=(!wasGrounded&&p.grounded&&!p.inFluid)?fallSpeed:Fixed{}; // landing speed (fall damage)
}
Target target(const Player& p,const State& s,const Config& c,const BlockRegistry& r){
    Target out;auto eye=p.pos+Vec3{{},c.motor.eye,{}};auto ray=lookDirection(p.yaw,p.pitch)*c.reach;
    auto hit=voxelRaycast(s.world,r,eye,ray,[](BlockId id,const BlockDef& d){return id!=kAir&&!d.fluid;});
    Fixed best=2_fx;
    if(hit.hit){out.block=true;out.pos=hit.block;out.before=hit.before;out.face=hit.face;best=hit.t;}
    for(const auto& m:s.mobs){Fixed t;if(rayBox(eye,ray,mobBox(m.pos),t)&&t<best){best=t;out.mob=m.id;out.block=false;}}
    out.distance=best*c.reach;return out;
}
void VoxelHost::setup(Simulation& sim,const Json& rules,const Json& map){
    content_=readContent(rules);generator_=std::make_unique<VoxelGenerator>(content_.gen);
    State s{VoxelWorld(content_.bounds),{},{},{},1,0,{}};
    const auto& ps=map["players"];size_t n=std::clamp<size_t>(ps.size(),1,content_.config.maxPlayers);
    // Spawn: the surface at the map's spawn column.
    int32_t sx=int32_t(map["spawn"][0].integer(0)),sz=int32_t(map["spawn"][1].integer(0));
    for(int dx=-content_.config.simRadius;dx<=content_.config.simRadius;++dx)for(int dz=-content_.config.simRadius;dz<=content_.config.simRadius;++dz)
        if(dx*dx+dz*dz<=content_.config.simRadius*content_.config.simRadius)ensureColumn(s,(sx>>4)+dx,(sz>>4)+dz);
    int32_t top=content_.bounds.maxChunkY*16+15,y=top;
    while(y>content_.bounds.minChunkY*16&&!content_.blocks[s.world.blockOr({sx,y,sz},kAir)].solid)--y;
    s.spawn={Fixed::fromInt(sx)+ratio(1,2),Fixed::fromInt(y+1),Fixed::fromInt(sz)+ratio(1,2)};
    for(size_t i=0;i<n;++i){Player p;p.slot=uint8_t(i+1);p.pos=s.spawn;p.health=int32_t(content_.config.playerHealth);
        for(size_t k=0;k<content_.config.startItems.size();++k)p.hotbar[k]=content_.config.startItems[k];s.players.push_back(p);}
    sim.world().setResource<State>(std::move(s));
    sim.addSystem("voxel",[this](Simulation& sm){tick(sm);});
}
void VoxelHost::ensureColumn(State& s,int cx,int cz){
    auto& w=s.world;std::vector<ChunkPos> added;
    for(int cy=content_.bounds.maxChunkY;cy>=content_.bounds.minChunkY;--cy){ChunkPos c{cx,cy,cz};if(w.loaded(c))continue;
        auto chunk=w.parked(c)?w.unpark(c,content_.blocks):std::optional<VoxelChunk>(generator_->generate(c));
        if(!chunk)chunk=generator_->generate(c); // damaged parked bytes: fall back to generation
        w.insert(c,std::move(*chunk));added.push_back(c);}
    for(auto c:added)lightChunk(w,content_.blocks,c); // top-down: sky enters from above
}
void VoxelHost::stream(State& s){
    auto& w=s.world;const auto& cfg=content_.config;
    std::vector<Vec3> centers{s.spawn};for(const auto& p:s.players)if(p.active)centers.push_back(p.pos);
    std::vector<std::tuple<int64_t,int,int>> wanted;std::set<std::pair<int,int>> seen;
    for(const auto& c:centers){int ccx=int(c.x.floorInt()>>4),ccz=int(c.z.floorInt()>>4);
        for(int dx=-cfg.simRadius;dx<=cfg.simRadius;++dx)for(int dz=-cfg.simRadius;dz<=cfg.simRadius;++dz){if(dx*dx+dz*dz>cfg.simRadius*cfg.simRadius)continue;
            int cx=ccx+dx,cz=ccz+dz;if(!seen.insert({cx,cz}).second)continue;
            if(w.loaded({cx,content_.bounds.maxChunkY,cz})&&w.loaded({cx,content_.bounds.minChunkY,cz}))continue;
            int64_t best=INT64_MAX;for(const auto& cc:centers)best=std::min(best,dist2(cx,cz,cc));wanted.push_back({best,cx,cz});}}
    std::sort(wanted.begin(),wanted.end());
    for(size_t i=0;i<wanted.size()&&i<cfg.columnsPerTick;++i)ensureColumn(s,std::get<1>(wanted[i]),std::get<2>(wanted[i]));
    // Unload columns far from everyone (edited chunks are parked, the rest regenerates).
    std::set<std::pair<int,int>> far;
    for(const auto& [c,chunk]:w.chunks()){(void)chunk;int64_t best=INT64_MAX;for(const auto& cc:centers)best=std::min(best,dist2(c.x,c.z,cc));
        if(best>int64_t(cfg.keepRadius)*cfg.keepRadius)far.insert({c.x,c.z});}
    for(auto [cx,cz]:far)for(int cy=content_.bounds.minChunkY;cy<=content_.bounds.maxChunkY;++cy)w.unload({cx,cy,cz});
    if(!far.empty())s.mobs.erase(std::remove_if(s.mobs.begin(),s.mobs.end(),[&](const Mob& m){return !w.loaded(chunkOf(cellOf(m.pos)));}),s.mobs.end());
}
void VoxelHost::changed(State& s,BlockPos p,BlockId oldBlock){relightBlock(s.world,content_.blocks,p,oldBlock);notifyVoxelChange(s.ticks,s.world,content_.blocks,content_.ticks,p,s.time);}
bool VoxelHost::give(Player& p,BlockId block,uint32_t count)const{
    if(block==kAir||!count)return false;
    for(auto& st:p.hotbar)if(st.block==block&&st.count<kStack){auto add=std::min(count,kStack-st.count);st.count+=add;count-=add;if(!count)return true;}
    for(auto& st:p.hotbar)if(!st.count){st.block=block;st.count=std::min(count,kStack);count-=st.count;if(!count)return true;}
    return false; // hotbar full: the rest is lost (no dropped items yet)
}
void VoxelHost::applyCommand(Simulation& sim,const Command& c){
    Input in;if(!decodeInput(c,in))return;auto& s=state(sim);
    if(c.player<1||c.player>s.players.size())return;auto& p=s.players[c.player-1];
    if(in.sequence<=p.lastSequence&&p.active)return;
    p.input=in;p.lastSequence=in.sequence;p.active=true;
}
void VoxelHost::tickPlayer(Simulation& sim,State& s,Player& p){
    const auto& cfg=content_.config;const auto& r=content_.blocks;
    if(p.health<=0){if(s.time>=p.respawnAt){p.health=int32_t(cfg.playerHealth);p.pos=s.spawn;p.vel={};p.hasBreaking=false;}return;}
    auto& in=p.input;if(in.slot>=1&&in.slot<=kHotbar)p.selected=uint8_t(in.slot-1);
    if(!s.world.loaded(chunkOf(cellOf(p.pos))))return; // wait for the column under the player
    movePlayer(p,in,cfg,s.world,r,Fixed::fromRatio(1,cfg.tickRate));
    if(p.fallFrom>cfg.motor.fallSafe&&!cfg.creative){p.health-=int32_t(((p.fallFrom-cfg.motor.fallSafe)/ratio(3,1)).floorInt()+1);p.hurtAt=uint32_t(s.time);}
    uint32_t pressed=in.buttons&~p.lastButtons;p.lastButtons=in.buttons;
    auto t=target(p,s,cfg,r);
    if((in.buttons&Break)&&t.mob){
        if(s.time>=p.attackReadyAt){p.attackReadyAt=uint32_t(s.time)+10;p.hasBreaking=false;
            for(auto& m:s.mobs)if(m.id==t.mob){m.health-=5;m.hurtAt=uint32_t(s.time);auto push=lookDirection(p.yaw,Fixed{})*6_fx;m.vel={push.x,4_fx,push.z};}}
    } else if((in.buttons&Break)&&t.block){
        auto id=s.world.blockOr(t.pos,kAir);const auto& d=r[id];
        if(!d.unbreakable){
            if(!p.hasBreaking||!(p.breaking==t.pos)){p.breaking=t.pos;p.hasBreaking=true;p.breakProgress=0;}
            ++p.breakProgress;
            if(cfg.creative||p.breakProgress>=std::max<uint32_t>(1,d.breakTicks)){
                auto old=s.world.set(t.pos,kAir,0,r);
                if(old){changed(s,t.pos,*old);if(!cfg.creative)give(p,d.dropsSelf?id:d.drop,d.dropsSelf?1:d.dropCount);}
                p.hasBreaking=false;p.breakProgress=0;}
        }
    } else {p.hasBreaking=false;p.breakProgress=0;}
    if((in.buttons&Place)&&t.block&&t.face>=0&&((pressed&Place)||s.time>=p.placeReadyAt)){
        auto& st=p.hotbar[p.selected];
        if(st.count&&st.block){
            const auto& d=r[st.block];auto there=s.world.block(t.before);
            VoxelBox cell{{Fixed::fromInt(t.before.x),Fixed::fromInt(t.before.y),Fixed::fromInt(t.before.z)},{Fixed::fromInt(t.before.x+1),Fixed::fromInt(t.before.y+1),Fixed::fromInt(t.before.z+1)}};
            bool blocked=false;if(d.solid){for(const auto& o:s.players)if(o.health>0&&overlaps(cell,playerBox(o.pos,cfg.motor)))blocked=true;for(const auto& m:s.mobs)if(overlaps(cell,mobBox(m.pos)))blocked=true;}
            if(there&&r[*there].replaceable&&!blocked){
                auto old=s.world.set(t.before,st.block,0,r);
                if(old){changed(s,t.before,*old);if(!cfg.creative&&--st.count==0)st.block=kAir;p.placeReadyAt=uint32_t(s.time)+5;}}
        }
    }
    (void)sim;
}
void VoxelHost::tickMobs(Simulation& sim,State& s){
    const auto& cfg=content_.config;const auto& r=content_.blocks;auto dark=skyDarkening(s.time,cfg.dayTicks);
    lib::bots::VoxelAgent agent;agent.maxNodes=cfg.mobPathNodes;agent.height=2;
    std::vector<const Player*> alive;for(const auto& p:s.players)if(p.active&&p.health>0)alive.push_back(&p);
    if(!alive.empty()&&s.mobs.size()<cfg.maxMobs&&s.time%cfg.spawnEvery==0){
        const auto& who=*alive[sim.rng().below(uint32_t(alive.size()))];
        lib::bots::VoxelSpawnRule rule;rule.maxLight=7;rule.skyDarkening=dark;rule.radius=24;rule.minDistance=12;rule.verticalRange=12;rule.attempts=24;
        auto spots=lib::bots::voxelSpawnSpots(s.world,r,agent,rule,cellOf(who.pos),1,sim.rng());
        for(auto sp:spots){Mob m;m.id=s.nextMob++;m.pos=Vec3{Fixed::fromInt(sp.x)+ratio(1,2),Fixed::fromInt(sp.y),Fixed::fromInt(sp.z)+ratio(1,2)};m.health=int32_t(cfg.mobHealth);m.repathAt=uint32_t(s.time)+m.id%20;s.mobs.push_back(m);}
    }
    Config mobCfg=cfg;mobCfg.motor.speed=cfg.mobSpeed;mobCfg.motor.height=ratio(19,10);
    for(auto& m:s.mobs){
        // Daylight burns mobs standing under open sky.
        auto head=cellOf(m.pos+Vec3{{},ratio(17,10),{}});
        if(dark<4&&skyLight(s.world.light(head))>=12&&s.time%10==0)m.health-=1;
        const Player* tgt=nullptr;Fixed best=40_fx*40_fx;
        for(auto* p:alive){auto d=(p->pos-m.pos).lengthSq();if(d<best){best=d;tgt=p;}}
        Input in;
        if(tgt){
            m.target=tgt->slot;
            if(s.time>=m.repathAt){m.repathAt=uint32_t(s.time)+40;auto res=lib::bots::voxelPath(s.world,r,agent,cellOf(m.pos),cellOf(tgt->pos),1);
                m.path=res?res->path:std::vector<BlockPos>{};m.pathIndex=m.path.size()>1?1:0;}
            Vec3 goal=tgt->pos;
            if(m.pathIndex<m.path.size()){auto wp=m.path[m.pathIndex];goal={Fixed::fromInt(wp.x)+ratio(1,2),Fixed::fromInt(wp.y),Fixed::fromInt(wp.z)+ratio(1,2)};
                auto dxz=Vec3{goal.x-m.pos.x,{},goal.z-m.pos.z};if(dxz.lengthSq()<ratio(9,100))++m.pathIndex;}
            auto dir=Vec3{goal.x-m.pos.x,{},goal.z-m.pos.z};
            if(dir.lengthSq()>ratio(1,100)){in.yaw=fxAtan2Turns(dir.x,dir.z);in.forward=1000;m.yaw=in.yaw;}
            if(goal.y>m.pos.y+ratio(1,2))in.buttons|=Jump;
            auto gap=tgt->pos-m.pos;
            if(Vec3{gap.x,{},gap.z}.lengthSq()<ratio(169,100)&&fxAbs(gap.y)<ratio(15,10)&&s.time>=m.attackAt){
                m.attackAt=uint32_t(s.time)+cfg.mobAttackTicks;auto& victim=s.players[tgt->slot-1];
                if(!cfg.creative){victim.health-=int32_t(cfg.mobDamage);victim.hurtAt=uint32_t(s.time);if(victim.health<=0){victim.respawnAt=uint32_t(s.time)+cfg.respawnTicks;++victim.deaths;victim.vel={};}}}
        } else m.target=0;
        Player body;body.pos=m.pos;body.vel=m.vel;body.grounded=m.grounded;
        // Knockback decays; otherwise the motor drives the mob like a player.
        bool knocked=m.hurtAt+6>s.time;
        if(!s.world.loaded(chunkOf(cellOf(m.pos))))continue;
        movePlayer(body,in,mobCfg,s.world,r,Fixed::fromRatio(1,cfg.tickRate));
        if(knocked){auto kb=voxelMoveBox(s.world,r,mobBox(body.pos),Vec3{m.vel.x,Fixed{},m.vel.z}*Fixed::fromRatio(1,cfg.tickRate));body.pos+=kb.delta;m.vel=m.vel*ratio(8,10);}else m.vel={};
        m.pos=body.pos;m.grounded=body.grounded;if(!knocked)m.vel.y=body.vel.y;
    }
    s.mobs.erase(std::remove_if(s.mobs.begin(),s.mobs.end(),[&](const Mob& m){
        if(m.health<=0)return true;bool near=false;for(auto* p:alive)if((p->pos-m.pos).lengthSq()<96_fx*96_fx)near=true;return !near;}),s.mobs.end());
}
void VoxelHost::tick(Simulation& sim){
    auto& s=state(sim);++s.time;
    stream(s);
    for(auto& p:s.players)if(p.active)tickPlayer(sim,s,p);
    if(auto rep=stepVoxelTicks(content_.ticks,s.ticks,s.world,content_.blocks,s.time,sim.rng()))for(auto c:rep->changed)relightBlock(s.world,content_.blocks,c.pos,c.oldBlock);
    tickMobs(sim,s);
}
// Archive layout of the module's types (found by the vector/map io templates through ADL).
inline void ioPos(Archive& a,BlockPos& p){a(p.x,p.y,p.z);}
void io(Archive& a,ItemStack& st){a(st.block,st.count);}
void io(Archive& a,Input& in){a(in.forward,in.right,in.yaw,in.pitch,in.buttons,in.slot,in.sequence);}
void io(Archive& a,Player& p){
    a(p.slot,p.active,p.pos,p.vel,p.yaw,p.pitch,p.grounded,p.inFluid,p.fallFrom,p.health,p.respawnAt,p.deaths,p.hurtAt);
    for(auto& st:p.hotbar)io(a,st);
    a(p.selected);ioPos(a,p.breaking);a(p.hasBreaking,p.breakProgress,p.lastButtons,p.placeReadyAt,p.attackReadyAt,p.lastSequence);io(a,p.input);
}
void io(Archive& a,Mob& m){
    a(m.id,m.pos,m.vel,m.yaw,m.health,m.grounded,m.target);
    uint32_t n=a.count(m.path.size(),12);if(!a.writing())m.path.assign(n,{});for(uint32_t i=0;i<n&&a.ok();++i)ioPos(a,m.path[i]);
    a(m.pathIndex,m.repathAt,m.attackAt,m.hurtAt);
}
void VoxelHost::hashState(Simulation& sim,Hasher& h){
    auto& s=state(sim);h.u64(s.world.hash());h.u64(voxelTicksHash(s.ticks));h.u64(s.time);h.u32(s.nextMob);h.v3(s.spawn);
    std::string bytes;Archive a(&bytes);for(auto& p:s.players)io(a,p);for(auto& m:s.mobs)io(a,m);h.bytes(bytes.data(),bytes.size());
}
bool VoxelHost::serializeState(Simulation& sim,Archive& a){
    uint32_t version=1;uint64_t blocks=content_.blocks.defs.size(),seed=content_.gen.seed;a(version,blocks,seed);
    if(version!=1||blocks!=content_.blocks.defs.size()||seed!=content_.gen.seed)return false;
    auto& s=state(sim);
    if(a.writing()){
        game::NativeState ns;storeVoxelWorld(ns,"world",s.world);storeVoxelTicks(ns,"ticks",s.ticks);
        a(ns.arrays["world"],ns.arrays["ticks"]);io(a,s.players);io(a,s.mobs);a(s.nextMob,s.time,s.spawn);return a.ok();
    }
    game::NativeState ns;a(ns.arrays["world"],ns.arrays["ticks"]);
    State next{VoxelWorld(content_.bounds),{},{},{},1,0,{}};io(a,next.players);io(a,next.mobs);a(next.nextMob,next.time,next.spawn);
    if(!a.ok()||next.players.size()!=s.players.size())return false;
    for(size_t i=0;i<next.players.size();++i)if(next.players[i].slot!=i+1||next.players[i].selected>=kHotbar)return false;
    for(const auto& p:next.players)for(const auto& st:p.hotbar)if(!content_.blocks.has(st.block)||st.count>kStack)return false;
    auto world=loadVoxelWorld(ns,"world",content_.blocks);auto ticks=loadVoxelTicks(ns,"ticks");if(!world||!ticks)return false;
    next.world=std::move(world->world);next.ticks=std::move(*ticks);
    // Make the saved resident set resident again, column by column from the top (light), then compare by hash.
    std::map<std::pair<int,int>,std::vector<int>> columns;for(auto c:world->resident)columns[{c.x,c.z}].push_back(c.y);
    for(auto& [col,ys]:columns){std::sort(ys.rbegin(),ys.rend());std::vector<ChunkPos> added;
        for(int y:ys){ChunkPos c{col.first,y,col.second};auto chunk=next.world.parked(c)?next.world.unpark(c,content_.blocks):std::optional<VoxelChunk>(generator_->generate(c));if(!chunk)return false;next.world.insert(c,std::move(*chunk));added.push_back(c);}
        for(auto c:added)lightChunk(next.world,content_.blocks,c);}
    next.world.renderDirty.clear();next.world.edits.clear();
    s=std::move(next);return true;
}
}
