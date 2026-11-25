#include "engine/render/PrismaCompositor.h"
#include "engine/render/PrismaData.h"
#include <cstdio>
using namespace aiverse;
using namespace aiverse::render;

int main() {
    const Json game = Json::parse(R"({"hdr":"off","compositor":{
      "targets":[{"id":"half","divisor":2},{"id":"blur","divisor":2},{"id":"output"}],
      "passes":[{"id":"combine","op":"add","inputs":["blur","scene"],"target":"output"},
                {"id":"extract","op":"bright","inputs":["scene"],"target":"half"},
                {"id":"spread","op":"blurX","inputs":["half"],"target":"blur"}],
      "output":"output"}})");
    std::vector<std::string> issues;
    const auto settings = parsePrisma(game, {}, &issues);
    if (!issues.empty() || !settings.anyActive(PrismaQuality::Low) || !settings.compositor) return 1;
    const auto& c = *settings.compositor;
    if (c.plan().order != std::vector<PrismaGraphId>{1,2,0} || c.divisor(1) != 2 ||
        c.plan().resources[1].slot == c.plan().resources[2].slot || c.output() != 3) return 2;
    if (prismaCostReport(settings)["total"]["desktop"].getInt("passes") != 4) return 3;
    for (auto text : {R"({"compositor":null})", R"({"replace":true})"})
        if (parsePrisma(game, Json::parse(text)).compositor) return 4;
    const Json graph = game["compositor"];
    for (int fault = 0; fault < 6; ++fault) {
        Json bad = graph;
        auto& pass = bad.member("passes").at(1);
        if (fault == 0) pass.set("inputs",Json::parse(R"(["blur"])"));
        if (fault == 1) pass.set("op",Json::string("unknown"));
        if (fault == 2) pass.set("target",Json::string("scene"));
        if (fault == 3) bad.member("targets").at(0).set("divisor",Json::number(2.5));
        if (fault == 4) pass.set("target",Json::string("blur"));
        if (fault == 5) bad.set("output",Json::string("half"));
        Json map = Json::object(); map.set("compositor",bad); issues.clear();
        if (parsePrisma(game,map,&issues).compositor || issues.empty()) return 5;
    }
    std::puts("PASS compositor ordering, sizing, alias safety, estimate, map overrides and invalid graphs");
}
