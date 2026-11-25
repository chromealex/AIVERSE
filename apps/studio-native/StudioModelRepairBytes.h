// Bounded binary-safe glTF repair snapshots. Only the JSON chunk changes;
// every following chunk (header, bytes and padding) is retained verbatim.
#pragma once
#include "engine/data/Json.h"
#include "engine/pack/Sha256.h"
#include <fstream>
#include <functional>
#include <algorithm>
#include "engine/render/Xml.h"
#include "apps/editor/core/ProjectFileEdits.h"
#include <filesystem>
#include <cstdint>
#include <string>
#include <stdexcept>
#include <limits>
#include "engine/view/MaterialLibraryFiles.h"

namespace aiverse::studio {
inline constexpr size_t StudioResourceEditBudget=8u*1024u*1024u;
inline bool studioRepairModelExtension(const std::filesystem::path& path){
    return view::materialAssetPath(path.generic_string())||path.extension()==".gltf"||path.extension()==".glb"||path.extension()==".obj"||path.extension()==".mtl"||path.extension()==".dae";
}
inline bool studioRepairGltfExtension(const std::filesystem::path& path){
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
    if(!studioRepairGltfExtension(path)||bytes.size()>StudioResourceEditBudget)
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
// Compact prefix snapshots: exact 20-byte header + padded JSON; the
// unchanged suffix is identified by SHA-256 and length, never retained in RAM.
inline bool studioStreamingResource(const editor::ProjectFileEdit& edit){return !edit.tailDigest.empty();}
inline Json studioDecodeGlbPrefix(const std::string& prefix,uint64_t tailBytes){
    if(prefix.size()<20||prefix.size()>StudioResourceEditBudget||!studioIsBinaryGlb(prefix)||
       studioGlbU32(prefix,4)!=2||studioGlbU32(prefix,16)!=0x4e4f534a||
       studioGlbU32(prefix,12)!=prefix.size()-20||prefix.size()==20||(prefix.size()-20)%4||
       tailBytes>0xffffffffull-prefix.size()||studioGlbU32(prefix,8)!=prefix.size()+tailBytes)
        throw std::runtime_error("Invalid compact GLB prefix");
    const auto doc=Json::parse(prefix.substr(20));
    if(!doc.isObject())throw std::runtime_error("Invalid compact GLB JSON");
    return doc;
}
inline std::string studioEncodeGlbPrefix(const Json& doc,uint64_t tailBytes){
    auto json=doc.dump(2)+"\n";while(json.size()%4)json+=' ';
    if(json.size()>StudioResourceEditBudget-20||tailBytes>0xffffffffull-20-json.size())
        throw std::runtime_error("GLB prefix exceeds format or history budget");
    std::string out="glTF";studioGlbAppendU32(out,2);
    studioGlbAppendU32(out,static_cast<uint32_t>(20+json.size()+tailBytes));
    studioGlbAppendU32(out,static_cast<uint32_t>(json.size()));studioGlbAppendU32(out,0x4e4f534a);
    out+=json;return out;
}
struct StudioGlbPrefixSnapshot{std::string prefix,digest;uint64_t tailBytes=0;};
inline StudioGlbPrefixSnapshot studioReadGlbPrefix(const std::filesystem::path& path,
    const std::function<void(const char*,size_t)>& consume={}){
    namespace fs=std::filesystem;
    if(!fs::is_regular_file(fs::symlink_status(path)))throw std::runtime_error("Unsafe GLB stream source");
    const auto size=fs::file_size(path);const auto stamp=fs::last_write_time(path);
    if(size<20||size>0xffffffffull)throw std::runtime_error("Invalid GLB stream length");
    std::ifstream input(path,std::ios::binary);
    const auto read=[&](char* dst,size_t n){
        input.read(dst,static_cast<std::streamsize>(n));
        if(input.gcount()!=static_cast<std::streamsize>(n))throw std::runtime_error("Truncated GLB stream");
    };
    StudioGlbPrefixSnapshot out;out.prefix.resize(20);read(out.prefix.data(),20);
    const auto json=studioGlbU32(out.prefix,12);
    if(json>StudioResourceEditBudget/2-20||uint64_t(json)>size-20)
        throw std::runtime_error("GLB JSON prefix exceeds history budget");
    out.prefix.resize(20+json);read(out.prefix.data()+20,json);
    out.tailBytes=size-out.prefix.size();(void)studioDecodeGlbPrefix(out.prefix,out.tailBytes);
    pack::Sha256 hash;char buffer[65536];uint64_t remaining=out.tailBytes;size_t chunks=0;
    const auto emit=[&](const char* data,size_t count){hash.update(data,count);if(consume)consume(data,count);};
    while(remaining){
        if(++chunks>4096||remaining<8)throw std::runtime_error("Invalid GLB stream chunk table");
        std::string header(8,'\0');read(header.data(),8);remaining-=8;
        uint64_t count=studioGlbU32(header,0);
        if(studioGlbU32(header,4)==0x4e4f534a||count%4||count>remaining)
            throw std::runtime_error("Invalid or duplicate GLB stream chunk");
        emit(header.data(),8);remaining-=count;
        while(count){const auto n=static_cast<size_t>(std::min<uint64_t>(count,sizeof(buffer)));read(buffer,n);emit(buffer,n);count-=n;}
    }
    if(input.peek()!=std::char_traits<char>::eof()||input.bad()||fs::file_size(path)!=size||fs::last_write_time(path)!=stamp)
        throw std::runtime_error("GLB changed while streaming");
    const auto digest=hash.finish();out.digest=pack::toHex(digest.data(),digest.size());return out;
}
inline bool studioGlbMagicFile(const std::filesystem::path& path){
    std::ifstream input(path,std::ios::binary);char magic[4]{};
    input.read(magic,4);return input.gcount()==4&&std::string(magic,4)=="glTF";
}
inline bool studioResourceMatches(const std::filesystem::path& path,const editor::ProjectFileEdit& edit,bool after=false){
    const auto& expected=after?edit.after:edit.before;
    if(!studioStreamingResource(edit)){
        return std::filesystem::is_regular_file(std::filesystem::symlink_status(path))&&
            std::filesystem::file_size(path)==expected.size()&&editor::readProjectFileBytes(path)==expected;
    }
    const auto actual=studioReadGlbPrefix(path);
    return actual.prefix==expected&&actual.tailBytes==edit.tailBytes&&actual.digest==edit.tailDigest;
}
inline uint64_t studioResourceProjectedSize(const editor::ProjectFileEdit& edit){return edit.after.size()+edit.tailBytes;}
inline void studioStreamProjectedResource(const std::filesystem::path& path,const editor::ProjectFileEdit& edit,
    const std::function<void(const char*,size_t)>& consume){
    if(!studioStreamingResource(edit)){
        if(!studioResourceMatches(path,edit))throw std::runtime_error("Resource baseline changed");
        consume(edit.after.data(),edit.after.size());return;
    }
    consume(edit.after.data(),edit.after.size());
    const auto actual=studioReadGlbPrefix(path,consume);
    if(actual.prefix!=edit.before||actual.digest!=edit.tailDigest||actual.tailBytes!=edit.tailBytes)
        throw std::runtime_error("GLB prefix or suffix changed");
}
inline void studioValidateModelRepairBytes(const editor::ProjectFileEdit& edit){
    if(!studioRepairModelExtension(edit.path)||edit.before.size()>StudioResourceEditBudget||
       edit.after.size()>StudioResourceEditBudget-edit.before.size())
        throw std::runtime_error("Invalid resource edit budget or extension");
    if(studioStreamingResource(edit)){
        if(edit.path.extension()!=".glb"||edit.tailDigest.size()!=64||
           edit.tailDigest.find_first_not_of("0123456789abcdef")!=std::string::npos)
            throw std::runtime_error("Invalid compact GLB tail identity");
        (void)studioDecodeGlbPrefix(edit.before,edit.tailBytes);(void)studioDecodeGlbPrefix(edit.after,edit.tailBytes);return;
    }
    if(edit.tailBytes)throw std::runtime_error("Missing GLB tail digest");
    if(!studioRepairGltfExtension(edit.path)){
        if(edit.path.extension()==".dae"){
            for(const auto* bytes:{&edit.before,&edit.after})
                if(render::xml::parse(render::xml::toUtf8(*bytes)).name!="COLLADA")
                    throw std::runtime_error("Invalid DAE repair document");
        }
        return;
    }
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
    if(studioStreamingResource(edit)){
        row.set("tailDigest",Json::string(edit.tailDigest));row.set("tailBytes",Json::number(static_cast<int64_t>(edit.tailBytes)));
    }
    if(!studioRepairGltfExtension(edit.path)||studioIsBinaryGlb(edit.before)||studioIsBinaryGlb(edit.after)){
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
    editor::ProjectFileEdit edit{std::filesystem::path(row.getStr("path")),std::move(before),std::move(after)};
    if(row.has("tailDigest")||row.has("tailBytes")){
        const auto size=row.getInt("tailBytes",-1);
        if(!allowBinary||!row["tailDigest"].isString()||!row["tailBytes"].isNumber()||
           size<0||uint64_t(size)>0xffffffffull||row["tailBytes"].numberLiteral()!=std::to_string(size))
            throw std::runtime_error("Invalid compact GLB snapshot metadata");
        edit.tailDigest=row.getStr("tailDigest");edit.tailBytes=static_cast<uint64_t>(size);
        if(edit.tailDigest.empty())throw std::runtime_error("Missing compact GLB identity");
    }
    studioValidateModelRepairBytes(edit);return edit;
}
} // namespace aiverse::studio
