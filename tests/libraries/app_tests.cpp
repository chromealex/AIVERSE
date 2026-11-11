#include <aiverselib/app/Binding.h>
#include <aiverselib/app/Screens.h>
#include <aiverselib/app/Theme.h>
#include <aiverselib/app/Lifecycle.h>
#include <aiverselib/app/MatchBridge.h>
#include <aiverselib/app/Session.h>
#include <aiverselib/app/Content.h>
#include <aiverselib/app/Balance.h>
#include <aiverselib/app/Scenario.h>
#include <aiverselib/app/Performance.h>
#include <aiverselib/app/Genre.h>
#include <aiverselib/app/Planner.h>
#include <cstdlib>
#include <iostream>
#include <limits>
using namespace aiverse::lib::app;
#define CHECK(x) do { if(!(x)){std::cerr<<"failed line "<<__LINE__<<": "<<#x<<'\n';std::exit(1);} } while(false)
int main(){
    BindingModel model;CHECK(model.set("gold",std::int64_t{10}));CHECK(!model.set("gold",std::int64_t{10}));
    model.set("gold",std::int64_t{12});CHECK(model.drain().size()==1);CHECK(model.drain().empty());model.invalidate();CHECK(model.drain().size()==1);
    CHECK(escapeMarkup("<a>&\"'")=="&lt;a&gt;&amp;&quot;&#39;");Actions actions(1);CHECK(actions.push({"build","farm"}));CHECK(!actions.push({"build","tower"}));CHECK(actions.drain().size()==1);
    Screen hud{"hud","hud.title",ScreenKind::Hud,false,{{"farm","farm.label","10","build","",true},{"tower","tower.label","20","build","poor",false}}};
    CHECK(validScreen(hud));auto bad=hud;bad.rows.push_back(bad.rows[0]);CHECK(!validScreen(bad));
    ScreenStack stack;CHECK(stack.open(hud));CHECK(stack.click("hud","farm"));CHECK(!stack.click("hud","tower"));
    CHECK(stack.open({"pause","pause.title",ScreenKind::Pause,true,{}}));CHECK(stack.blocksGameInput());CHECK(stack.open(hud));CHECK(!stack.click("hud","farm"));CHECK(stack.close("pause"));CHECK(stack.click("hud","farm"));
    auto markup=screenMarkup(hud,[](const auto& s){return "<"+s+">";});CHECK(markup.find("&lt;hud.title&gt;")!=std::string::npos);
    auto phone=layout(390,844,{0,40,0,30});CHECK(phone&&phone->columns==1&&phone->height==774);
    CHECK(layout(1920,1080,{})->columns==4);CHECK(!layout(100,100,{100,0,100,0}));CHECK(!uiScale(std::numeric_limits<float>::infinity()));CHECK(*uiScale(10)==2);CHECK(themeStyles({}));Theme unsafe;unsafe.accent="red;}";CHECK(!themeStyles(unsafe));
    Lifecycle life;CHECK(!life.send(AppEvent::Loaded));CHECK(life.send(AppEvent::Ready));CHECK(life.send(AppEvent::Start));auto ticket=life.generation;
    CHECK(life.send(AppEvent::Back));CHECK(life.send(AppEvent::Start));CHECK(!life.loaded(ticket));CHECK(life.loaded(life.generation));CHECK(life.send(AppEvent::Pause));CHECK(life.send(AppEvent::Resume));CHECK(life.send(AppEvent::Finish));CHECK(life.send(AppEvent::Quit));CHECK(!life.send(AppEvent::Ready));
    CHECK(rewardKey({"a:b","c","gold",1})!=rewardKey({"b","c:a","gold",1}));
    Session session;CHECK(session.connect("sha"));auto epoch=session.epoch;CHECK(!session.connected(epoch-1,"sha"));CHECK(session.connected(epoch,"sha"));CHECK(session.start());CHECK(session.accept(epoch,0));CHECK(!session.accept(epoch,0));
    session.disconnected(10);CHECK(!session.retry(100));CHECK(session.retry(1010));CHECK(!session.connected(epoch,"sha"));CHECK(session.connected(session.epoch,"sha"));CHECK(session.phase==SessionPhase::Playing);CHECK(!session.accept(session.epoch,0));CHECK(session.accept(session.epoch,1));
    session.disconnected(2000);for(int i=0;i<3;++i)CHECK(session.retry(3000+1000*i));CHECK(!session.retry(6000));CHECK(session.phase==SessionPhase::Closed);
    CHECK(session.connect("new"));CHECK(!session.connected(session.epoch,"old"));CHECK(session.phase==SessionPhase::Closed);
    std::vector<ContentPackage> content{{"mod","b",2,{{"base",1,1}},{}},{"base","a",1,{},{}}};auto order=resolveContent(content);CHECK(order&&order.ids.front()=="base");
    content[1].dependencies.push_back({"mod",1,0});CHECK(!resolveContent(content));content[1].dependencies.clear();content[0].dependencies[0].minimum=2;CHECK(!resolveContent(content));
    std::map<std::string,BalanceRow> report;CHECK(roundRobin({"a","b"},{1,2}).size()==4);CHECK(roundRobin({"a","a"},{1}).empty());CHECK(addSample(report,{1,10,"a","b","a"}));CHECK(addSample(report,{2,10,"a","b",""}));CHECK(report["a"].wins==1&&report["b"].draws==1);CHECK(!addSample(report,{1,10,"a","b","c"}));
    Scenario scenario{{{0,"build","farm"},{2,"train","worker"}},{{2,"units",1,2}}};ScenarioRunner runner;int emitted=0;
    auto emit=[&](const ScenarioStep&){++emitted;return true;};auto read=[](const std::string&)->std::optional<std::int64_t>{return 1;};
    CHECK(runner.advance(scenario,0,emit,read));CHECK(runner.advance(scenario,1,emit,read));CHECK(runner.advance(scenario,2,emit,read));CHECK(runner.complete(scenario)&&emitted==2);
    ScenarioRunner missed;CHECK(!missed.advance(scenario,1,emit,read));CHECK(missed.failed);
    ScenarioRunner missingMetric;CHECK(missingMetric.advance(scenario,0,emit,read));CHECK(!missingMetric.advance(scenario,2,emit,[](const auto&)->std::optional<std::int64_t>{return {};}));
    CHECK(!summarize({}));std::vector<std::uint64_t> samples;for(int i=1;i<=100;++i)samples.push_back(i);auto metrics=*summarize(samples);CHECK(metrics.p95==95&&metrics.median==50&&metrics.peak==100);
    CHECK(checkBudget(metrics)==BudgetResult::Pass);CHECK(checkBudget(metrics,{90,1000,100,30})==BudgetResult::AbsoluteLimit);CHECK(checkBudget(metrics,{},Measurements{50,80,100,100})==BudgetResult::Regression);
    CHECK(checkBudget(metrics,{100,100,100,101})==BudgetResult::InsufficientSamples);
    for(auto genre:{Genre::ClassicRts,Genre::Colony,Genre::TowerDefense,Genre::LaneBattler})CHECK(!genreKit(genre).acceptance.empty());
    std::vector<ChangeRecipe> recipes{{"ui",{"data"},{"ui/hud.rml"},{"visual"}},{"data",{},{"rules.json"},{"headless"}}};auto plan=planChanges(recipes,{"ui"});CHECK(plan&&plan.recipes.front()=="data"&&plan.checks.size()==2);CHECK(!planChanges(recipes,{"missing"}));recipes[1].dependencies={"ui"};CHECK(!planChanges(recipes,{"ui"}));
    std::cout<<"Application package tests passed\n";
}
