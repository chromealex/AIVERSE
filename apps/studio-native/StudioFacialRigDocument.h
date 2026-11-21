// Studio HTML5 facial Control Rig v1 project document authoring.
// This is project-file validation, NOT a native Model/Graph compile or GPU preview.
// Shares Stage 180 MotionWeave's version/morphs/rig JSON shape without
// importing an unmerged feature branch into Studio.
#pragma once
#include "engine/data/Json.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <fcntl.h>
#include <unistd.h>
#include <iterator>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
#include <vector>

namespace aiverse::studio {

inline bool validStudioFacialRigName(std::string_view name) {
    if(name.empty() || name.size()>64)return false;
    for(const char c:name)
        if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||
             (c>='0'&&c<='9')||c=='_'||c=='-'))return false;
    return true;
}
inline bool validStudioFacialRigLabel(std::string_view text) {
    if(text.empty() || text.size()>128)return false;
    for(unsigned char c:text)if(c<32 || c==127)return false;
    return true;
}

inline bool validStudioFacialRigDraft(const Json& doc,std::string* error=nullptr) {
    auto reject=[&](const char* problem) {
        if(error)*error=problem;
        return false;
    };
    if(!doc.isObject() || !doc["version"].isNumber() ||
       doc["version"].numberLiteral()!="1")
        return reject("Facial rig requires version 1");
    const Json& morphs=doc["morphs"];
    const Json& rig=doc["rig"];
    if(!morphs.isArray() || morphs.size()>256 ||
       (!rig.isNull() && (!rig.isArray() || rig.size()>64)))
        return reject("Invalid facial morph/IK array or size limit");
    if(morphs.size()==0 && (rig.isNull() || rig.size()==0))
        return reject("Facial rig must contain morph mappings or IK");
    std::set<std::pair<std::string,std::string>> destinations;
    for(const Json& m:morphs.items()) {
        if(!m.isObject() || !m["parameter"].isString() ||
           !m["node"].isString() || !m["target"].isString())
            return reject("Morph mapping needs parameter, node and target");
        const auto parameter=m["parameter"].str();
        const auto node=m["node"].str();
        const auto target=m["target"].str();
        if(!validStudioFacialRigLabel(parameter) ||
           !validStudioFacialRigLabel(node) ||
           !validStudioFacialRigLabel(target))
            return reject("Invalid facial parameter, bone or morph name");
        if(!destinations.emplace(node,target).second)
            return reject("Duplicate facial morph destination");
        if(m.has("blend")) {
            if(!m["blend"].isString())return reject("Morph blend must be text");
            const auto value=m["blend"].str();
            if(value!="override" && value!="additive")
                return reject("Unknown facial morph blend");
        }
        static constexpr std::array<std::tuple<const char*,double,double>,3>
            ranges{{{"gain",0.,4.},{"bias",-1.,1.},{"halfLife",0.,10.}}};
        for(const auto& [name,min,max]:ranges) {
            if(!m.has(name))continue;
            const Json& value=m[name];
            if(!value.isNumber() || !std::isfinite(value.toDouble()) ||
               value.toDouble()<min || value.toDouble()>max)
                return reject("Invalid facial morph numeric control");
        }
    }
    if(rig.isArray()) {
        static const std::set<std::string> ops={
            "aim","angularLimit","limit","transform","copy",
            "twoBoneIK","fullBodyIK","surfaceLocomotion"};
        for(const Json& unit:rig.items()) {
            if(!unit.isObject() || !unit["bone"].isString() ||
               !validStudioFacialRigLabel(unit["bone"].str()) ||
               !unit["op"].isString() || !ops.contains(unit["op"].str()))
                return reject("Invalid facial IK bone or operation");
            const std::string op=unit["op"].str();
            if(op=="aim" || op=="twoBoneIK") {
                if(!unit["target"].isString() ||
                   !validStudioFacialRigLabel(unit["target"].str()))
                    return reject("Facial IK target parameter is required");
            }
            if(op=="twoBoneIK") {
                for(const char* key:{"knee","end","pole"})
                    if(!unit[key].isString() ||
                       !validStudioFacialRigLabel(unit[key].str()))
                        return reject("Two-bone IK requires knee/end/pole");
            }
            if(op=="copy" &&
               (!unit["source"].isString() ||
                !validStudioFacialRigLabel(unit["source"].str())))
                return reject("Copy IK requires a source bone");
            if(unit.has("weight") &&
               (!unit["weight"].isNumber() ||
                !std::isfinite(unit["weight"].toDouble()) ||
                unit["weight"].toDouble()<0 ||
                unit["weight"].toDouble()>1))
                return reject("Invalid IK blend weight");
            if(unit.has("weightParameter") &&
               (!unit["weightParameter"].isString() ||
                !validStudioFacialRigLabel(unit["weightParameter"].str())))
                return reject("Invalid IK weight parameter");
            for(const char* vectorKey:{"axis","min","max","translation","rotation","scale"}) {
                if(!unit.has(vectorKey))continue;
                const Json& values=unit[vectorKey];
                if(!values.isArray() || values.size()!=3)
                    return reject("IK vector must contain 3 numbers");
                for(const Json& value:values.items())
                    if(!value.isNumber() ||
                       !std::isfinite(value.toDouble()) ||
                       std::abs(value.toDouble())>1000000.)
                        return reject("Nonfinite/out-of-range IK vector");
            }
        }
    }
    if(doc.dump(-1).size()>128u*1024u)
        return reject("Facial rig exceeds 128 KiB");
    if(error)error->clear();
    return true;
}

inline std::filesystem::path studioFacialRigFolder(
        const std::string& projectRoot,bool create=false) {
    namespace fs=std::filesystem;
    if(projectRoot.empty())throw std::runtime_error("Open a project first");
    const fs::path root=fs::weakly_canonical(fs::path(projectRoot));
    if(!fs::is_directory(root))throw std::runtime_error("Invalid project root");
    const fs::path folder=root/"facial-rigs";
    std::error_code ec;
    const auto status=fs::symlink_status(folder,ec);
    if(!ec && fs::is_symlink(status))
        throw std::runtime_error("Facial rig folder cannot be a symlink");
    if(!fs::exists(folder)) {
        if(!create)return folder;
        if(!fs::create_directory(folder))
            throw std::runtime_error("Cannot create facial-rigs directory");
    }
    if(!fs::is_directory(folder) || fs::is_symlink(fs::symlink_status(folder)))
        throw std::runtime_error("Facial rig folder is not a real directory");
    return folder;
}

inline std::filesystem::path studioFacialRigFile(
        const std::string& projectRoot,const std::string& name,
        bool createFolder=false) {
    namespace fs=std::filesystem;
    if(!validStudioFacialRigName(name))
        throw std::invalid_argument("Invalid facial rig filename");
    const fs::path path=studioFacialRigFolder(projectRoot,createFolder)/
                        (name+".json");
    const auto state=fs::symlink_status(path);
    if(fs::is_symlink(state))
        throw std::runtime_error("Facial rig file cannot be a symlink");
    if(fs::exists(path) && !fs::is_regular_file(path))
        throw std::runtime_error("Facial rig entry must be a regular file");
    return path;
}

inline Json listStudioFacialRigs(const std::string& projectRoot) {
    namespace fs=std::filesystem;
    Json names=Json::array();
    const fs::path folder=studioFacialRigFolder(projectRoot);
    if(!fs::exists(folder)) {
        Json result=Json::object();
        result.set("rigs",std::move(names));
        return result;
    }
    std::vector<std::string> sorted;
    for(const auto& entry:fs::directory_iterator(folder)) {
        if(!entry.is_regular_file() || entry.is_symlink() ||
           entry.path().extension()!=".json")continue;
        const auto name=entry.path().stem().string();
        if(validStudioFacialRigName(name))sorted.push_back(name);
        if(sorted.size()>128)throw std::runtime_error("Too many facial rig files");
    }
    std::sort(sorted.begin(),sorted.end());
    for(const auto& name:sorted)names.push(Json::string(name));
    Json result=Json::object();
    result.set("rigs",std::move(names));
    return result;
}

inline Json readStudioFacialRig(const std::string& root,const std::string& name) {
    namespace fs=std::filesystem;
    const fs::path file=studioFacialRigFile(root,name);
    if(!fs::exists(file) || fs::file_size(file)>128u*1024u)
        throw std::runtime_error("Facial rig file unavailable or too large");
    std::ifstream input(file,std::ios::binary);
    if(!input)throw std::runtime_error("Cannot read facial rig file");
    std::string bytes(std::istreambuf_iterator<char>{input},
                      std::istreambuf_iterator<char>{});
    const Json doc=Json::parse(bytes);
    std::string error;
    if(!validStudioFacialRigDraft(doc,&error))
        throw std::runtime_error("Invalid saved facial rig: "+error);
    Json result=Json::object();
    result.set("name",Json::string(name));
    result.set("rig",doc);
    return result;
}

inline Json saveStudioFacialRig(const std::string& root,
                                const std::string& name,const Json& doc) {
    namespace fs=std::filesystem;
    std::string error;
    if(!validStudioFacialRigDraft(doc,&error))
        throw std::invalid_argument(error);
    const fs::path file=studioFacialRigFile(root,name,true);
    // Descriptor-relative exclusive staging prevents path traversal,
    // following a substituted pending symlink, or truncating the live
    // preset before a complete payload has been written.
    const std::string filename=name+".json";
    const std::string pending=filename+".pending";
    const int dir=::open(file.parent_path().c_str(),
                         O_RDONLY|O_DIRECTORY|O_NOFOLLOW);
    if(dir<0)throw std::runtime_error("Cannot open facial rig project folder");
    int fd=-1;
    try {
        fd=::openat(dir,pending.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW,0600);
        if(fd<0)throw std::runtime_error("Cannot create exclusive facial rig staging file");
        const std::string bytes=doc.dump(2)+"\n";
        std::size_t offset=0;
        while(offset<bytes.size()) {
            const ssize_t n=::write(fd,bytes.data()+offset,bytes.size()-offset);
            if(n<=0)throw std::runtime_error("Failed writing facial rig staging file");
            offset+=std::size_t(n);
        }
        if(::fsync(fd)!=0)throw std::runtime_error("Facial rig staging fsync failed");
        if(::close(fd)!=0) {
            fd=-1;
            throw std::runtime_error("Failed closing facial rig staging file");
        }
        fd=-1;
        if(::renameat(dir,pending.c_str(),dir,filename.c_str())!=0)
            throw std::runtime_error("Cannot publish facial rig file");
        (void)::fsync(dir);
        (void)::close(dir);
    }catch(...) {
        if(fd>=0)(void)::close(fd);
        (void)::unlinkat(dir,pending.c_str(),0);
        (void)::close(dir);
        throw;
    }
    Json result=Json::object();
    result.set("name",Json::string(name));
    result.set("rig",doc);
    return result;
}

} // namespace aiverse::studio
