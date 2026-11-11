#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
namespace aiverse::lib::app {
struct RewardReceipt { std::string match, player, currency; std::int64_t amount=0; };
inline std::string rewardKey(const RewardReceipt& r) { return "match:"+std::to_string(r.player.size())+":"+r.player+std::to_string(r.match.size())+":"+r.match; }
// Economy is aiverse::meta::Economy. Reuse its receipts, balance limits and durable CAS transaction.
// Invoke only for an authoritative final result (never a predicted finish or replay event).
// Nullopt is invalid envelope; otherwise preserve the exact meta::Result including Duplicate/Conflict.
template<class Economy>
auto grantMatch(Economy& economy,const RewardReceipt& r)
    -> std::optional<decltype(economy.transact(std::string{}, {}, {{std::string{}, std::int64_t{}}}))> {
    if(r.match.empty()||r.player.empty()||r.currency.empty()||r.amount<0||economy.profile().player!=r.player)return std::nullopt;
    return economy.transact(rewardKey(r),{},{{r.currency,r.amount}});
}
}
