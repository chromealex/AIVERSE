#include <aiverselib/app/Binding.h>
#include <aiverselib/app/Screens.h>
#include <aiverselib/app/Theme.h>
#include <aiverselib/app/Lifecycle.h>
#include <aiverselib/app/Scenario.h>
#include <aiverselib/app/Genre.h>
#include <aiverselib/app/Planner.h>
#include <iostream>
#include <map>
using namespace aiverse::lib::app;
int main(){
    Lifecycle lifecycle;lifecycle.send(AppEvent::Ready);lifecycle.send(AppEvent::Start);lifecycle.loaded(lifecycle.generation);
    BindingModel model;model.set("gold",std::int64_t{250});model.set("units",std::int64_t{4});
    ScreenStack stack;Actions actions;
    Screen build{"build","build.title",ScreenKind::Build,false,{{"farm","farm.title","100","build","",true},{"tower","tower.title","300","build","resources.low",false}}};
    stack.open(build);auto action=stack.click("build","farm");if(action)actions.push(*action);
    for(const auto& a:actions.drain())std::cerr<<"Command request: "<<a.id<<" "<<a.payload<<'\n';
    const std::map<std::string,std::string> labels{{"build.title","Construction"},{"farm.title","Farm"},{"tower.title","Tower"},{"resources.low","Insufficient resources"}};
    auto translate=[&](const std::string& key){auto it=labels.find(key);return it==labels.end()?key:it->second;};
    std::cout<<"<rml><head><title>Native screens</title><style>"<<*themeStyles({})<<"</style></head><body>"<<screenMarkup(build,translate)<<"</body></rml>\n";
}
