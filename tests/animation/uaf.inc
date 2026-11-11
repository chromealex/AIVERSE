    // Trait stack order, private state, failure isolation.
    std::string order;
    auto bottom=std::make_shared<AnimationTrait>(),top=std::make_shared<AnimationTrait>();
    bottom->stateFloats=1;top->stateFloats=1;
    bottom->preUpdate=[&](const Json&,AnimationParameters&,float dt,std::span<float> s){order+='b';s[0]+=dt;return true;};
    top->preUpdate=[&](const Json&,AnimationParameters&,float,std::span<float>){order+='t';return true;};
    bottom->postUpdate=[&](const Json&,AnimationParameters&,float,std::span<float>){order+='B';return true;};
    top->postUpdate=[&](const Json&,AnimationParameters&,float,std::span<float>){order+='T';return true;};
    bottom->preEvaluate=[&](const Json&,const AnimationParameters&,std::span<const float>,Pose&){order+='c';return true;};
    top->preEvaluate=[&](const Json&,const AnimationParameters&,std::span<const float>,Pose&){order+='d';return true;};
    bottom->postEvaluate=[&](const Json&,const AnimationParameters&,std::span<const float> s,Pose& p){order+='C';p[0].t.x+=s[0];return true;};
    top->postEvaluate=[&](const Json&,const AnimationParameters&,std::span<const float>,Pose& p){order+='D';p[0].t.x*=2;return true;};
    auto stacked=parse(R"({"nodes":[{"id":"base","kind":"bind"},{"id":"stack","kind":"stack","input":"base","traits":[{"trait":"bottom"},{"trait":"top"}]}],"output":"stack"})");
    auto stackGraph=AnimationGraph::compile(m,stacked,{},{{"bottom",bottom},{"top",top}});
    a.reset(stackGraph);b.reset(stackGraph);order.clear();a.update(.25f);a.evaluate(p);
    check(order=="tbBTdcCD","traits wrap in top-down pre / bottom-up post order");check(near(p[0].t.x,.5f),"trait composition");
    b.evaluate(p);check(near(p[0].t.x,0),"trait state isolated");a.evaluate(p);check(near(p[0].t.x,.5f),"trait evaluate does not tick");
    auto badTrait=std::make_shared<AnimationTrait>();badTrait->postEvaluate=[](const Json&,const AnimationParameters&,std::span<const float>,Pose& p){p[0].t.x=999;return false;};
    auto badStack=stacked;badStack.member("nodes").at(1).set("traits",parse(R"([{"trait":"bad"}])"));
    a.reset(AnimationGraph::compile(m,badStack,{},{{"bad",badTrait}}));a.evaluate(p);check(!a.healthy()&&near(p[0].t.x,0),"failed trait discards partial pose");
    // Independent subgraph instances and typed bindings.
    auto nested=parse(R"({"parameters":{"rate":2},"graphs":{"child":{"parameters":{"speed":1},"nodes":[{"id":"clip","kind":"clip","clip":"Move","parameter":"speed"}],"output":"clip"}},"nodes":[{"id":"first","kind":"subgraph","graph":"child","bindings":{"speed":"rate"}},{"id":"second","kind":"subgraph","graph":"child"},{"id":"mix","kind":"blend","a":"first","b":"second","weight":0.5}],"output":"mix"})");
    a.reset(AnimationGraph::compile(m,nested));a.update(.25f);a.evaluate(p);check(near(p[0].t.x,.75f),"nested graph bindings and clocks");
    check(a.child("first")&&a.child("second")&&a.child("first")!=a.child("second"),"nested graph instances are distinct");
    a.child("second")->parameters().set("speed",0.f);a.update(.1f);a.evaluate(p);check(near(p[0].t.x,.95f),"unbound child parameters persist");
    auto cycle=nested;cycle.member("graphs").member("child").set("nodes",parse(R"([{"id":"clip","kind":"subgraph","graph":"child"}])"));rejects(cycle,"reject recursive subgraphs");
    auto mismatch=nested;mismatch.member("nodes").at(0).set("bindings",parse(R"({"missing":"rate"})"));rejects(mismatch,"reject invalid child bindings");
    // Named forward rig operations use local/model spaces and ordinary parameter bindings.
    auto rig=parse(R"({"parameters":{"control":[2,0,0],"weight":1},"nodes":[{"id":"base","kind":"bind"},{"id":"rig","kind":"rig","input":"base","units":[{"op":"transform","bone":"root","translationParameter":"control"},{"op":"transform","bone":"knee","space":"model","translation":[3,1,0]},{"op":"copy","bone":"other","source":"knee","space":"model"}]}],"output":"rig"})");
    a.reset(AnimationGraph::compile(m,rig));a.evaluate(p);worldMatrices(m,p,world);check(near(p[1].t.x,1)&&near(world[3].at(0,3),3),"rig model/local transform conversion and copy");
    rig.member("nodes").at(1).set("units",parse(R"([{"op":"transform","bone":"other","translation":[3,4,5]},{"op":"limit","bone":"other","min":[-1,-1,-1],"max":[1,2,3]}])"));
    a.reset(AnimationGraph::compile(m,rig));a.evaluate(p);check(near(p[3].t.x,1)&&near(p[3].t.y,2)&&near(p[3].t.z,3),"rig translation limits");
    rig.member("nodes").at(1).set("units",parse(R"([{"op":"aim","bone":"root","target":"control","axis":[0,1,0]}])"));
    a.reset(AnimationGraph::compile(m,rig));a.evaluate(p);check(near(p[0].r.rotate({0,1,0}).x,1),"rig aim towards control");
    rig.member("nodes").at(1).set("units",parse(R"([{"op":"transform","bone":"other","translation":[10,0,0],"weightParameter":"weight"}])"));
    a.reset(AnimationGraph::compile(m,rig));a.parameters().set("weight",.25f);a.evaluate(p);check(near(p[3].t.x,2.5f),"rig parameter weighting");
    rig.member("nodes").at(1).member("units").at(0).set("bone",Json::string("absent"));rejects(rig,"rig rejects missing bones");
    // Real pose search: opposite root trajectories, feature extraction, stable best match.
    Model locomotion=m;locomotion.clips[0].name="Right";AnimClip left=locomotion.clips[0];left.name="Left";left.channels[0].values={0,0,0,-2,0,0};locomotion.clips.push_back(left);
    auto mm=parse(R"({"parameters":{"future":[0.6,0,0]},"nodes":[{"id":"motion","kind":"motionMatching","database":{"root":"root","clips":["Right","Left"],"bones":["foot"],"trajectory":[{"time":0.3,"parameter":"future"}],"poseWeight":0,"velocityWeight":0,"fps":20,"searchInterval":0.05,"blend":0,"stripRoot":true}}],"output":"motion"})");
    a.reset(AnimationGraph::compile(locomotion,mm));a.update(.1f);a.evaluate(p);check(a.motion("motion")->clip()==0,"motion matching chooses right trajectory");
    check(near(p[0].t.x,0),"motion extraction does not move authoritative root");
    a.parameters().set("future",float3{-.6f,0,0});a.update(.1f);check(a.motion("motion")->clip()==1,"motion matching switches for opposite trajectory");
    a.update(.05f);check(a.motion("motion")->clip()==1,"continuation remains stable");
    auto db=MotionDatabase::compile(locomotion,mm["nodes"][0]["database"],a.parameters());check(db->frames.size()==40&&db->weights.size()==9,"motion database pose velocity trajectory dimensions");
    std::vector<float> features;db->features(0,.9f,features);check(near(features[6],.6f),"trajectory sampling crosses clip loop");
    float score;auto exact=db->search(features,-1,0,score);check(db->frames[exact].clip==0&&std::isfinite(score),"exact weighted nearest neighbour");
    auto invalidMM=mm;invalidMM.member("nodes").at(0).member("database").set("fps",Json::number(int64_t(0)));bool badMotion=false;try{AnimationGraph::compile(locomotion,invalidMM);}catch(const std::exception&){badMotion=true;}check(badMotion,"reject invalid motion sample rate");
    invalidMM=mm;invalidMM.member("nodes").at(0).member("database").member("trajectory").at(0).set("parameter",Json::string("missing"));badMotion=false;try{AnimationGraph::compile(locomotion,invalidMM);}catch(const std::exception&){badMotion=true;}check(badMotion,"reject missing trajectory binding");
