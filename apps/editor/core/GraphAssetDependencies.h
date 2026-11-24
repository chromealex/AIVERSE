// Read-only authoring references. No asset loading, execution or writes.
#pragma once
#include "GraphPropertySchema.h"
#include <filesystem>
#include <functional>
#include <map>
#include <set>

namespace aiverse::editor::graph_dependencies {
inline Json resolve(const std::string& family,const Json& source,const std::string& project){
    namespace fs=std::filesystem;
    Json refs=Json::array(),issues=Json::array();
    std::set<std::string> unique;
    std::error_code ec;
    const auto root=project.empty()?fs::path{}:fs::weakly_canonical(project,ec);
    const bool hasRoot=!project.empty()&&!ec;
    auto issue=[&](const char* code,const std::string& subject){
        if(issues.size()>=1024)throw std::length_error("dependency diagnostics exceed limit");
        Json j=Json::object();j.set("code",Json::string(code));j.set("subject",Json::string(subject));issues.push(j);
    };
    auto add=[&](const std::string& owner,const char* kind,const std::string& value,const std::string& state){
        if(value.empty())return;
        Json j=Json::object();j.set("owner",Json::string(owner));j.set("kind",Json::string(kind));j.set("reference",Json::string(value));j.set("status",Json::string(state));
        if(!unique.insert(j.dump(-1)).second)return;
        if(refs.size()>=4096)throw std::length_error("dependency references exceed limit");
        refs.push(j);
    };
    auto asset=[&](const std::string& owner,const char* kind,const std::string& value){
        if(value.empty())return;
        std::string state;
        if(value.rfind("builtin:",0)==0)state="builtin-unverified";
        else if(!graph_properties::projectPath(value))state="invalid-path";
        else if(!hasRoot)state="project-unavailable";
        else{
            std::error_code error;
            const auto path=fs::weakly_canonical(root/value,error);
            const auto relative=path.lexically_relative(root);
            if(error)state="unresolved";
            else if(relative.empty()||relative.is_absolute()||*relative.begin()=="..")state="outside-project";
            else{const bool file=fs::is_regular_file(path,error);state=error?"unresolved":file?"file-present":"file-missing";}
        }
        add(owner,kind,value,state);
    };
    auto animation=[&](const Json& g,const std::string& prefix){
        asset(prefix,"model",g.getStr("model"));
        const auto& defs=g["graphs"];
        std::map<std::string,int> mark;
        size_t visits=0;
        std::function<void(const Json&,const std::string&,size_t)> visit;
        visit=[&](const Json& def,const std::string& owner,size_t depth){
            if(depth>64||++visits>256)throw std::length_error("subgraph traversal limit");
            for(const auto& n:def["nodes"].items()){
                if(n.getStr("kind")!="subgraph")continue;
                const auto id=n.getStr("graph");const auto node=owner+"/"+n.getStr("id");
                const bool found=!id.empty()&&defs[id].isObject();
                add(node,"embedded-animation-graph",id,found?"embedded":"missing");
                if(!found){issue("missing-subgraph",node);continue;}
                if(mark[id]==1){issue("recursive-subgraph",node+" -> "+id);continue;}
                if(mark[id]==2)continue;
                mark[id]=1;visit(defs[id],prefix+"/graphs/"+id,depth+1);mark[id]=2;
            }
        };
        visit(g,prefix,0);
        // Include unused definitions too, but do not claim they are executed.
        for(const auto& [id,def]:defs.members())if(mark[id]==0){
            mark[id]=1;visit(def,prefix+"/graphs/"+id,1);mark[id]=2;
        }
    };
    if(family=="shader"){
        for(const auto& n:source["nodes"].items()){
            const auto op=n.getStr("op");
            if(op=="sampleTexture"||op=="sampleTextureLod")asset(n.getStr("id"),"texture",n.getStr("texture"));
        }
    }else if(family=="motionweave")animation(source,"graph");
    else if(family=="modeling"){
        if(source["graph"].isObject())animation(source["graph"],"document/graph");
        for(const auto& n:source["nodes"].items()){
            const auto id=n.getStr("material");if(id.empty())continue;
            bool found=false;for(const auto& m:source["materials"].items())if(m.getStr("id")==id){found=true;break;}
            add(n.getStr("id"),"local-material",id,found?"embedded":"native-validation-required");
        }
    }else if(family=="texture"){
        auto safe=[](const std::string& s){return !s.empty()&&s.size()<=64&&s.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")==std::string::npos;};
        const auto& links=source["functionLinks"];
        if(!links.isNull()&&(!links.isObject()||links.size()>16))issue("invalid-texture-function-links","functionLinks");
        else for(const auto& [alias,link]:links.members()){
            const auto name=link.getStr("asset"),definition=link.getStr("definition");
            if(!safe(alias)||!safe(name)||!safe(definition)||!source["functions"][alias].isObject()){
                issue("invalid-texture-function-link",alias);continue;
            }
            asset(alias,"texture-function-asset","texturegraphs/"+name+".json");
        }
        for(const auto& n:source["nodes"].items())if(n.getStr("op")=="imageAsset"||n.getStr("op")=="imageAssetChannel")asset(n.getStr("id"),"texture-image-asset",n["properties"].getStr("asset"));
        for(const auto& [name,def]:source["functions"].members())for(const auto& n:def["nodes"].items())if(n.getStr("op")=="imageAsset"||n.getStr("op")=="imageAssetChannel")asset(name+"/"+n.getStr("id"),"texture-image-asset",n["properties"].getStr("asset"));
        const auto& defs=source["functions"];std::map<std::string,int> mark;
        std::function<void(const Json&,const std::string&,size_t)> walk;
        walk=[&](const Json& graph,const std::string& prefix,size_t depth){
            for(const auto& n:graph["nodes"].items())if(n.getStr("op")=="function"){
                const auto fn=n["properties"].getStr("function"),owner=prefix+n.getStr("id");
                const bool found=safe(fn)&&defs[fn].isObject();
                add(owner,"embedded-texture-function",fn,found?"embedded":"missing");
                if(!found){issue("missing-texture-function",owner);continue;}
                if(mark[fn]==1){issue("recursive-texture-function",owner+" -> "+fn);continue;}
                if(depth>=8){issue("texture-function-depth",owner);continue;}
                if(mark[fn]==2)continue;
                mark[fn]=1;walk(defs[fn],fn+"/",depth+1);mark[fn]=2;
            }
        };
        walk(source,"",0);
        for(const auto& [name,def]:defs.members())if(mark[name]==0){mark[name]=1;walk(def,name+"/",1);mark[name]=2;}
    }else if(family=="spectrafx"){
        for(const auto& n:source["nodes"].items()){
            const auto id=n.getStr("id");const auto& p=n["properties"];
            asset(id,"texture",p.getStr("texture"));
            for(const auto& mesh:p["meshes"].items())asset(id,"mesh",mesh.str());
            if(n.getStr("type")=="FunctionCall"){
                const auto fn=p.getStr("function");
                add(id,"embedded-fx-function",fn,source["functions"][fn].isObject()?"embedded":"native-validation-required");
            }
        }
    }
    Json result=Json::object();result.set("references",refs);result.set("issues",issues);
    result.set("scope",Json::string("literal owner-schema references and embedded animation recursion; file existence only; not importer, dynamic parameter or transitive external asset validation"));
    return result;
}
} // namespace aiverse::editor::graph_dependencies
