#include "LiveOpsInternal.h"
#include "LiveOpsSchema.h"
#include "libraries/include/aiverselib/Graphs.h"
namespace aiverse::meta {
namespace {
using namespace live;
void validate(const Json& value,const Json& schema,const Config& economy,const std::string& path,int depth=0) {
    require(depth<=40,path+": nesting"); auto type=schema.getStr("type");
    if(type=="object") {
        require(value.isObject(),path+": expected object");
        if(schema.has("maxProperties"))require(value.size()<=size_t(schema.getInt("maxProperties")),path+": object size");
        for(auto& key:schema["required"].items())require(value.has(key.str()),path+": missing "+key.str());
        std::set<std::string> seen;
        for(auto& [key,v]:value.members()) {
            require(!key.empty()&&key.size()<=128&&seen.insert(key).second,path+": invalid/duplicate key");
            const auto& rule=schema["properties"][key];
            if(!rule.isNull())validate(v,rule,economy,path+"."+key,depth+1);
            else if(schema["additionalProperties"].isObject())validate(v,schema["additionalProperties"],economy,path+"."+key,depth+1);
            else require(!schema.has("additionalProperties")||schema["additionalProperties"].boolean(),path+": unknown key "+key);
        }
        if(schema.getBool("x-assets"))checkBundle(value,economy);
    } else if(type=="array") {
        require(value.isArray()&&value.size()>=size_t(schema.getInt("minItems"))&&value.size()<=size_t(schema.getInt("maxItems",1000)),path+": array size");
        std::set<std::string> seen;
        for(auto& v:value.items()) {if(schema.getBool("uniqueItems"))require(seen.insert(canonical(v).dump(-1)).second,path+": duplicate item");validate(v,schema["items"],economy,path+"[]",depth+1);}
    } else if(type=="integer") { auto n=integer(value,schema.getInt("minimum"),schema.getInt("maximum",kMaxAmount));(void)n; }
    else if(type=="boolean")require(value.type()==Json::Type::Bool,path+": boolean");
    else if(type=="string")require(value.isString()&&value.str().size()>=size_t(schema.getInt("minLength"))&&value.str().size()<=size_t(schema.getInt("maxLength",128)),path+": string length");
    if(schema.has("enum")) {bool found=false;for(auto& v:schema["enum"].items())if(v==value)found=true;require(found,path+": enum");}
}
void dag(const Json& defs,const std::function<std::vector<std::string>(const Json&)>& refs,const std::string& path) {
    std::map<std::string,size_t> indexes;for(auto& [id,d]:defs.members()){(void)d;indexes[id]=indexes.size();}
    lib::Graph graph(indexes.size());
    for(auto& [id,d]:defs.members())for(auto& ref:refs(d)){require(indexes.count(ref),path+": unknown reference "+ref);graph[indexes[id]].push_back(indexes.at(ref));}
    auto sorted=lib::topological(graph);require(bool(sorted),path+": dependency cycle");
}
std::vector<std::string> dependencies(const Json& j) {std::vector<std::string> out;for(auto& v:j["requires"].items())out.push_back(v.str());return out;}
}
void validateLiveOps(const Json& x,const Config& economy) {
    validate(x,Json::parse(live::kSchema),economy,"liveops");
    dag(x["loot"],[](const Json& d){std::vector<std::string> out;for(auto& e:d["entries"].items())if(e.has("table"))out.push_back(e.getStr("table"));return out;},"loot");
    for(auto& [id,d]:x["loot"].members()) {
        std::set<std::string> ids;bool rare=false;
        for(auto& e:d["entries"].items()) {require(ids.insert(e.getStr("id")).second,id+": duplicate entry");require(e.has("reward")!=e.has("table"),id+": exactly reward or table");rare|=e.getBool("rare");
            if(e.has("uniqueAsset"))require(economy.assets.count(e.getStr("uniqueAsset"))&&e.has("duplicateReward"),id+": unique asset/fallback");}
        require(!d.getInt("pity")||rare,id+": pity without rare entry");
    }
    for(auto& [id,d]:x["research"].members()){dag(d["nodes"],dependencies,"research "+id);require(d["nodes"].size()>=size_t(d.getInt("prestigeCount")),id+": prestige count");}
    dag(x["quests"],dependencies,"quests");
    for(auto& [id,d]:x["events"].members()) {int64_t duration=0;std::set<std::string> phases;for(auto& phase:d["phases"].items()){duration=add(duration,phase.getInt("duration"));require(phases.insert(phase.getStr("id")).second,id+": duplicate phase");}require(duration<=d.getInt("period"),id+": phases exceed period");}
    for(auto& group:{"albums","events","wars"})for(auto& [id,d]:x[group].members())require(!d.getInt("end")||d.getInt("end")>d.getInt("start"),id+": schedule");
    for(auto& [id,d]:x["wars"].members())require(d.getInt("end")>d.getInt("start"),id+": finite war end required");
    for(auto& [id,d]:x["jobs"].members()){require(economy.assets.count(d.getStr("speedAsset")),id+": speed asset");require(d.getInt("maxQueued")>=d.getInt("slots"),id+": queue capacity");}
    for(auto& [id,d]:x["ratings"].members())require(d.getInt("minimum")<=d.getInt("initial")&&d.getInt("initial")<=d.getInt("maximum"),id+": rating range");
    for(auto& [id,d]:x["matchmaking"].members()){require(d.getInt("maxRange")>=d.getInt("initialRange"),id+": matchmaking range");require(x["ratings"].has(d.getStr("rating")),id+": unknown matchmaking rating");}
    for(auto& [id,d]:x["referrals"].members())for(auto& [code,p]:d["codes"].members()){(void)code;require(!p.getInt("end")||p.getInt("end")>p.getInt("start"),id+": promo schedule");}
    for(auto& [id,d]:x["chains"].members()) {
        require(d["nodes"].has(d.getStr("start")),id+": chain start");
        dag(d["nodes"],[](const Json& n){std::vector<std::string> out;for(auto k:{"accept","reject"})if(n.has(k))out.push_back(n.getStr(k));return out;},"chain "+id);
        for(auto& [node,n]:d["nodes"].members()){(void)node;auto offer=economy.offers.find(n.getStr("offer"));require(offer!=economy.offers.end()&&offer->second.product.empty(),id+": chain needs a virtual offer");}
    }
}
}
