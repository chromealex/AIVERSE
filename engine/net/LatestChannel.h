#pragma once
#include "Reliable.h"
#include <optional>
#include <algorithm>

namespace aiverse::net {
// Replaceable, unordered snapshots. Loss never blocks the reliable command stream.
// At most one incomplete message and one completed message are retained per peer.
class LatestChannel {
public:
    void queue(const Bytes& message){if(message.size()<=131072)out_=message;}
    void flush(uint32_t token,const std::function<void(const Bytes&)>& send) {
        if(!out_)return;
        ++sendSeq_;if(!sendSeq_)++sendSeq_;
        uint16_t count=uint16_t(std::max<size_t>(1,(out_->size()+999)/1000));
        for(uint16_t i=0;i<count;++i) {
            size_t offset=size_t(i)*1000,n=std::min<size_t>(1000,out_->size()-offset);
            ByteWriter w;writePacketHeader(w,PacketKind::Latest,token);w.u32(sendSeq_);w.u16(i);w.u16(count);w.u16(uint16_t(n));
            w.buf.insert(w.buf.end(),out_->begin()+long(offset),out_->begin()+long(offset+n));send(w.buf);
        }
        out_.reset();
    }
    bool receive(ByteReader& r) {
        uint32_t seq=r.u32();uint16_t index=r.u16(),count=r.u16(),size=r.u16();
        if(!r.ok()||seq==0||count==0||count>132||index>=count||size>1000||(index+1<count&&size!=1000))return false;
        Bytes fragment;fragment.reserve(size);for(uint16_t i=0;i<size;++i)fragment.push_back(r.u8());
        if(!r.ok()||!r.atEnd())return false;
        if(seq<=delivered_||seq<receiveSeq_)return true;
        if(seq>receiveSeq_){receiveSeq_=seq;fragments_.assign(count,{});present_.assign(count,false);}
        if(count!=fragments_.size())return false;
        fragments_[index]=std::move(fragment);present_[index]=true;
        for(bool p:present_)if(!p)return true;
        Bytes message;for(const auto& f:fragments_)message.insert(message.end(),f.begin(),f.end());
        if(message.size()>131072)return false;
        in_=std::move(message);delivered_=seq;fragments_.clear();present_.clear();return true;
    }
    bool recv(Bytes& out){if(!in_)return false;out=std::move(*in_);in_.reset();return true;}
private:
    uint32_t sendSeq_=0,receiveSeq_=0,delivered_=0;
    std::optional<Bytes> out_,in_;
    std::vector<Bytes> fragments_;
    std::vector<bool> present_;
};
}
