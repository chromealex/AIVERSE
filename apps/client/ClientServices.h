// The `social`, `push` and `links` Lua tables for the client UI scripts (ClientServices.cpp) and the
// relay of their events to game.on("social" | "push" | "link").
#pragma once
#include <functional>
#include <memory>
#include "engine/data/Json.h"

struct lua_State;

namespace aiverse::client {

class ClientServices {
public:
    ClientServices();
    ~ClientServices();
    void attach(lua_State* L, std::shared_ptr<bool> alive, std::function<void(const char* name, const Json& data)> emit);
    void detach();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace aiverse::client
