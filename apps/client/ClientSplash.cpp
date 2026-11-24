#include "ClientSplash.h"

namespace aiverse::client {

void ClientBoot::init(gfx::Device& dev, GameClient& client, Mode mode, std::function<void()> afterLoad) {
    client_ = &client;
    Json block;
    std::string text;
    try {
        if (platform::readContentFile("game.json", text)) block = Json::parse(text)["splash"];
    } catch (const std::exception&) {}   // the client reports a broken game.json in its menu
    const bool contentWait = block.isObject() && block.getBool("waitForContent", false);
    if (contentWait) platform::requestAllContent();
    splash::Boot::Hooks h;
    h.steps = GameClient::kInitSteps;
    h.step = [&client](gfx::Device& d, int i) { client.initStep(d, i); };
    h.weight = [](int i) { return GameClient::initWeight(i); };
    h.frame = [&client](gfx::Device& d, double dt) { client.frame(d, dt); };
    h.overlay = [&client](gfx::Device& d, double dt, float shown, int step) {
        if (step > 1) client.drawLoadingStatus(d, dt, shown, client.initLabel(step));
    };
    if (contentWait) {   // split builds: downloads and local loading share the bar
        h.moreLoading = [] { platform::ContentProgress c = platform::contentProgress(); return c.busy || c.ready + c.failed < c.chunks; };
        h.moreProgress = [] { platform::ContentProgress c = platform::contentProgress(); return c.bytesTotal ? float(double(c.bytesReady) / double(c.bytesTotal)) : 1.0f; };
    }
    h.afterLoad = std::move(afterLoad);
    h.handOver = [&client] { return client.mapLoading(); };
    h.pixelScale = [] { return platform::dpiScale(); };
    const auto m = mode == Mode::Off ? splash::Boot::Mode::Off : mode == Mode::LoadingOnly ? splash::Boot::Mode::LoadingOnly : splash::Boot::Mode::Full;
    boot_.init(dev, block, m, [](const std::string& p, std::string& out) { return platform::readContentFile(p, out); }, std::move(h));
}

void ClientBoot::frame(gfx::Device& dev, double dt) { boot_.frame(dev, dt); }

void ClientBoot::event(const platform::Event& e) {
    if (!boot_.booting()) { client_->event(e); return; }
    if (e.type == platform::EventType::MouseDown || e.type == platform::EventType::TouchBegin ||
        (e.type == platform::EventType::KeyDown && !e.repeat))
        boot_.skip();
}

void ClientBoot::shutdown() {
    boot_.shutdown();
    // a window closed in the middle of loading: the half-initialised client is left to the OS
    if (boot_.loaded()) client_->shutdown();
}

} // namespace aiverse::client
