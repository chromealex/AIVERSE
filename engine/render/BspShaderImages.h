// Static base-image selection from Quake III shader scripts. Multi-stage blending stays in the source material.
#pragma once
#include "ModelInterchange.h"
#include <set>
#include <sstream>

namespace aiverse::render {
inline bool bspAssetName(const std::string& name) {
    return !name.empty() && name.size()<256 && name.front()!='/' && name.find("..")==name.npos &&
           name.find_first_of(":\\")==name.npos;
}

class BspShaderImages {
public:
    BspShaderImages(const FileReader& read,std::string root):read_(read),root_(std::move(root)) {
        std::string listing;
        if(read_(root_+"scripts/shaderlist.txt",listing) && listing.size()<=65536) {
            std::istringstream lines(listing);
            for(std::string line;std::getline(lines,line);) {
                line.erase(line.find("//")==line.npos?line.size():line.find("//"));
                std::istringstream entries(line);
                for(std::string name;entries>>name;)load(name);
            }
        }
    }
    std::string baseImage(const std::string& shader) {
        if(!bspAssetName(shader))return {};
        const auto slash=shader.find('/');
        const auto next=shader.find('/',slash==shader.npos?0:slash+1);
        if(slash!=shader.npos && next!=shader.npos)load(shader.substr(slash+1,next-slash-1));
        const auto it=images_.find(interchange::lowerStr(shader));
        return it==images_.end()?shader:it->second;
    }
private:
    void load(const std::string& name) {
        if(!bspAssetName(name) || name.find('/')!=name.npos || seen_.size()>=512 || !seen_.insert(name).second)return;
        std::string source;
        if(!read_(root_+"scripts/"+name+".shader",source) || source.size()>2*1024*1024 || source.size()>remaining_)return;
        remaining_-=source.size();
        std::istringstream lines(source);
        std::string shader,editorImage,base,command;
        int depth=0;
        for(std::string line;std::getline(lines,line);) {
            const auto comment=line.find("//");if(comment!=line.npos)line.resize(comment);
            std::string separated;
            for(char c:line) {
                if(c=='{'||c=='}'){separated.push_back(' ');separated.push_back(c);separated.push_back(' ');}
                else separated.push_back(c);
            }
            std::istringstream words(separated);
            for(std::string word;words>>word;) {
                if(word=="{"){++depth;command.clear();continue;}
                if(word=="}"){
                    if(depth==1 && !shader.empty()) {
                        if(!base.empty()||!editorImage.empty())images_.try_emplace(shader,base.empty()?editorImage:base);
                        base.clear();editorImage.clear();shader.clear();
                    }
                    depth=std::max(0,depth-1);command.clear();continue;
                }
                if(!depth){shader=interchange::lowerStr(word);continue;}
                if(!command.empty()) {
                    if(word.front()!='$' && word.front()!='*' && bspAssetName(word)) {
                        if(command=="qer_editorimage")editorImage=word;
                        else if(base.empty())base=word;
                    }
                    command.clear();continue;
                }
                const auto key=interchange::lowerStr(word);
                if((depth==1 && key=="qer_editorimage") || (depth==2 && (key=="map"||key=="clampmap")))command=key;
            }
            command.clear();
        }
    }
    const FileReader& read_;
    std::string root_;
    size_t remaining_=16*1024*1024;
    std::set<std::string> seen_;
    std::map<std::string,std::string> images_;
};
}
