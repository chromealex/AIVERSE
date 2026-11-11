    {
        Model body;body.nodes.resize(5);body.order={0,1,2,3,4};
        body.nodes[0].name="pelvis";body.nodes[0].children={1,3};
        for(int i=1;i<5;++i){body.nodes[size_t(i)].name="bone"+std::to_string(i);body.nodes[size_t(i)].parent=i%2?0:i-1;body.nodes[size_t(i)].t={i<3?-1.f:1.f,0,0};if(i%2)body.nodes[size_t(i)].children={i+1};}
        AnimationParameters params;params.set("left",float3{-1,1,0});params.set("right",float3{1,1,0});params.set("rotation",float3{});params.set("strength",1.f);
        auto settings=parse(R"({"bone":"pelvis","iterations":64,"effectors":[{"bone":"bone2","target":"left","weightParameter":"strength"},{"bone":"bone4","target":"right"}],"joints":[{"bone":"pelvis","stiffness":1}]})");
        Pose pose;bindPose(body,pose);auto solver=FullBodyIK::compile(body,settings,params);auto solved=solver->solve(body,params,pose);worldMatrices(body,pose,world);
        check(solved.error<.01f,"full body simultaneous branch goals converge");check(near(pose[0].t.y,0),"full body fixed root remains fixed");
        for(size_t i=1;i<pose.size();++i)check(near(length(pose[i].t),1),"full body preserves bone lengths");
        auto oriented=settings;oriented.member("effectors").at(0).set("rotation",Json::string("rotation"));oriented.member("effectors").at(0).set("rotationWeight",Json::number(1.0));
        bindPose(body,pose);auto orientedSolver=FullBodyIK::compile(body,oriented,params);auto orientedResult=orientedSolver->solve(body,params,pose);worldMatrices(body,pose,world);check(orientedResult.error<.02f&&std::abs(rotationOf(world[2]).w)>.999f,"effector orientation and position solved together");
        bindPose(body,pose);solver->solve(body,params,pose);
        auto first=pose;bindPose(body,pose);solver->solve(body,params,pose);check(near(pose[1].r.z,first[1].r.z),"full body repeatability");
        bindPose(body,pose);auto unchanged=pose;solver->solve(body,params,pose,0);check(near(pose[1].r.z,unchanged[1].r.z),"zero rig weight keeps input");
        settings.set("rootPull",Json::number(1.0));params.set("left",float3{-2,1,0});params.set("right",float3{2,1,0});solver=FullBodyIK::compile(body,settings,params);bindPose(body,pose);solved=solver->solve(body,params,pose);check(solved.error<.002f&&near(pose[0].t.y,1),"free pelvis translation satisfies shared goals");
        settings.set("rootPull",Json::number(0.0));settings.member("joints").push(parse(R"({"bone":"bone1","min":[0,0,0],"max":[0,0,0]})"));solver=FullBodyIK::compile(body,settings,params);bindPose(body,pose);solved=solver->solve(body,params,pose);check(near(pose[1].r.z,0)&&std::isfinite(solved.error),"joint rotation limits with unreachable targets");
        params.set("left",float3{-200,50,0});bindPose(body,pose);solved=solver->solve(body,params,pose);check(solved.iterations<=64&&std::isfinite(solved.error),"unreachable targets have bounded work");
        bool invalid=false;try{auto bad=settings;bad.member("effectors").at(0).set("bone",Json::string("missing"));FullBodyIK::compile(body,bad,params);}catch(...){invalid=true;}check(invalid,"full body rejects unknown effector");
        invalid=false;try{auto bad=settings;bad.set("iterations",Json::number(int64_t(1000)));FullBodyIK::compile(body,bad,params);}catch(...){invalid=true;}check(invalid,"full body iteration cap");
        auto rig=settings;rig.set("op",Json::string("fullBodyIK"));Json units=Json::array();units.push(rig);auto program=AnimationRig::compile(body,units,params);bindPose(body,pose);program->execute(body,params,pose,world);check(std::isfinite(pose[3].r.w),"full body rig integration");
    }
    {
        using namespace aiverse::editor::animation;
        Json g=makeGraph();g.member("nodes").push(makeNode("blend","mix","bind"));g.member("nodes").push(makeNode("clip","clip","bind"));
        connect(g,"clip","mix","b");check(g["nodes"][1].getStr("id")=="clip","visual connections topologically sort runtime nodes");
        auto before=g;bool rejected=false;try{connect(g,"mix","mix","a");}catch(...){rejected=true;}check(rejected&&g==before,"cyclic visual edit is atomic");
        rename(g,"clip","walk");check(g["nodes"][2].getStr("b")=="walk","node rename updates references");
        rejected=false;try{remove(g,"walk");}catch(...){rejected=true;}check(rejected,"connected deletion rejected");
        connect(g,"bind","mix","b");remove(g,"walk");check(g["nodes"].size()==2,"disconnect then delete");
        check(makeUnit("fullBodyIK")["effectors"].size()==1,"rig editor full body unit schema");
    }

    {bool rejected=false;try{parseAnimationAsset(std::string(80,'[')+std::string(80,']'));}catch(...){rejected=true;}check(rejected,"asset nesting rejected before parser recursion");check(parseAnimationAsset("{\"note\":\"[[[\" // {{{\n}").isObject(),"asset bound scanner respects strings and comments");}
