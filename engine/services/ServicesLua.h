// Lua tables for the analytics and privacy services (UI / client Lua state only, never the sim):
//
//   analytics.available() / analytics.providers()
//   analytics.event(name [, fields])                          fields: flat table of strings / numbers / booleans
//   analytics.design(id [, value] [, fields])
//   analytics.progression("start"|"complete"|"fail", "world:level[:phase]" [, fields] [, score])
//   analytics.resource("source"|"sink", currency, amount, itemType, itemId [, fields])
//   analytics.purchase{currency=, priceMicros=, itemType=, itemId=, cartType=, receipt={...}, fields={...}}
//   analytics.user(id)  analytics.property(key, value)  analytics.flush()
//   analytics.deferredLink(fn(url))                          install ad link ("" if none)
//   privacy.provider()  privacy.tracking() -> "unavailable"|"notDetermined"|"restricted"|"denied"|"authorized"
//   privacy.trackingAllowed()  privacy.requestTracking([fn(status)])  privacy.refreshTracking([fn(status)])
//   privacy.consent([{analytics=, personalizedAds=, ageRestricted=}]) -> current consent table
//
// Callbacks run on the game thread from plugins::pump(); `alive` turning false (state closed) drops them.
#pragma once
#include <memory>

typedef struct lua_State lua_State;

namespace aiverse::services {
void openServicesLua(lua_State* L, std::shared_ptr<bool> alive);
} // namespace aiverse::services
