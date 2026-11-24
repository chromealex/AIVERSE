#pragma once
// Moved from libraries/include/aiverselib/net/SnapshotDelta.h (2026-11-21, docs/architecture-layers.md L1); the old include still works.
// Delta compression of server snapshots for any authoritative game (FPS, top-down, vehicles, MMO-style
// interest sets). A snapshot is a RecordSet: stable record IDs (entity slot, object id, 0 = globals) ->
// opaque bytes. encodeDelta writes only what changed against a baseline the receiver confirmed: removed
// IDs, new records in full, changed records as XOR runs against the old bytes (same size) or in full.
// DeltaSender keeps the last snapshots sent to one peer and picks the newest acknowledged one as the
// baseline (full snapshot when none is acknowledged); DeltaReceiver keeps the decoded ones so it can
// apply a delta against any of them. Works over unreliable latest-only channels: a lost packet only
// means the next delta uses an older acknowledged baseline. Pure bytes, no clocks, no sockets.
// Decoding is bounded and rejects malformed, truncated, oversized or unknown-baseline packets.
#include <algorithm>
#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
namespace aiverse::lib::delta {
using RecordSet=std::map<uint32_t,std::string>;
inline constexpr std::size_t kDeltaMaxRecords=65536,kDeltaMaxRecordBytes=65536,kDeltaMaxBytes=4*1024*1024;
inline constexpr uint8_t kDeltaMagic=0xD5;

namespace deltaDetail {
inline void varint(std::string& out,uint64_t v){while(v>=0x80){out.push_back(char(uint8_t(v)|0x80));v>>=7;}out.push_back(char(uint8_t(v)));}
struct Reader{
    std::string_view in;std::size_t pos=0;bool ok=true;
    uint64_t varint(){uint64_t v=0;for(int shift=0;shift<64;shift+=7){if(pos>=in.size()){ok=false;return 0;}auto b=uint8_t(in[pos++]);v|=uint64_t(b&0x7f)<<shift;if(!(b&0x80))return v;}ok=false;return 0;}
    std::string_view bytes(std::size_t n){if(n>in.size()-pos){ok=false;return {};}auto v=in.substr(pos,n);pos+=n;return v;}
    uint8_t byte(){if(pos>=in.size()){ok=false;return 0;}return uint8_t(in[pos++]);}
};
// XOR runs: (zeros, literal count, literal xor bytes)*; literals are the XOR of new and old bytes.
inline std::string xorRuns(std::string_view from,std::string_view to){
    std::string out;std::size_t i=0,n=to.size();
    while(i<n){
        std::size_t z=i;while(z<n&&from[z]==to[z])++z;
        if(z==n)break;
        std::size_t lit=z;while(lit<n&&(from[lit]!=to[lit]||(lit+2<n&&(from[lit+1]!=to[lit+1]||from[lit+2]!=to[lit+2]))))++lit;   // bridge 1-2 equal bytes
        varint(out,z-i);varint(out,lit-z);for(std::size_t k=z;k<lit;++k)out.push_back(char(uint8_t(from[k])^uint8_t(to[k])));
        i=lit;
    }
    return out;
}
inline bool applyRuns(std::string& value,Reader& r,std::size_t runsBytes){
    auto end=r.pos+runsBytes;if(runsBytes>r.in.size()-r.pos)return false;std::size_t i=0;
    while(r.pos<end){
        auto zeros=r.varint(),lit=r.varint();if(!r.ok||zeros>value.size()-i||lit>value.size()-i-zeros)return false;
        i+=std::size_t(zeros);auto x=r.bytes(std::size_t(lit));if(!r.ok||r.pos>end)return false;
        for(std::size_t k=0;k<lit;++k)value[i+k]=char(uint8_t(value[i+k])^uint8_t(x[k]));i+=std::size_t(lit);
    }
    return r.pos==end;
}
inline bool recordsOk(const RecordSet& s){
    if(s.size()>kDeltaMaxRecords)return false;std::size_t total=0;
    for(const auto& [id,bytes]:s){(void)id;if(bytes.size()>kDeltaMaxRecordBytes)return false;total+=bytes.size();}
    return total<=kDeltaMaxBytes;
}
}
// Body layout: varint removed count, removed ids (ascending, delta-coded); varint written count, then per
// record: varint id gap, byte mode (0 = full, 1 = xor runs), varint length, bytes.
inline std::string encodeDelta(const RecordSet& baseline,const RecordSet& current){
    using namespace deltaDetail;std::string out;
    std::vector<uint32_t> removed;for(const auto& [id,bytes]:baseline){(void)bytes;if(!current.contains(id))removed.push_back(id);}
    varint(out,removed.size());uint32_t last=0;for(auto id:removed){varint(out,id-last);last=id;}
    std::string body;std::size_t written=0;last=0;
    for(const auto& [id,bytes]:current){
        auto it=baseline.find(id);
        if(it!=baseline.end()&&it->second==bytes)continue;
        varint(body,id-last);last=id;++written;
        if(it!=baseline.end()&&it->second.size()==bytes.size()){
            auto runs=xorRuns(it->second,bytes);
            if(runs.size()<bytes.size()){body.push_back(char(1));varint(body,runs.size());body+=runs;continue;}
        }
        body.push_back(char(0));varint(body,bytes.size());body+=bytes;
    }
    varint(out,written);out+=body;return out;
}
inline std::optional<RecordSet> applyDelta(const RecordSet& baseline,std::string_view delta){
    using namespace deltaDetail;
    if(delta.size()>kDeltaMaxBytes)return {};
    Reader r{delta};RecordSet out=baseline;
    auto removed=r.varint();if(!r.ok||removed>baseline.size())return {};
    uint64_t id=0;for(uint64_t i=0;i<removed;++i){auto gap=r.varint();if(!r.ok||(i&&!gap))return {};id+=gap;if(id>UINT32_MAX||!out.erase(uint32_t(id)))return {};}
    auto written=r.varint();if(!r.ok||written>kDeltaMaxRecords)return {};
    id=0;for(uint64_t i=0;i<written;++i){
        auto gap=r.varint();if(!r.ok||(i&&!gap))return {};id+=gap;if(id>UINT32_MAX)return {};
        auto mode=r.byte();auto length=r.varint();if(!r.ok||mode>1||length>kDeltaMaxRecordBytes*2)return {};
        if(mode==0){auto bytes=r.bytes(std::size_t(length));if(!r.ok||length>kDeltaMaxRecordBytes)return {};out[uint32_t(id)]=std::string(bytes);}
        else{auto it=out.find(uint32_t(id));if(it==out.end()||!baseline.contains(uint32_t(id))||!applyRuns(it->second,r,std::size_t(length)))return {};}
    }
    if(r.pos!=delta.size()||!recordsOk(out))return {};
    return out;
}

// Packet: magic, varint sequence (> 0, increasing per peer), varint baseline sequence (0 = full), body.
class DeltaSender{
public:
    explicit DeltaSender(std::size_t history=32):history_(std::clamp<std::size_t>(history,1,1024)){}
    // A sequence that does not grow (or invalid records) yields an empty packet: send nothing.
    std::string encode(uint32_t sequence,RecordSet current){
        if(!sequence||(!sent_.empty()&&sequence<=sent_.back().first)||!deltaDetail::recordsOk(current))return {};
        static const RecordSet empty;const RecordSet* base=&empty;uint32_t baseSeq=0;
        for(auto it=sent_.rbegin();it!=sent_.rend();++it)if(it->first<=acked_&&it->first==acked_){base=&it->second;baseSeq=it->first;break;}
        std::string out;out.push_back(char(kDeltaMagic));deltaDetail::varint(out,sequence);deltaDetail::varint(out,baseSeq);out+=encodeDelta(*base,current);
        sent_.push_back({sequence,std::move(current)});while(sent_.size()>history_)sent_.pop_front();
        lastFull_=baseSeq==0;return out;
    }
    // The receiver confirmed `sequence` (it decoded that packet). Older or unknown sequences are ignored.
    void acknowledge(uint32_t sequence){if(sequence>acked_)for(const auto& [s,set]:sent_){(void)set;if(s==sequence){acked_=sequence;break;}}}
    uint32_t acknowledged()const{return acked_;}
    bool lastWasFull()const{return lastFull_;}
    void reset(){sent_.clear();acked_=0;}
private:
    std::size_t history_;std::deque<std::pair<uint32_t,RecordSet>> sent_;uint32_t acked_=0;bool lastFull_=true;
};
class DeltaReceiver{
public:
    explicit DeltaReceiver(std::size_t history=32):history_(std::clamp<std::size_t>(history,1,1024)){}
    // Decodes a packet into the full record set; nullopt for malformed packets, old sequences or a baseline
    // this receiver no longer holds (the sender falls back to a full snapshot once acks stop matching).
    std::optional<RecordSet> decode(std::string_view packet,uint32_t* sequenceOut=nullptr){
        deltaDetail::Reader r{packet};if(r.byte()!=kDeltaMagic||!r.ok)return {};
        auto sequence=r.varint(),base=r.varint();if(!r.ok||!sequence||sequence>UINT32_MAX||base>=sequence||(!received_.empty()&&sequence<=received_.back().first))return {};
        static const RecordSet empty;const RecordSet* from=&empty;
        if(base){from=nullptr;for(const auto& [s,set]:received_)if(s==base)from=&set;if(!from)return {};}
        auto out=applyDelta(*from,packet.substr(r.pos));if(!out)return {};
        received_.push_back({uint32_t(sequence),*out});while(received_.size()>history_)received_.pop_front();
        if(sequenceOut)*sequenceOut=uint32_t(sequence);return out;
    }
    uint32_t latest()const{return received_.empty()?0:received_.back().first;}
    void reset(){received_.clear();}
private:
    std::size_t history_;std::deque<std::pair<uint32_t,RecordSet>> received_;
};
}
