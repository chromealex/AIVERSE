#include "example_meta.h"
int main() {
    using namespace aiverse::meta;
    auto config=ServiceConfig::parse(aiverse::Json::parse(R"({"economy":{"version":1,"assets":{"gold":{"cap":1000}}},"extensions":{"initial":{"gold":10}}})"));
    aiverse::Json database;int64_t revision=0;
    Service server(kits::example_meta(),config,{},[&](int64_t expected,const aiverse::Json& next){
        if(revision!=expected)return false;
        database=next;revision=next.getInt("revision");return true;
    });
    auto reply=server.execute({"demo",false},{"profile","create","register",aiverse::Json::object()},100);
    return !reply.result||database["profiles"]["demo"]["balances"].getInt("gold")!=10;
}
