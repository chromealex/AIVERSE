#include "modules/rts/RtsNative.h"
#include "modules/rts/RtsModule.h"
#include <stdexcept>

namespace {
class Income final : public aiverse::game::NativeModule {
public:
    void setup(aiverse::game::RtsModule& game, aiverse::Simulation&, aiverse::game::NativeState& state,
               const aiverse::Json& config) const override {
        const auto resource = config.getStr("resource", "gold");
        const int id = game.rules().findResource(resource);
        if (id < 0) throw std::runtime_error("native module 'income': unknown resource '" + resource + "'");
        const aiverse::Fixed amount = config.getFx("amount", aiverse::Fixed::fromInt(5));
        if (amount.raw < 0) throw std::runtime_error("native module 'income': amount must be non-negative");
        state.values["resource"] = id;
        state.values["amount"] = amount.raw;
    }
    void tick(aiverse::game::RtsModule& game, aiverse::Simulation& sim, aiverse::game::NativeState& state) const override {
        if (sim.tick() % sim.tickRate() != 0) return;
        auto& players = game.state(sim).players;
        const size_t resource = size_t(state.values.at("resource"));
        const auto amount = aiverse::Fixed::fromRaw(state.values.at("amount"));
        for (const auto& player : sim.players()) {
            if (player.alive && player.slot < players.size()) players[player.slot].stock[resource] += amount;
        }
    }
};
}
std::unique_ptr<aiverse::game::NativeModule> createIncomeModule() { return std::make_unique<Income>(); }
