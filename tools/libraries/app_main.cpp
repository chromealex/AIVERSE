// Host CLI over the same public C++ APIs used by native games.
#include "engine/data/Json.h"
#include <aiverselib/app/Balance.h>
#include <aiverselib/systems/Curves.h>
#include <aiverselib/app/Performance.h>
#include <aiverselib/app/Planner.h>
#include <aiverselib/app/Scenario.h>
#include <aiverselib/app/Genre.h>
#include <aiverselib/app/Screens.h>
#include <aiverselib/app/Theme.h>
#include <charconv>
#include <iostream>
#include <set>
using aiverse::Json;
using namespace aiverse::lib::app;
namespace {
void object(const Json& j,std::initializer_list<std::string> keys){
    if(!j.isObject())throw std::runtime_error("expected object");
    std::set<std::string> allowed(keys),seen;
    for(const auto& [key,value]:j.members()){(void)value;if(!allowed.count(key)||!seen.insert(key).second)throw std::runtime_error("unknown/duplicate key: "+key);}
}
std::string string(const Json& j){if(!j.isString()||j.str().empty())throw std::runtime_error("expected nonempty string");return j.str();}
std::int64_t integer(const Json& j){
    if(!j.isNumber())throw std::runtime_error("expected integer");
    const auto& s=j.numberLiteral();std::int64_t value=0;auto [p,e]=std::from_chars(s.data(),s.data()+s.size(),value);
    if(e!=std::errc{}||p!=s.data()+s.size())throw std::runtime_error("invalid integer");
    return value;
}
std::uint64_t positive(const Json& j){auto v=integer(j);if(v<0)throw std::runtime_error("expected nonnegative integer");return static_cast<std::uint64_t>(v);}
const std::vector<Json>& array(const Json& j){if(!j.isArray())throw std::runtime_error("expected array");return j.items();}
std::vector<std::string> strings(const Json& j){std::vector<std::string> out;for(const auto& v:array(j))out.push_back(string(v));return out;}
Json stringArray(const std::vector<std::string>& v){auto out=Json::array();for(const auto& x:v)out.push(Json::string(x));return out;}
std::vector<std::uint64_t> samples(const Json& j){std::vector<std::uint64_t> out;for(const auto& v:array(j))out.push_back(positive(v));return out;}
std::uint32_t version(const Json& j){auto v=positive(j);if(v>UINT32_MAX)throw std::runtime_error("version out of range");return static_cast<std::uint32_t>(v);}
}
int main(int argc,char** argv){
    if(argc!=3){std::cerr<<"usage: aiverse_app_tools balance|performance|plan|content|scenario|genre|screens|curve input.json\n";return 2;}
    try {
        auto input=Json::parseFile(argv[2]);std::string command=argv[1];auto out=Json::object();bool ok=true;
        if(command=="balance"){
            object(input,{"samples"});std::map<std::string,BalanceRow> report;
            for(const auto& j:array(input["samples"])){
                object(j,{"seed","ticks","first","second","winner"});
                if(!j["winner"].isString())throw std::runtime_error("winner must be a string; empty means draw");
                if(!addSample(report,{positive(j["seed"]),positive(j["ticks"]),string(j["first"]),string(j["second"]),j["winner"].str()}))throw std::runtime_error("invalid match");
            }
            auto rows=Json::object();for(const auto& [id,r]:report){auto row=Json::object();row.set("played",Json::number(static_cast<std::int64_t>(r.played)));row.set("wins",Json::number(static_cast<std::int64_t>(r.wins)));row.set("losses",Json::number(static_cast<std::int64_t>(r.losses)));row.set("draws",Json::number(static_cast<std::int64_t>(r.draws)));rows.set(id,row);}out.set("factions",rows);
        }else if(command=="performance"){
            object(input,{"samples","baseline","maximumP95","maximumPeak","regressionPermille","minimumSamples"});
            Budget b;
            if(input.has("maximumP95"))b.maximumP95=positive(input["maximumP95"]);
            if(input.has("maximumPeak"))b.maximumPeak=positive(input["maximumPeak"]);
            if(input.has("regressionPermille"))b.regressionPermille=version(input["regressionPermille"]);
            if(input.has("minimumSamples"))b.minimumSamples=positive(input["minimumSamples"]);
            auto current=summarize(samples(input["samples"]));if(!current)throw std::runtime_error("no samples");
            std::optional<Measurements> baseline;if(input.has("baseline")){baseline=summarize(samples(input["baseline"]));if(!baseline)throw std::runtime_error("empty baseline");}
            auto result=checkBudget(*current,b,baseline);ok=result==BudgetResult::Pass;
            out.set("status",Json::string(result==BudgetResult::Pass?"pass":result==BudgetResult::Regression?"regression":result==BudgetResult::AbsoluteLimit?"absolute-limit":"insufficient-samples"));
            out.set("p95",Json::number(static_cast<std::int64_t>(current->p95)));out.set("peak",Json::number(static_cast<std::int64_t>(current->peak)));
        }else if(command=="plan"){
            object(input,{"recipes","requested"});std::vector<ChangeRecipe> recipes;
            for(const auto& j:array(input["recipes"])){object(j,{"id","dependencies","files","checks"});recipes.push_back({string(j["id"]),strings(j["dependencies"]),strings(j["files"]),strings(j["checks"])});}
            auto plan=planChanges(recipes,strings(input["requested"]));if(!plan)throw std::runtime_error(plan.error);
            out.set("recipes",stringArray(plan.recipes));out.set("files",stringArray(plan.files));out.set("checks",stringArray(plan.checks));
        }else if(command=="content"){
            object(input,{"packages"});std::vector<ContentPackage> packages;
            for(const auto& j:array(input["packages"])){
                object(j,{"id","digest","version","dependencies","conflicts"});ContentPackage p{string(j["id"]),string(j["digest"]),version(j["version"]),{},strings(j["conflicts"])};
                for(const auto& d:array(j["dependencies"])){object(d,{"id","minimum","maximum"});p.dependencies.push_back({string(d["id"]),version(d["minimum"]),version(d["maximum"])});}packages.push_back(std::move(p));
            }
            auto result=resolveContent(packages);if(!result)throw std::runtime_error(result.error);out.set("order",stringArray(result.ids));
        }else if(command=="scenario"){
            object(input,{"steps","checks"});Scenario s;
            for(const auto& j:array(input["steps"])){object(j,{"tick","action","payload"});if(!j["payload"].isString())throw std::runtime_error("payload must be a string");s.steps.push_back({positive(j["tick"]),string(j["action"]),j["payload"].str()});}
            for(const auto& j:array(input["checks"])){object(j,{"tick","metric","minimum","maximum"});s.checks.push_back({positive(j["tick"]),string(j["metric"]),integer(j["minimum"]),integer(j["maximum"])});}
            if(!validScenario(s))throw std::runtime_error("invalid scenario ordering/range");
            out.set("validated",Json::fromBool(true));
        }else if(command=="curve"){
            object(input,{"points","samples","mode"});std::vector<aiverse::lib::systems::CurvePoint> points;
            for(const auto& p:array(input["points"])){object(p,{"x","y"});points.push_back({integer(p["x"]),integer(p["y"])});}
            auto mode=input.has("mode")?string(input["mode"]):"linear";if(mode!="linear"&&mode!="step")throw std::runtime_error("unknown curve mode");
            if(!aiverse::lib::systems::curve(points,0))throw std::runtime_error("invalid curve definition");
            auto values=Json::array();for(const auto& x:array(input["samples"])){
                auto y=aiverse::lib::systems::curve(points,integer(x),mode=="step"?aiverse::lib::systems::CurveMode::Step:aiverse::lib::systems::CurveMode::Linear);
                if(!y)throw std::runtime_error("invalid curve/domain or intermediate overflow");
                values.push(Json::number(*y));
            }
            if(points.empty())throw std::runtime_error("empty curve");
            out.set("values",std::move(values));
        }else if(command=="genre"){
            object(input,{"genre"});auto genre=parseGenre(string(input["genre"]));
            if(!genre)throw std::runtime_error("unknown genre");
            auto kit=genreKit(*genre);out.set("packages",stringArray(kit.packages));out.set("acceptance",stringArray(kit.acceptance));
            out.set("id",Json::string(kit.id));out.set("family",Json::string(kit.family));
            out.set("integration",stringArray(kit.integration));out.set("coverage",Json::string("recipe"));
        }else if(command=="screens"){
            object(input,{"id","titleKey","rows","translations"});Screen screen;screen.id=string(input["id"]);screen.titleKey=string(input["titleKey"]);
            for(const auto& j:array(input["rows"])){object(j,{"id","labelKey","value","action","reasonKey","enabled"});if(j["enabled"].type()!=Json::Type::Bool||!j["value"].isString()||!j["action"].isString()||!j["reasonKey"].isString())throw std::runtime_error("invalid row fields");screen.rows.push_back({string(j["id"]),string(j["labelKey"]),j["value"].str(),j["action"].str(),j["reasonKey"].str(),j["enabled"].boolean()});}
            if(!validScreen(screen)||!input["translations"].isObject())throw std::runtime_error("invalid screen/translations");
            auto translate=[&](const std::string& key){return string(input["translations"][key]);};
            std::cout<<"<rml><head><title>Screen</title><style>"<<*themeStyles({})<<"</style></head><body>"<<screenMarkup(screen,translate)<<"</body></rml>\n";return 0;
        }else throw std::runtime_error("unknown command");
        out.set("ok",Json::fromBool(ok));std::cout<<out.dump()<<'\n';return ok?0:1;
    }catch(const std::exception& e){auto error=Json::object();error.set("ok",Json::fromBool(false));error.set("error",Json::string(e.what()));std::cout<<error.dump()<<'\n';return 1;}
}
