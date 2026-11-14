// The game client in a persistent city (docs/city-server.md): the simulation is not stepped here, the server's
// city is shown in it (net::city::Mirror) and every command goes to the server.
//   AIVERSE_CITY=host:port   connect when a match starts (the same map and seats as the server)
//   AIVERSE_CITY_SEATS=N     the server's --seats (default 1)
//   AIVERSE_CITY_PLAYER=id   who this is; AIVERSE_CITY_TICKET=hex: the ticket of the login service (a server with a key)
//   AIVERSE_CITY_PREDICT=0   draw the own hero where the server says (no prediction)
#include <chrono>
#include <cstdlib>
#include <thread>
#include "GameClient.h"
#include "engine/net/city/CityMirror.h"

namespace aiverse::client {

struct GameClient::CityLink {
    std::unique_ptr<net::city::Client> client;
    std::unique_ptr<net::city::Mirror> mirror;   // after the client: it goes first
    bool lost = false;
};

// Before the simulation is set up: the city's seats in the map, the connection, the seat the server gave.
void GameClient::cityPrepare(Json& rules, Json& map) {
    city_.reset();
    const char* address = std::getenv("AIVERSE_CITY");
    if (!address || !*address) return;
    const std::string target = address;
    const char* seats = std::getenv("AIVERSE_CITY_SEATS");
    net::city::prepare(rules, map, seats ? std::max(1, std::atoi(seats)) : 1);
    auto conn = net::connectTo(target);
    if (!conn) throw std::runtime_error("AIVERSE_CITY: host:port");
    auto link = std::make_shared<CityLink>();
    const char* player = std::getenv("AIVERSE_CITY_PLAYER");
    const char* ticket = std::getenv("AIVERSE_CITY_TICKET");
    link->client = std::make_unique<net::city::Client>(std::move(conn), net::city::contentId(rules, map), player ? player : "", ticket ? net::city::fromHex(ticket) : std::vector<uint8_t>{});
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(8);
    while (!link->client->ready()) {   // a few round trips: the handshake, hello, welcome
        link->client->poll(net::nowMs());
        if (!link->client->connected()) throw std::runtime_error("city: the server refused (another map, other seats, or no free seat)");
        if (std::chrono::steady_clock::now() > until) throw std::runtime_error("city: no answer from " + target);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    human_ = link->client->slot();
    city_ = std::move(link);
}

// After the simulation is set up: from now on it shows the server's city.
void GameClient::cityAttach() {
    if (!city_ || !sim_) return;
    city_->mirror = std::make_unique<net::city::Mirror>(*sim_, *city_->client);
    if (const char* p = std::getenv("AIVERSE_CITY_PREDICT"); p && *p == '0') city_->mirror->setPrediction(false);
    bots_.clear();
}

// One frame instead of Simulation::advance. Returns the interpolation factor for the scene.
float GameClient::cityFrame(double dt) {
    if (!city_ || !city_->mirror) return 0;
    city_->client->poll(net::nowMs());
    city_->mirror->update(dt);
    if (!sim_->events().empty()) processEvents();   // notices about this player, as after a local tick
    if (!city_->client->connected() && !city_->lost) { city_->lost = true; notice("Connection to the city is lost"); }
    return city_->mirror->alpha();
}
}
