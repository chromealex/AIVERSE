#include "apps/studio-native/StudioWizardDictation.h"
#include <iostream>

namespace fake {
bool supported = true, busy = false, listening = false, recording = false;
std::string locale, text, error;
}
namespace aiverse::editor::speech {
bool supported() { return fake::supported; }
void start(const std::string& locale, const std::vector<std::string>&) {
    fake::locale = locale;
    fake::text.clear(); fake::error.clear();
    fake::busy = fake::listening = true;
}
void stop() { fake::listening = fake::recording = false; }
void cancel() { stop(); fake::busy = false; }
bool busy() { return fake::busy; }
bool listening() { return fake::listening; }
bool recording() { return fake::recording; }
std::string transcript() { return fake::text; }
bool takeResult(std::string& out) { out = fake::text; fake::text.clear(); return !out.empty(); }
std::string error() { return fake::error; }
}
int main() {
    using namespace aiverse;
    studio::StudioWizardDictation controller;
    auto call = [&](const char* action, const char* owner = "idea") {
        Json req = Json::object();
        req.set("action", Json::string(action)); req.set("owner", Json::string(owner)); req.set("language", Json::string("fr"));
        return controller.handle(req);
    };
    auto require = [](bool ok, const char* what) { if (!ok) throw std::runtime_error(what); };
    require(call("available").getBool("supported") && !fake::busy, "probe must not record");
    require(call("start").getStr("phase") == "starting" && fake::locale == "fr-FR", "start authorization in game language");
    fake::recording = true; fake::text = "port";
    require(call("poll").getStr("phase") == "listening" && call("poll").getStr("text") == "port", "live transcript");
    require(call("poll", "economy").getStr("text").empty(), "private field ownership");
    call("cancel", "economy"); require(fake::recording, "only owner can cancel");
    bool rejected = false;
    try { call("start", "economy"); } catch (const std::runtime_error&) { rejected = true; }
    require(rejected, "overlapping sessions");
    require(call("stop").getStr("phase") == "finishing", "finish recording");
    fake::busy = false; fake::text = "port maritime";
    require(call("poll").getStr("text") == "port maritime" && call("poll").getStr("phase") == "idle", "final result delivered once");
    call("start", "economy"); call("cancel", "economy");
    require(!fake::busy && call("poll", "economy").getStr("phase") == "idle", "cancel before authorization");
    call("start"); fake::busy = fake::listening = false; fake::error = "permission denied";
    require(call("poll").getStr("error") == "permission denied", "permission error");
    fake::supported = false;
    require(!call("available").getBool("supported"), "system dictation fallback");
    std::cout << "Wizard dictation lifecycle passed\n";
}
