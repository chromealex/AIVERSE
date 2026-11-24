// Exact byte-span edits for supported Wavefront and COLLADA resource slots.
#pragma once
#include "engine/render/Xml.h"
#include <filesystem>
#include <map>
#include <string>
#include <vector>
#include <stdexcept>
#include <algorithm>

namespace aiverse::studio {
struct StudioTextResourceSlot{
    std::string key,value;
    size_t begin=0,end=0;
};
inline bool studioMtlTextureSlot(const std::string& key){
    return key=="map_Kd"||key=="map_Bump"||key=="map_bump"||key=="bump"||key=="norm"||key=="map_Ke";
}
inline std::vector<StudioTextResourceSlot> studioWavefrontSlots(const std::filesystem::path& path,const std::string& bytes){
    std::vector<StudioTextResourceSlot> result;
    bool material=false;size_t line=0,offset=0;
    const bool obj=path.extension()==".obj";
    while(offset<bytes.size()){
        ++line;
        auto end=bytes.find('\n',offset);if(end==std::string::npos)end=bytes.size();
        const auto next=end==bytes.size()?end:end+1;
        const auto first=bytes.find_first_not_of(" \t\r",offset);
        if(first==std::string::npos||first>=end||bytes[first]=='#'){offset=next;continue;}
        auto keyEnd=bytes.find_first_of(" \t\r\n",first);
        if(keyEnd==std::string::npos||keyEnd>end)keyEnd=end;
        const auto key=bytes.substr(first,keyEnd-first);
        if(!obj&&key=="newmtl")material=true;
        if((obj&&key=="mtllib")||(!obj&&material&&studioMtlTextureSlot(key))){
            const auto start=bytes.find_first_not_of(" \t\r",keyEnd);
            const auto last=bytes.find_last_not_of(" \t\r",end-1);
            if(start!=std::string::npos&&start<end&&last!=std::string::npos&&last>=start){
                size_t begin=start;
                if(!obj){
                    const auto split=bytes.find_last_of(" \t",last);
                    if(split!=std::string::npos&&split>=start)begin=split+1;
                }
                result.push_back({std::to_string(line)+"/"+key,bytes.substr(begin,last-begin+1),begin,last+1});
            }
        }
        offset=next;
    }
    return result;
}
inline int studioDaeUtf16Endian(const std::string& bytes){
    if(bytes.size()<2)return 0;
    const auto a=static_cast<unsigned char>(bytes[0]),b=static_cast<unsigned char>(bytes[1]);
    return a==0xff&&b==0xfe?1:a==0xfe&&b==0xff?2:0;
}
inline std::string studioDaeEncodeUtf16(const std::string& utf8,int endian){
    if(!endian)return utf8;
    std::string result;
    const auto append=[&](uint32_t code){
        const auto low=static_cast<char>(code&255),high=static_cast<char>((code>>8)&255);
        if(endian==1){result+=low;result+=high;}else{result+=high;result+=low;}
    };
    append(0xfeff);
    for(size_t i=0;i<utf8.size();){
        const auto c=static_cast<unsigned char>(utf8[i++]);uint32_t cp=c;unsigned more=0;
        if(c>=0xc2&&c<=0xdf){cp=c&31;more=1;}
        else if(c>=0xe0&&c<=0xef){cp=c&15;more=2;}
        else if(c>=0xf0&&c<=0xf4){cp=c&7;more=3;}
        else if(c>=0x80)throw std::runtime_error("Invalid UTF-8 in DAE resource repair");
        const auto length=more;
        while(more--){
            if(i>=utf8.size())throw std::runtime_error("Truncated UTF-8 in DAE resource repair");
            const auto next=static_cast<unsigned char>(utf8[i++]);
            if((next&0xc0)!=0x80)throw std::runtime_error("Invalid UTF-8 continuation");
            cp=(cp<<6)|(next&63);
        }
        if((length==1&&cp<0x80)||(length==2&&cp<0x800)||(length==3&&cp<0x10000)||
           cp>0x10ffff||(cp>=0xd800&&cp<=0xdfff))throw std::runtime_error("Invalid DAE Unicode scalar");
        if(cp<0x10000)append(cp);
        else{cp-=0x10000;append(0xd800+(cp>>10));append(0xdc00+(cp&1023));}
    }
    return result;
}
inline std::string studioDaeRepairText(const std::string& bytes){
    const auto endian=studioDaeUtf16Endian(bytes);
    if(!endian)return bytes;
    auto text=render::xml::toUtf8(bytes);
    if(studioDaeEncodeUtf16(text,endian)!=bytes)throw std::runtime_error("Non-roundtrippable UTF-16 DAE");
    return text;
}
inline std::vector<StudioTextResourceSlot> studioDaeSlots(const std::string& input){
    const auto bytes=studioDaeRepairText(input);
    const auto doc=render::xml::parse(bytes);
    if(doc.name!="COLLADA")throw std::runtime_error("Invalid DAE repair document");
    std::vector<StudioTextResourceSlot> result;size_t index=0;
    doc.each("library_images",[&](const render::xml::Element& library){
        library.each("image",[&](const render::xml::Element& image){
            const auto key="/library_images/image/"+std::to_string(index++)+"/init_from";
            const auto* init=image.child("init_from");if(!init)return;
            const auto* ref=init->child("ref");const auto* leaf=ref?ref:init;
            if(!leaf->children.empty()||leaf->contentEnd<=leaf->contentBegin)return;
            const auto raw=bytes.substr(leaf->contentBegin,leaf->contentEnd-leaf->contentBegin);
            // Do not erase comments, CDATA or processing instructions.
            if(raw.find('<')!=std::string::npos)return;
            const auto start=raw.find_first_not_of(" \t\r\n"),last=raw.find_last_not_of(" \t\r\n");
            if(start==std::string::npos)return;
            result.push_back({key,leaf->text,leaf->contentBegin+start,leaf->contentBegin+last+1});
        });
    });
    return result;
}
inline std::vector<StudioTextResourceSlot> studioTextSlots(const std::filesystem::path& path,const std::string& bytes){
    return path.extension()==".dae"?studioDaeSlots(bytes):studioWavefrontSlots(path,bytes);
}
inline std::string studioPatchTextSlots(std::string bytes,const std::vector<StudioTextResourceSlot>& slots,
    const std::map<std::string,std::string>& replacements){
    size_t applied=0,last=bytes.size();
    auto ordered=slots;
    std::sort(ordered.begin(),ordered.end(),[](const auto& a,const auto& b){return a.begin>b.begin;});
    for(const auto& slot:ordered){
        const auto it=replacements.find(slot.key);if(it==replacements.end())continue;
        if(slot.begin>=slot.end||slot.end>last)throw std::runtime_error("Overlapping resource text spans");
        bytes.replace(slot.begin,slot.end-slot.begin,it->second);
        last=slot.begin;++applied;
    }
    if(applied!=replacements.size())throw std::runtime_error("Resource text slot changed or unsupported");
    return bytes;
}
inline std::string studioPatchResourceText(const std::filesystem::path& path,const std::string& bytes,
    const std::vector<StudioTextResourceSlot>& slots,const std::map<std::string,std::string>& replacements){
    if(path.extension()!=".dae")return studioPatchTextSlots(bytes,slots,replacements);
    auto result=studioPatchTextSlots(studioDaeRepairText(bytes),slots,replacements);
    return studioDaeEncodeUtf16(result,studioDaeUtf16Endian(bytes));
}
inline std::string studioXmlResourceEscape(const std::string& value){
    std::string result;
    for(char c:value){
        if(c=='&')result+="&amp;";else if(c=='<')result+="&lt;";else if(c=='>')result+="&gt;";else result+=c;
    }
    return result;
}
} // namespace aiverse::studio
