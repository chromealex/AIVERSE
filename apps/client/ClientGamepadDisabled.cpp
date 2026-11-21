#include "GameClient.h"

namespace aiverse::client {

void GameClient::updateGamepad(float) {
    padPanX_ = padPanY_ = 0;
}
void GameClient::runPadAction(const ClientGamepad::Action&) {}
void GameClient::drawGamepadCursor() {}

} // namespace aiverse::client
