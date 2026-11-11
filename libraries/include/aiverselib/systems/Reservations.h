#pragma once
#include <cstdint>
#include <map>
namespace aiverse::lib::systems {
using Quantities=std::map<std::int64_t,std::int64_t>;
struct Reservation {std::int64_t owner=0;Quantities amounts;bool operator==(const Reservation&)const=default;};
struct ReservationBook {std::map<std::int64_t,Reservation> tickets;};
enum class ReserveResult {Ok,Duplicate,Conflict,Invalid,Insufficient,Missing};
inline bool validAmounts(const Quantities& amounts){if(amounts.empty())return false;for(auto [id,n]:amounts)if(id<=0||n<=0)return false;return true;}
// Stock is total capacity, including reservations. Only this API may consume custom stock while reserved.
inline ReserveResult reserve(ReservationBook& book,const Quantities& stock,std::int64_t ticket,std::int64_t owner,const Quantities& requested){
    if(ticket<=0||owner<=0||!validAmounts(requested))return ReserveResult::Invalid;
    Reservation next{owner,requested};auto existing=book.tickets.find(ticket);
    if(existing!=book.tickets.end())return existing->second==next?ReserveResult::Duplicate:ReserveResult::Conflict;
    for(auto [id,amount]:requested){
        auto it=stock.find(id);if(it==stock.end()||it->second<amount)return ReserveResult::Insufficient;
        auto available=it->second-amount;
        for(const auto& [key,r]:book.tickets){(void)key;auto p=r.amounts.find(id);if(p!=r.amounts.end()){if(p->second<=0||p->second>available)return ReserveResult::Insufficient;available-=p->second;}}
    }
    book.tickets.emplace(ticket,std::move(next));return ReserveResult::Ok;
}
inline ReserveResult release(ReservationBook& book,std::int64_t ticket,std::int64_t owner){
    auto it=book.tickets.find(ticket);if(it==book.tickets.end())return ReserveResult::Missing;
    if(it->second.owner!=owner)return ReserveResult::Conflict;
    book.tickets.erase(it);return ReserveResult::Ok;
}
inline ReserveResult commit(ReservationBook& book,Quantities& stock,std::int64_t ticket,std::int64_t owner){
    auto it=book.tickets.find(ticket);if(it==book.tickets.end())return ReserveResult::Missing;
    if(it->second.owner!=owner)return ReserveResult::Conflict;
    if(!validAmounts(it->second.amounts))return ReserveResult::Invalid;
    for(auto [id,n]:it->second.amounts){
        (void)n;auto p=stock.find(id);if(p==stock.end()||p->second<0)return ReserveResult::Insufficient;
        auto remaining=p->second;
        for(const auto& [key,r]:book.tickets){(void)key;auto amount=r.amounts.find(id);if(amount!=r.amounts.end()){
            if(amount->second<=0)return ReserveResult::Invalid;
            if(amount->second>remaining)return ReserveResult::Insufficient;
            remaining-=amount->second;
        }}
    }
    for(auto [id,n]:it->second.amounts)stock.at(id)-=n;
    book.tickets.erase(it);return ReserveResult::Ok;
}
}
