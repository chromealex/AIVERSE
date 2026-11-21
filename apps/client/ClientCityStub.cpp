#include "GameClient.h"

#include <cstdlib>
#include <stdexcept>

namespace aiverse::client {

void GameClient::cityPrepare(Json&, Json&) {
    city_.reset();
    if (const char* address = std::getenv("AIVERSE_CITY"); address && *address)
        throw std::runtime_error("AIVERSE_CITY requires the city-client engine module");
}

void GameClient::cityAttach() {}
float GameClient::cityFrame(double) { return 0.0f; }

} // namespace aiverse::client
