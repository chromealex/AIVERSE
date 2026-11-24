// Bounded binary-safe glTF repair snapshots. Only the JSON chunk changes;
// every following chunk (header, bytes and padding) is retained verbatim.
#pragma once
#include "engine/data/Json.h"
#include "apps/editor/core/ProjectFileEdits.h"
#include <filesystem>
#include <cstdint>
#include <string>
#include <stdexcept>
#include <limits>

namespace aiverse::studio {
inline constexpr size_t StudioResourceEditBudget=8u*1024u*1024u;
inline bool studioRepairModelExtension(const std::filesystem::path& path){
    return path.extension()==".gltf"||path.extension()==".glb";
}
inline bool studioIsBinaryGlb(const std::string& bytes){
    return bytes.size()>=4&&bytes.compare(0,4,"glTF")==0;
}
inline uint32_t studioGlbU32(const std::string& bytes,size_t at){
    if(at>bytes.size()||bytes.size()-at<4)throw std::runtime_error("Truncated GLB repair header");
    const auto* p=reinterpret_cast<const unsigned char*>(bytes.data()+at);
    return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);
}
inline void studioGlbAppendU32(std::string& bytes,uint32_t value){
    for(unsigned shift=0;shift<32;shift+=8)bytes.push_back(static_cast<char>((value>>shift)&255u));
}
struct StudioModelRepairDocument {
    Json json;
    std::string tail;
    bool binary=false;
};
inline StudioModelRepairDocument studioDecodeModelRepair(const std::filesystem::path& path,const std::string& bytes){
    if(!studioRepairModelExtension(path)||bytes.size()>StudioResourceEditBudget)
        throw std::runtime_error("Unsupported or oversized model repair document");
    StudioModelRepairDocument result;
    result.binary=studioIsBinaryGlb(bytes);
    if(!result.binary){
        result.json=Json::parse(bytes);
    }else{
        if(path.extension()!=".glb"||bytes.size()<20||studioGlbU32(bytes,4)!=2||
           studioGlbU32(bytes,8)!=bytes.size()||studioGlbU32(bytes,16)!=0x4e4f534a)
            throw std::runtime_error("Invalid GLB repair header");
        const size_t length=studioGlbU32(bytes,12);
        if(!length||length%4||length>bytes.size()-20)throw std::runtime_error("Invalid GLB repair JSON chunk");
        result.json=Json::parse(bytes.substr(20,length));
        size_t offset=20+length,chunks=0;
        result.tail=bytes.substr(offset);
        while(offset<bytes.size()){
            if(++chunks>4096||bytes.size()-offset<8)throw std::runtime_error("Invalid GLB repair chunk table");
            const size_t next=studioGlbU32(bytes,offset);
            if(studioGlbU32(bytes,offset+4)==0x4e4f534a||next%4||next>bytes.size()-offset-8)
                throw std::runtime_error("Invalid or duplicate GLB repair chunk");
            offset+=8+next;
        }
    }
    if(!result.json.isObject())throw std::runtime_error("Invalid model repair JSON");
    return result;
}
inline std::string studioEncodeModelRepair(const StudioModelRepairDocument& doc){
    auto text=doc.json.dump(2)+"\n";
    if(!doc.binary)return text;
    while(text.size()%4)text.push_back(' ');
    if(text.size()>StudioResourceEditBudget||doc.tail.size()>StudioResourceEditBudget-text.size()||
       text.size()+doc.tail.size()>StudioResourceEditBudget-20)
        throw std::runtime_error("GLB repair history budget exceeded");
    std::string result="glTF";
    studioGlbAppendU32(result,2);
    studioGlbAppendU32(result,static_cast<uint32_t>(20+text.size()+doc.tail.size()));
    studioGlbAppendU32(result,static_cast<uint32_t>(text.size()));
    studioGlbAppendU32(result,0x4e4f534a);
    result+=text;result+=doc.tail;return result;
}
inline void studioValidateModelRepairBytes(const editor::ProjectFileEdit& edit){
    const auto before=studioDecodeModelRepair(edit.path,edit.before),after=studioDecodeModelRepair(edit.path,edit.after);
    if(before.binary!=after.binary||before.tail!=after.tail)
        throw std::runtime_error("Model repair changed non-JSON chunks");
}
// Hex encoding avoids embedding arbitrary NUL/invalid UTF-8 bytes in JSON.
// Legacy plain textual snapshots remain readable.
inline std::string studioResourceHex(const std::string& bytes){
    if(bytes.size()>StudioResourceEditBudget)throw std::runtime_error("Resource snapshot exceeds budget");
    static constexpr char digits[]="0123456789abcdef";
    std::string result;result.reserve(bytes.size()*2);
    for(unsigned char c:bytes){result.push_back(digits[c>>4]);result.push_back(digits[c&15]);}
    return result;
}
inline std::string studioResourceUnhex(const std::string& text){
    if(text.size()%2||text.size()>StudioResourceEditBudget*2)throw std::runtime_error("Invalid resource snapshot length");
    const auto nibble=[](char c)->unsigned{
        if(c>='0'&&c<='9')return unsigned(c-'0');
        if(c>='a'&&c<='f')return unsigned(c-'a'+10);
        throw std::runtime_error("Invalid resource snapshot hex");
    };
    std::string result;result.reserve(text.size()/2);
    for(size_t i=0;i<text.size();i+=2)result.push_back(static_cast<char>((nibble(text[i])<<4)|nibble(text[i+1])));
    return result;
}
inline Json studioEncodeResourceEdit(const editor::ProjectFileEdit& edit){
    Json row=Json::object();row.set("path",Json::string(edit.path.generic_string()));
    if(studioIsBinaryGlb(edit.before)||studioIsBinaryGlb(edit.after)){
        row.set("encoding",Json::string("hex"));
        row.set("before",Json::string(studioResourceHex(edit.before)));
        row.set("after",Json::string(studioResourceHex(edit.after)));
    }else{
        row.set("before",Json::string(edit.before));row.set("after",Json::string(edit.after));
    }
    return row;
}
inline editor::ProjectFileEdit studioDecodeResourceEdit(const Json& row,bool allowBinary){
    if(!row["path"].isString()||!row["before"].isString()||!row["after"].isString())
        throw std::runtime_error("Invalid resource repair record");
    auto before=row.getStr("before"),after=row.getStr("after");
    if(row.has("encoding")){
        if(!allowBinary||row.getStr("encoding")!="hex")throw std::runtime_error("Invalid resource snapshot encoding");
        before=studioResourceUnhex(before);after=studioResourceUnhex(after);
    }else if(studioIsBinaryGlb(before)||studioIsBinaryGlb(after)){
        throw std::runtime_error("Binary resource snapshot requires encoding");
    }
    if(before.size()>StudioResourceEditBudget||after.size()>StudioResourceEditBudget-before.size())
        throw std::runtime_error("Resource snapshot exceeds budget");
    return {std::filesystem::path(row.getStr("path")),std::move(before),std::move(after)};
}
} // namespace aiverse::studio
