// The client's picture of the city (docs/city-server.md): a local simulation that is never stepped. It is set up
// from the same rules and map as the server's (so the ground, the buildings and the UI are there) and its units
// are moved by what the server tells: the Mirror creates, moves and removes them from the Client's entity table.
// The game client draws that simulation as it draws a single-player one; commands go to the server.
#pragma once
#include <deque>
#include "CityNet.h"

namespace aiverse::net::city {
class Mirror {
public:
    // `local` must be set up from city::prepare()d rules and map with the same seats as the server.
    Mirror(Simulation& local, Client& client);
    ~Mirror();
    // Once per frame after Client::poll: applies what came, advances the interpolation between two snapshots.
    void update(double seconds);
    float alpha() const { return alpha_; }             // for Transform::prevPos -> pos, as Simulation::alpha()
    Entity hero() const { return hero_; }              // the player's hero in the local world (null before the first snapshot)
    Entity localOf(uint32_t serverId) const;
    size_t shown() const { return map_.size(); }
    // The own hero does not wait for the server: he is drawn ahead of the told position by what the stick did
    // during the round trip (off: he is drawn where the server says, like everybody else).
    void setPrediction(bool on) { predict_ = on; }
    double latency() const;                            // seconds the picture is behind the stick (the round trip + a snapshot + a tick)

private:
    void apply();
    void placeHero(double seconds);
    void showEvents();
    void showPremises();
    struct Stick { double at, vx, vz; };
    std::deque<Stick> sticks_;                         // what the player asked for, by local time
    double time_ = 0, heroFrom_[2] = {0, 0}, heroTo_[2] = {0, 0}, lead_[2] = {0, 0}, serverSpeed_ = 0, grip_ = 1;
    bool predict_ = true, heroKnown_ = false, heroInside_ = false, hadEvents_ = false;
    uint32_t premisesVersion_ = 0;
    uint16_t others_ = 0;                              // the local account record that stands for "somebody else"
    Simulation& sim_;
    Client& client_;
    std::unordered_map<uint32_t, Entity> map_;         // server entity -> local entity
    uint32_t tick_ = 0, snapshots_ = 0;
    uint8_t bound_ = 0;
    float alpha_ = 1, interval_ = 0.1f;
    double sinceSnapshot_ = 0;
    Entity hero_ = kNullEntity;
};
}
