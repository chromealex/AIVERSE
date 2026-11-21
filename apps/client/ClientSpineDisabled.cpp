#include "ClientSpine.h"
#include <cstdio>

namespace aiverse::client {

void ClientSpine::init(gfx::Device&, ui::UiLayer&, const Json& manifest) {
    if (manifest["spine"].size() != 0)
        std::fprintf(stderr, "[spine] game.json requests Spine, but this client was built without the spine module\n");
}
void ClientSpine::shutdown() {}
bool ClientSpine::create(const std::string&, const Json&, std::string& error) {
    error = "Spine module excluded from this client build";
    return false;
}
bool ClientSpine::remove(const std::string&) { return false; }
bool ClientSpine::play(const std::string&, const std::string&, bool, int, bool, float) { return false; }
bool ClientSpine::skin(const std::string&, const std::string&) { return false; }
std::vector<std::string> ClientSpine::animations(const std::string&) const { return {}; }
void ClientSpine::render(float) {}

} // namespace aiverse::client
