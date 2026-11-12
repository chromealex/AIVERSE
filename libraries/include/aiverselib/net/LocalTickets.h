#pragma once
#include "Admission.h"
#include "engine/data/Json.h"
#include <map>
#include <set>
#include <limits>

namespace aiverse::lib::session {
// Trusted local file adapter for tools/meta/local_match.py. Not a remote credential
// validator or cryptographic protocol. File permissions/atomic replacement are host-owned.
class LocalTickets {
public:
    bool read(const Json& data){
        if(!data.isObject()||!data["version"].isNumber()||data["version"].fx()!=Fixed::fromInt(1)||!data["tickets"].isArray()||data["tickets"].size()>10000)return false;
        std::map<std::string,Record> next;std::set<std::pair<std::string,int64_t>> slots;std::set<std::pair<std::string,std::string>> players;
        for(const auto& row:data["tickets"].items()){
            auto token=row.getStr("token"),player=row.getStr("player"),audience=row.getStr("audience");
            if(!detail::tokenValid(token)||player.empty()||player.size()>128||audience.empty()||audience.size()>128||!row["slot"].isNumber()||!row["expires"].isNumber())return false;
            auto slot=row.getInt("slot"),expires=row.getInt("expires");
            if(slot<1||slot>200||expires<1||expires>1000000000000LL||row["slot"].fx()!=Fixed::fromInt(slot)||row["expires"].fx()!=Fixed::fromInt(expires))return false;
            if(!next.emplace(token,Record{player,audience,uint8_t(slot),uint64_t(expires)}).second||!slots.emplace(audience,slot).second||!players.emplace(audience,player).second)return false;
        }
        entries_=std::move(next);return true;
    }
    std::optional<Principal> verify(const std::string& token,const std::string& audience,uint64_t utcSeconds,uint64_t monotonicMs)const{
        auto it=entries_.find(token);if(it==entries_.end()||it->second.audience!=audience||it->second.expires<=utcSeconds)return {};
        // The issuer's maximum is 15 minutes. Reject unexpectedly distant expiries.
        uint64_t remaining=it->second.expires-utcSeconds;if(remaining>900||remaining*1000>std::numeric_limits<uint64_t>::max()-monotonicMs)return {};
        return Principal{it->second.player,audience,it->second.slot,monotonicMs+remaining*1000};
    }
    void clear(){entries_.clear();}
private:
    struct Record {std::string player,audience;uint8_t slot;uint64_t expires;};
    std::map<std::string,Record> entries_;
};
}
