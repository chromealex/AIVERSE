#pragma once
#include "../data/Json.h"
namespace aiverse::render {
// Bound recursion before entering the general JSON parser. Strings/comments do not count.
inline Json parseAnimationAsset(const std::string& text) {
    if(text.size()>256*1024)throw std::runtime_error("animation asset exceeds 256 KiB");
    int depth=0;bool quoted=false,escape=false,line=false,block=false;
    for(size_t i=0;i<text.size();++i){char c=text[i],next=i+1<text.size()?text[i+1]:0;
        if(line){if(c=='\n')line=false;continue;}
        if(block){if(c=='*'&&next=='/'){block=false;++i;}continue;}
        if(quoted){if(escape)escape=false;else if(c=='\\')escape=true;else if(c=='"')quoted=false;continue;}
        if(c=='/'&&next=='/'){line=true;++i;continue;}if(c=='/'&&next=='*'){block=true;++i;continue;}
        if(c=='"'){quoted=true;continue;}
        if((c=='{'||c=='[')&&++depth>64)throw std::runtime_error("animation asset nesting exceeds 64");
        if(c=='}'||c==']')--depth;
    }
    auto graph=Json::parse(text);if(!graph.isObject())throw std::runtime_error("animation asset must be object");return graph;
}
}
