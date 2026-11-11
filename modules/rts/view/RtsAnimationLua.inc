// Included in RtsViewScript.cpp's binding namespace: same sandbox and error discipline.
// Lua graph assets are data. Native callbacks retain a revocable VM token across hot reload.
bool animationJson(lua_State* L,int index,Json& out,int depth,size_t& budget) {
  if (depth > 24 || budget == 0 || !lua_checkstack(L, 8))
    return false;
  --budget;
  index = lua_absindex(L, index);
  switch (lua_type(L, index)) {
  case LUA_TNIL:
    out = Json{};
    return true;
  case LUA_TBOOLEAN:
    out = Json::fromBool(lua_toboolean(L, index));
    return true;
  case LUA_TNUMBER: {
    double n = toDouble(L, index);
    if (!std::isfinite(n) || std::abs(n) > 1000000)
      return false;
    out = Json::number(n, 6);
    return true;}
    case LUA_TSTRING:{size_t len=0;const char* s=lua_tolstring(L,index,&len);if(len>4096)return false;out=Json::string(std::string(s,len));return true;}
    case LUA_TTABLE:{
        size_t n=lua_rawlen(L,index);if(n>1024)return false;out=n?Json::array():Json::object();size_t count=0;
        lua_pushnil(L);while(lua_next(L,index)){
            Json value;if(!animationJson(L,-1,value,depth+1,budget)){lua_pop(L,2);return false;}
            if(n){if(!lua_isinteger(L,-2)||lua_tointeger(L,-2)<1||lua_tointeger(L,-2)>lua_Integer(n)){lua_pop(L,2);return false;}}
            else{if(lua_type(L,-2)!=LUA_TSTRING){lua_pop(L,2);return false;}out.set(lua_tostring(L,-2),std::move(value));}
            ++count;lua_pop(L,1);
        }
        if(n){if(count!=n)return false;for(size_t i=1;i<=n;++i){lua_rawgeti(L,index,lua_Integer(i));Json v;bool ok=animationJson(L,-1,v,depth+1,budget);lua_pop(L,1);if(!ok)return false;out.push(std::move(v));}}
        return true;
    }
    default:return false;
    }
}
void animationPushJson(lua_State* L,const Json& j){
    if(j.isNumber()){lua_pushnumber(L,j.toDouble());return;}
    if(j.isArray()){lua_createtable(L,int(j.size()),0);for(size_t i=0;i<j.size();++i){animationPushJson(L,j[i]);lua_rawseti(L,-2,lua_Integer(i+1));}return;}
    if(j.isObject()){lua_newtable(L);for(const auto& [name,v]:j.members()){animationPushJson(L,v);lua_setfield(L,-2,name.c_str());}return;}
    pushJson(L,j);
}
void animationPushVector(lua_State* L,const float* values,int size){lua_createtable(L,size,0);for(int i=0;i<size;++i){lua_pushnumber(L,values[i]);lua_rawseti(L,-2,i+1);}}
bool animationReadVector(lua_State* L,int table,const char* key,float* values,int size){
    lua_pushstring(L,key);lua_rawget(L,table);bool ok=lua_istable(L,-1)&&lua_rawlen(L,-1)==size_t(size);
    if (ok)
      for (int i = 0; i < size; ++i) {
        lua_rawgeti(L, -1, i + 1);
        double n = toDouble(L, -1);
        ok = ok && lua_isnumber(L, -1) && std::isfinite(n) && std::abs(n) <= 1000000;
        values[i] = float(n);
        lua_pop(L, 1);
      }
    lua_pop(L, 1);
    return ok;
}
bool animationCall(const std::shared_ptr<AnimationLuaContext>& context,int ref,const Json& config,
                   const render::AnimationParameters& params,float dt,std::span<float> mutableState,
                   std::span<const float> state,render::Pose* pose){
    if(!context->vm||!context->vm->L)return false;
    auto& vm=*context->vm;auto* L=vm.L;const int top=lua_gettop(L);
    if(!lua_checkstack(L,256))return false;
    if(pose&&pose->size()>512){if(vm.log)vm.log(true,"animation Lua trait: pose exceeds 512 bones");return false;}
    lua_rawgeti(L,LUA_REGISTRYINDEX,ref);lua_newtable(L);
    for(const auto& [name,v]:params.values){lua_pushnumber(L,v);lua_setfield(L,-2,name.c_str());}
    for(const auto& [name,v]:params.vectors){float xyz[]={v.x,v.y,v.z};animationPushVector(L,xyz,3);lua_setfield(L,-2,name.c_str());}
    animationPushVector(L,state.data(),int(state.size()));
    if(pose){
        lua_createtable(L,int(pose->size()),0);
        for(size_t i=0;i<pose->size();++i){const auto& p=(*pose)[i];lua_newtable(L);
            float t[]={p.t.x,p.t.y,p.t.z},r[]={p.r.x,p.r.y,p.r.z,p.r.w},s[]={p.s.x,p.s.y,p.s.z};
            animationPushVector(L,t,3);lua_setfield(L,-2,"t");animationPushVector(L,r,4);lua_setfield(L,-2,"r");animationPushVector(L,s,3);lua_setfield(L,-2,"s");
            lua_rawseti(L,-2,lua_Integer(i+1));
        }
    }else lua_pushnumber(L,dt);
    animationPushJson(L,config);
    auto savedBudget=vm.budget,savedUsed=vm.used;bool savedExceeded=vm.exceeded;
    vm.budget=100000;vm.used=0;vm.exceeded=false;std::string error;
    context->executing=true;
    bool ok=protectedCall(vm,4,1,&error);
    context->executing=false;
    vm.budget=savedBudget;vm.used=savedUsed;vm.exceeded=savedExceeded;
    if(ok&&pose){
        ok=lua_istable(L,-1)&&lua_rawlen(L,-1)==pose->size();render::Pose result=*pose;
        if(ok)for(size_t i=0;i<result.size();++i){lua_rawgeti(L,-1,lua_Integer(i+1));int table=lua_absindex(L,-1);float t[3]{},r[4]{},s[3]{};
            bool valid=lua_istable(L,table)&&animationReadVector(L,table,"t",t,3)&&animationReadVector(L,table,"r",r,4)&&animationReadVector(L,table,"s",s,3);
            lua_pop(L,1);if(!valid){ok=false;break;}auto& p=result[i];p.t={t[0],t[1],t[2]};p.r=render::qnormalize({r[0],r[1],r[2],r[3]});p.s={s[0],s[1],s[2]};
        }
        if(ok)*pose=std::move(result);
    }else if(ok){
        ok=lua_istable(L,-1)&&lua_rawlen(L,-1)==mutableState.size();std::vector<float> result(mutableState.size());
        if(ok)for(size_t i=0;i<result.size();++i){lua_rawgeti(L,-1,lua_Integer(i+1));double n=toDouble(L,-1);ok=ok&&lua_isnumber(L,-1)&&std::isfinite(n)&&std::abs(n)<=1000000;result[i]=float(n);lua_pop(L,1);}
        if(ok)std::copy(result.begin(),result.end(),mutableState.begin());
    }
    lua_settop(L,top);
    if(!ok&&vm.log)vm.log(true,"animation Lua trait disabled: "+(error.empty()?std::string("invalid return value"):error));
    return ok;
}
int vAnimationTrait(lua_State* L,Vm& vm){
    if(viewOf(vm).animationContext->executing)return fail(vm,"animation controls cannot be changed inside a trait");
    auto& im=viewOf(vm);if(lua_type(L,1)!=LUA_TSTRING||!lua_istable(L,2))return fail(vm,"animation_trait(name, callbacks) expected");
    std::string name=lua_tostring(L,1);if(name.empty()||name.size()>128||im.animationContext->registrations>=64)return fail(vm,"animation trait registration limit");
    auto trait=std::make_shared<render::AnimationTrait>();lua_pushliteral(L,"stateSize");lua_rawget(L,2);lua_Integer count=lua_isnil(L,-1)?0:lua_tointeger(L,-1);bool valid=lua_isnil(L,-1)||lua_isinteger(L,-1);lua_pop(L,1);
    if (!valid || count < 0 || count > 256)
      return fail(vm, "trait stateSize must be 0..256");
    trait->stateFloats = size_t(count);
    const char* names[]={"pre_update","post_update","pre_evaluate","post_evaluate"};
    for(auto name:names){lua_pushstring(L,name);lua_rawget(L,2);bool ok=lua_isnil(L,-1)||lua_isfunction(L,-1);lua_pop(L,1);if(!ok)return fail(vm,"trait hook must be function");}
    auto context=im.animationContext;
    for(int i=0;i<4;++i){lua_pushstring(L,names[i]);lua_rawget(L,2);if(lua_isnil(L,-1)){lua_pop(L,1);continue;}int ref=luaL_ref(L,LUA_REGISTRYINDEX);
        if(i<2){auto hook=[context,ref](const Json& c,render::AnimationParameters& p,float dt,std::span<float> state){return animationCall(context,ref,c,p,dt,state,state,nullptr);};if(i==0)trait->preUpdate=hook;else trait->postUpdate=hook;}
        else{auto hook=[context,ref](const Json& c,const render::AnimationParameters& p,std::span<const float> state,render::Pose& pose){return animationCall(context,ref,c,p,0,{},state,&pose);};if(i==2)trait->preEvaluate=hook;else trait->postEvaluate=hook;}
    }
    ++context->registrations;im.fx->animationTraits[name]=std::move(trait);return 0;
}
int vAnimationDefine(lua_State* L,Vm& vm){
    if(viewOf(vm).animationContext->executing)return fail(vm,"animation controls cannot be changed inside a trait");
    auto& library=viewOf(vm).fx->animationDefinitions;
    if(lua_type(L,1)!=LUA_TSTRING||!lua_istable(L,2))return fail(vm,"animation_define(name, graph) expected");
    std::string name=lua_tostring(L,1);if(name.empty()||name.size()>128||(!library.count(name)&&library.size()>=64))return fail(vm,"animation graph limit");
    Json definition;size_t budget=16384;if(!animationJson(L,2,definition,0,budget)||!definition.isObject())return fail(vm,"invalid graph data or graph limit");
    auto asset=std::make_shared<AnimationDefinition>();asset->definition=std::move(definition);asset->traits=viewOf(vm).fx->animationTraits;
    library[name]=std::move(asset);return 0;
}
int vAnimationLoad(lua_State* L,Vm& vm){
    if(viewOf(vm).animationContext->executing)return fail(vm,"animation controls cannot be changed inside a trait");
    if(lua_type(L,1)!=LUA_TSTRING||lua_type(L,2)!=LUA_TSTRING)return fail(vm,"animation_load(name, asset) expected");
    std::string name=lua_tostring(L,1),assetName=lua_tostring(L,2);
    if(name.empty()||name.size()>128||assetName.empty()||assetName.size()>64||assetName.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")!=std::string::npos)return fail(vm,"invalid animation asset name");
    auto& im=viewOf(vm);auto& library=im.fx->animationDefinitions;
    if(!library.count(name)&&library.size()>=64)return fail(vm,"animation graph limit");
    std::string text;auto reader=im.animationRead?im.animationRead:platform::readContentFile;if(!reader("animations/"+assetName+".json",text)||text.size()>256*1024)return fail(vm,"cannot read animation asset or asset too large");
    auto asset=std::make_shared<AnimationDefinition>();try{asset->definition=render::parseAnimationAsset(text);}catch(const std::exception& e){return fail(vm,e.what());}if(!asset->definition.isObject())return fail(vm,"animation asset must be object");asset->traits=im.fx->animationTraits;library[name]=std::move(asset);return 0;
}
int vAnimationUse(lua_State* L,Vm& vm){
    if(viewOf(vm).animationContext->executing)return fail(vm,"animation controls cannot be changed inside a trait");
    Entity entity;if(!argEntity(L,1,entity)||!vm.sim||!vm.sim->world().alive(entity))return fail(vm,"animation_use: live entity expected");
    auto& store=*viewOf(vm).fx;auto key=entity.packed();
    if(lua_isnil(L,2)){auto it=store.animations.find(key);if(it!=store.animations.end())it->second.graph.reset();return 0;}
    if(lua_type(L,2)!=LUA_TSTRING)return fail(vm,"animation_use: graph name expected");
    auto it=store.animationDefinitions.find(lua_tostring(L,2));if(it==store.animationDefinitions.end())return fail(vm,"animation_use: unknown graph");
    if(!store.animations.count(key)&&store.animations.size()>=65536)return fail(vm,"animation entity limit");
    store.animations[key].graph=it->second;store.animations[key].error.clear();return 0;
}
int vAnimationStatus(lua_State* L,Vm& vm){
    Entity entity;if(!argEntity(L,1,entity))return fail(vm,"animation_status: entity expected");auto& entries=viewOf(vm).fx->animations;auto it=entries.find(entity.packed());
    if(it==entries.end()){lua_pushnil(L);return 1;}lua_newtable(L);lua_pushstring(L,it->second.state.c_str());lua_setfield(L,-2,"state");lua_pushstring(L,it->second.error.c_str());lua_setfield(L,-2,"error");
    const auto& r=it->second.contacts;lua_pushboolean(L,r.supported);lua_setfield(L,-2,"supported");lua_pushboolean(L,r.stable);lua_setfield(L,-2,"stable");
    lua_pushinteger(L,r.supports);lua_setfield(L,-2,"supports");lua_pushinteger(L,r.collisions);lua_setfield(L,-2,"collisions");
    lua_pushnumber(L,r.penetration);lua_setfield(L,-2,"penetration");lua_pushnumber(L,r.balanceError);lua_setfield(L,-2,"balanceError");lua_pushnumber(L,r.supportError);lua_setfield(L,-2,"supportError");return 1;
}
