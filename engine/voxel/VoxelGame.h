#pragma once
// Voxel sandbox game module (game.json "module": "voxel"): players walking, digging and building in an infinite
// block world, hotbar inventory, fluids and falling blocks, day and night, hostile mobs at night, saves.
// The world, light, generation, ticks and navigation come from the voxel library packages (docs/voxel.md);
// this module is the authoritative game on top of them. Integers and Fixed only; all state lives in State.
#include "../game/HostSystems.h"
#include "engine/sim/Simulation.h"
#include "engine/data/Json.h"
#include "engine/core/Archive.h"
#include <aiverselib/systems/voxel/VoxelQuery.h>
#include <aiverselib/systems/voxelgen/VoxelGen.h>
#include <aiverselib/systems/voxelticks/VoxelTicks.h>
#include <aiverselib/bots/voxelnav/VoxelNav.h>
#include <array>
#include <functional>
namespace aiverse::voxelgame {
using lib::systems::BlockId;using lib::systems::BlockPos;using lib::systems::ChunkPos;using lib::systems::BlockRegistry;using lib::systems::VoxelWorld;
constexpr uint16_t kInputCommand=1;
enum Buttons:uint32_t{Jump=1,Sneak=2,Sprint=4,Break=8,Place=16};
struct Input{
    int32_t forward=0,right=0; // [-1000, 1000], relative to yaw
    Fixed yaw{},pitch{};       // turns, pitch positive up
    uint32_t buttons=0;uint8_t slot=0; // slot 1..9 selects a hotbar slot, 0 keeps it
    uint32_t sequence=0;
    bool operator==(const Input&)const=default;
};
Command command(uint8_t player,const Input& input);
bool decodeInput(const Command& command,Input& out);
struct MotorConfig{Fixed radius=Fixed::fromRatio(3,10),height=Fixed::fromRatio(18,10),eye=Fixed::fromRatio(162,100);
    Fixed speed=Fixed::fromRatio(43,10),sprintSpeed=Fixed::fromRatio(56,10),jumpSpeed=Fixed::fromRatio(84,10),gravity=28_fx,swimSpeed=3_fx,fallSafe=Fixed::fromRatio(108,10);};
struct ItemStack{BlockId block=0;uint32_t count=0;bool operator==(const ItemStack&)const=default;};
constexpr int kHotbar=9;constexpr uint32_t kStack=64;
struct Config{
    uint32_t tickRate=20,snapshotRate=20,maxPlayers=8;
    int simRadius=4,keepRadius=6,viewRadius=6;uint32_t columnsPerTick=2;
    Fixed reach=5_fx;MotorConfig motor;bool creative=false;
    uint32_t dayTicks=12000;           // a full day; time 0 = sunrise
    uint32_t maxMobs=10,spawnEvery=60,mobHealth=20,mobDamage=3,mobAttackTicks=20,playerHealth=20,respawnTicks=60;
    Fixed mobSpeed=Fixed::fromRatio(26,10);uint32_t mobPathNodes=600;
    std::vector<ItemStack> startItems;
};
struct Player{
    uint8_t slot=0;bool active=false;
    Vec3 pos{},vel{};Fixed yaw{},pitch{};
    bool grounded=false,inFluid=false;Fixed fallFrom{};
    int32_t health=20;uint32_t respawnAt=0,deaths=0,hurtAt=0;
    std::array<ItemStack,kHotbar> hotbar{};uint8_t selected=0;
    BlockPos breaking{};bool hasBreaking=false;uint32_t breakProgress=0;
    uint32_t lastButtons=0,placeReadyAt=0,attackReadyAt=0,lastSequence=0;
    Input input{};
    bool operator==(const Player&)const=default;
};
struct Mob{
    uint32_t id=0;Vec3 pos{},vel{};Fixed yaw{};int32_t health=20;bool grounded=false;
    uint8_t target=0;std::vector<BlockPos> path;uint32_t pathIndex=0,repathAt=0,attackAt=0,hurtAt=0;
    bool operator==(const Mob&)const=default;
};
struct State{
    VoxelWorld world;lib::systems::VoxelTickState ticks;
    std::vector<Player> players;std::vector<Mob> mobs;uint32_t nextMob=1;
    uint64_t time=0;  // ticks since the world started; time % dayTicks is the time of day
    Vec3 spawn{};
};
// Content shared by the server, clients and tools: blocks, generator, tick rules, game settings.
struct Content{
    BlockRegistry blocks;lib::systems::VoxelGenConfig gen;lib::systems::VoxelTickRules ticks;Config config;
    lib::systems::VoxelBounds bounds;
};
Content readContent(const Json& rules); // throws JsonError with paths
// Shared by the authority and client prediction: one input tick of a body's movement in the world.
void movePlayer(Player& p,const Input& in,const Config& c,const VoxelWorld& w,const BlockRegistry& r,Fixed dt);
lib::systems::VoxelBox playerBox(Vec3 pos,const MotorConfig& m);
lib::systems::VoxelBox mobBox(Vec3 pos);
Vec3 lookDirection(Fixed yaw,Fixed pitch);
// 0 at noon, 11 at midnight: how much sky light is lost at this time of day (spawning, view).
uint8_t skyDarkening(uint64_t time,uint32_t dayTicks);
float daylight(uint64_t time,uint32_t dayTicks); // presentation only: 0.2 night .. 1 day
// Block or mob the player looks at.
struct Target{bool block=false;BlockPos pos{},before{};int face=-1;uint32_t mob=0;Fixed distance{};};
Target target(const Player& p,const State& s,const Config& c,const BlockRegistry& r);
// Archive layout (saves, network snapshots).
void io(Archive&,ItemStack&);void io(Archive&,Input&);void io(Archive&,Player&);void io(Archive&,Mob&);

// Voxel world systems (blocks, players, mobs, streaming). game::GameCore owns one and runs it when the
// rules have a "voxelGame" block (engine/game/HostSystems.cpp).
class VoxelHost final:public game::HostSystems{
public:
    void setup(Simulation&,const Json& rules,const Json& map)override;
    void applyCommand(Simulation&,const Command&)override;
    void hashState(Simulation&,Hasher&)override;
    bool serializeState(Simulation&,Archive&)override;
    int result(Simulation&)override{return 0;}
    const Content& content()const{return content_;}
    const Config& config()const{return content_.config;}
    State& state(Simulation& sim)const{return *sim.world().resource<State>();}
private:
    void tick(Simulation&);
    void stream(State&);
    void ensureColumn(State&,int cx,int cz);
    void tickPlayer(Simulation&,State&,Player&);
    void tickMobs(Simulation&,State&);
    void changed(State&,BlockPos,BlockId oldBlock);
    bool give(Player&,BlockId,uint32_t)const;
    Content content_;
    std::unique_ptr<lib::systems::VoxelGenerator> generator_;
};
// The voxel systems of a simulation run by game::GameCore; host() throws when the rules have no voxel world.
VoxelHost& host(Simulation&);
VoxelHost* tryHost(Simulation&);
}
