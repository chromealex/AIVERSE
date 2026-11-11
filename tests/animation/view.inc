// End-to-end view VM parameter bridge: validates rollback-free presentation state.
TEST(animation_graph_lua_parameters) {
    Json rules=Json::parseFile(kSkirmish+"/rules.json");
    rules.set("viewScripts",Json::parse(R"(["scripts/view/animation.lua"])"));
    Json src=Json::object();src.set("scripts/view/animation.lua",Json::string(R"LUA(
local actor = game.units{player=1}[1]
view.animation(actor, {mix=0.25, enabled=true, target={1,2,3}})
view.animation(actor, {speed=2})
assert(not pcall(view.animation, actor, {mix=0.9, broken=0/0}))
assert(not pcall(view.animation, actor, {target={1,2}}))
assert(not pcall(view.animation, actor, {target={1,2,math.huge}}))
local clear = false
ui.on("clear_animation", function() view.animation(actor, nil) end)
)LUA"));rules.set("$scriptSources",src);
    Json map=Json::parseFile(kSkirmish+"/maps/valley.json");
    Simulation sim(SimConfig{},std::make_unique<game::RtsModule>());sim.setup(rules,map);
    const auto before=sim.checksum();
    game::ViewScripts view;CHECK(view.load(sim,rules,map,1));view.update(sim,.1f);
    for(const auto& l:view.log()) std::printf("animation log: %s\n",l.text.c_str());
    CHECK(view.log().empty());auto store=view.entityFx();CHECK(store->animations.size()==1);
    if(store->animations.size()==1) {
        const auto& p=store->animations.begin()->second.parameters;
        CHECK(p.values.at("mix")==.25f);CHECK(p.values.at("enabled")==1);CHECK(p.values.at("speed")==2);
        CHECK(p.vectors.at("target").z==3);CHECK(!p.values.count("broken"));
    }
    CHECK(sim.checksum()==before);
    view.uiEvent(sim,"clear_animation",Json::array());CHECK(store->animations.empty());
    CHECK(view.load(sim,rules,map,1));CHECK(view.entityFx()!=store);
}

TEST(animation_uaf_lua_graphs_traits_and_reload) {
    Json rules=Json::parseFile(kSkirmish+"/rules.json");rules.set("viewScripts",Json::parse(R"(["scripts/view/uaf.lua"])"));
    Json sources=Json::object();sources.set("scripts/view/uaf.lua",Json::string(R"LUA(
local actor=game.units{player=1}[1]
view.animation_trait("lift", {
 stateSize=1,
 post_update=function(params,state,dt,config) state[1]=state[1]+dt*config.rate;return state end,
 post_evaluate=function(params,state,pose,config) pose[1].t[2]=pose[1].t[2]+state[1]*params.height;return pose end
})
view.animation_define("locomotion", {
 parameters={height=2, target={1,0,0}},
 graphs={base={nodes={{id="bind",kind="bind"}},output="bind"}},
 nodes={{id="nested",kind="subgraph",graph="base"},
        {id="rig",kind="rig",input="nested",units={{op="transform",bone="root",translationParameter="target"}}},
        {id="stack",kind="stack",input="rig",traits={{trait="lift",config={rate=1}}}}},output="stack"
})
view.animation_use(actor,"locomotion")
view.animation(actor,{height=3})
assert(not pcall(view.animation_use,actor,"unknown"))
assert(not pcall(view.animation_trait,"overflow",{stateSize=4294967296}))
local cycle={};cycle.self=cycle
assert(not pcall(view.animation_define,"cycle",cycle))
view.animation_trait("broken",{post_evaluate=function(p,s,pose) pose[1].t[2]=0/0;return pose end})
view.animation_define("broken",{nodes={{id="bind",kind="bind"},{id="stack",kind="stack",input="bind",traits={{trait="broken"}}}},output="stack"})
view.animation_trait("loop",{post_evaluate=function() while true do end end})
view.animation_define("loop",{nodes={{id="bind",kind="bind"},{id="stack",kind="stack",input="bind",traits={{trait="loop"}}}},output="stack"})
)LUA"));rules.set("$scriptSources",sources);
    Json map=Json::parseFile(kSkirmish+"/maps/valley.json");Simulation sim(SimConfig{},std::make_unique<game::RtsModule>());sim.setup(rules,map);
    auto hash=sim.checksum();game::ViewScripts view;CHECK(view.load(sim,rules,map,1));CHECK(view.log().empty());
    auto store=view.entityFx();CHECK(store->animations.size()==1);CHECK(store->animationDefinitions.size()==3);
    auto model=render::loadModel(std::string(AIVERSE_SOURCE_DIR)+"/templates/showcase/models/golem.glb",modeltest::diskReader(),nullptr);
    auto asset=store->animationDefinitions.at("locomotion");render::AnimationGraphInstance graph;
    graph.reset(render::AnimationGraph::compile(*model,asset->definition,{},asset->traits));graph.parameters().set("height",3.f);graph.update(.5f);render::Pose pose;graph.evaluate(pose);
    CHECK(graph.healthy());CHECK(std::abs(pose[0].t.y-1.5f)<.001f);CHECK(std::abs(pose[0].t.x-1.f)<.001f);CHECK(sim.checksum()==hash);
    for(auto name:{"broken","loop"}){auto bad=store->animationDefinitions.at(name);render::AnimationGraphInstance instance;instance.reset(render::AnimationGraph::compile(*model,bad->definition,{},bad->traits));instance.evaluate(pose);CHECK(!instance.healthy());CHECK(std::isfinite(pose[0].t.y));}
    CHECK(view.load(sim,rules,map,1));graph.evaluate(pose);CHECK(!graph.healthy());CHECK(sim.checksum()==hash);
}
TEST(animation_fullbody_lua_definition) {
    Json rules=Json::parseFile(kSkirmish+"/rules.json");rules.set("viewScripts",Json::parse(R"(["scripts/view/fbik.lua"])"));Json sources=Json::object();
    sources.set("scripts/view/fbik.lua",Json::string(R"LUA(
view.animation_define("fbik",{parameters={target={0.6,1.6,0.2}},nodes={
{id="bind",kind="bind"},{id="rig",kind="rig",input="bind",units={{op="fullBodyIK",bone="hips",rootPull=0.3,iterations=32,effectors={{bone="arm.L",target="target"}}}}}},output="rig"})
view.animation_use(game.units{player=1}[1],"fbik")
assert(not pcall(view.animation_load,"escape","../bad"))
assert(not pcall(view.animation_load,"invalid","broken"))
view.animation_load("loaded","fullbody")
)LUA"));rules.set("$scriptSources",sources);
    Json map=Json::parseFile(kSkirmish+"/maps/valley.json");Simulation sim(SimConfig{},std::make_unique<game::RtsModule>());sim.setup(rules,map);auto hash=sim.checksum();game::ViewScripts view;view.setContentReader([](const std::string& path,std::string& out){if(path=="animations/fullbody.json")out=Json::parseFile(std::string(AIVERSE_SOURCE_DIR)+"/examples/animation/fullbody.json").dump();else out="{broken";return true;});CHECK(view.load(sim,rules,map,1));CHECK(view.log().empty());CHECK(view.entityFx()->animationDefinitions.count("loaded")==1);
    auto m=render::loadModel(std::string(AIVERSE_SOURCE_DIR)+"/templates/showcase/models/golem.glb",modeltest::diskReader(),nullptr);auto asset=view.entityFx()->animationDefinitions.at("fbik");render::AnimationGraphInstance instance;instance.reset(render::AnimationGraph::compile(*m,asset->definition));render::Pose pose;instance.evaluate(pose);std::vector<render::mat4> world;render::worldMatrices(*m,pose,world);int hand=m->findNode("arm.L");auto& w=world[size_t(hand)];CHECK(std::abs(w.at(0,3)-.6f)<.03f);CHECK(std::abs(w.at(1,3)-1.6f)<.03f);CHECK(std::abs(w.at(2,3)-.2f)<.03f);CHECK(instance.healthy());CHECK(sim.checksum()==hash);
}

TEST(animation_contacts_lua_and_status) {
    Json rules=Json::parseFile(kSkirmish+"/rules.json");rules.set("viewScripts",Json::parse(R"(["scripts/view/contacts.lua"])"));Json sources=Json::object();
    sources.set("scripts/view/contacts.lua",Json::string(R"LUA(
local actor=game.units{player=1}[1]
view.animation_define("contacts",{parameters={plant=1,obstacle={0.8,1.2,0}},nodes={
{id="bind",kind="bind"},{id="rig",kind="rig",input="bind",units={{op="fullBodyIK",bone="hips",iterations=48,effectors={},contacts={
 feet={{bone="leg.L",offset={0,-0.85,0},radius=0.2,plantParameter="plant"},{bone="leg.R",offset={0,-0.85,0},radius=0.2}},
 obstacles={{kind="plane",point={0,0,0},normal={0,1,0}},{kind="sphere",positionParameter="obstacle",radius=0.1}},
 balance={strength=0.5,masses={{bone="hips",mass=4},{bone="spine",mass=6}}}
}}}}},output="rig"})
view.animation_use(actor,"contacts")
ui.on("contact_status",function()
 local s=view.animation_status(actor)
 assert(s.supported and s.stable and s.supports==2)
 assert(s.penetration<0.005 and s.supportError<0.005 and s.balanceError<0.005)
 view.animation(actor,{plant=0,obstacle={1,1,0}})
end)
)LUA"));rules.set("$scriptSources",sources);
    Json map=Json::parseFile(kSkirmish+"/maps/valley.json");Simulation sim(SimConfig{},std::make_unique<game::RtsModule>());sim.setup(rules,map);auto hash=sim.checksum();game::ViewScripts view;CHECK(view.load(sim,rules,map,1));CHECK(view.log().empty());
    auto store=view.entityFx();auto m=render::loadModel(std::string(AIVERSE_SOURCE_DIR)+"/templates/showcase/models/golem.glb",modeltest::diskReader(),nullptr);auto asset=store->animationDefinitions.at("contacts");render::AnimationGraphInstance graph;graph.reset(render::AnimationGraph::compile(*m,asset->definition));render::Pose pose;graph.evaluate(pose);auto report=graph.contactReport();CHECK(report.stable);CHECK(report.supports==2);store->animations.begin()->second.contacts=report;
    view.uiEvent(sim,"contact_status",Json::array());CHECK(view.log().empty());CHECK(store->animations.begin()->second.parameters.values.at("plant")==0);CHECK(sim.checksum()==hash);
}

TEST(animation_surface_locomotion_lua) {
    Json rules=Json::parseFile(kSkirmish+"/rules.json");rules.set("viewScripts",Json::parse(R"(["scripts/view/crawl.lua"])"));Json sources=Json::object();
    sources.set("scripts/view/crawl.lua",Json::string(R"LUA(
local legs={}
for i=0,3 do legs[#legs+1]={hip="hip"..i,knee="knee"..i,foot="foot"..i} end
view.animation_define("crawl",{parameters={velocity={0,0,0.3},adhesion=1},nodes={{id="bind",kind="bind"},{id="rig",kind="rig",input="bind",units={{op="surfaceLocomotion",bone="body",velocity="velocity",adhesion="adhesion",legs=legs,world=false,obstacles={{kind="plane",normal={0,1,0}}}}}}},output="rig"})
local actor=game.units{player=1}[1]
view.animation_use(actor,"crawl")
ui.on("climb_status",function()
 local s=view.animation_status(actor)
 assert(s.climbing and s.attached and s.stanceFeet>=3 and s.steppingFeet<=1)
 assert(s.footError<0.01 and s.bodyPosition[3]>0 and s.surfaceNormal[2]>0.99)
 view.animation(actor,{velocity={0,0,0},adhesion=0})
end)
)LUA"));rules.set("$scriptSources",sources);
    Json map=Json::parseFile(kSkirmish+"/maps/valley.json");Simulation sim(SimConfig{},std::make_unique<game::RtsModule>());sim.setup(rules,map);auto hash=sim.checksum();game::ViewScripts view;CHECK(view.load(sim,rules,map,1));CHECK(view.log().empty());
    render::Model m;m.nodes.resize(13);m.order.push_back(0);m.nodes[0].name="body";m.nodes[0].t={0,.5f,0};for(int i=0;i<4;++i){int h=1+i*3;float x=i%2?1.f:-1.f,z=i<2?-1.f:1.f;m.nodes[0].children.push_back(h);for(int j=0;j<3;++j){m.order.push_back(h+j);m.nodes[h+j].name=std::string(j==0?"hip":j==1?"knee":"foot")+std::to_string(i);m.nodes[h+j].parent=j==0?0:h+j-1;if(j<2)m.nodes[h+j].children={h+j+1};}m.nodes[h].t={x*.25f,0,z*.25f};m.nodes[h+1].t={x*.35f,-.1f,z*.1f};m.nodes[h+2].t={x*.2f,-.4f,0};}
    auto store=view.entityFx();render::AnimationGraphInstance graph;graph.reset(render::AnimationGraph::compile(m,store->animationDefinitions.at("crawl")->definition));render::Pose pose;for(int i=0;i<30;++i){graph.update(1.f/60);graph.evaluate(pose);}store->animations.begin()->second.locomotion=graph.locomotionReport();view.uiEvent(sim,"climb_status",Json::array());CHECK(view.log().empty());CHECK(store->animations.begin()->second.parameters.values.at("adhesion")==0);CHECK(sim.checksum()==hash);
}
