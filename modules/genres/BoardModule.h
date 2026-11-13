#pragma once
#include "engine/sim/Simulation.h"

namespace aiverse::genres {
enum class BoardKind { Cards, Match3 };
enum BoardCommand : uint16_t { PlayCard = 1, EndTurn = 2, SwapCells = 3 };
struct CardDef {
    std::string id;
    int32_t cost=1, damage=0, guard=0, draw=0;
};
struct CardPlayer {
    int32_t health=30, guard=0, energy=3;
    std::vector<int32_t> deck, hand, discard;
};
struct BoardState {
    std::vector<CardPlayer> players;
    std::vector<int32_t> cells;
    int32_t active=0, turn=1, winner=0, moves=0, score=0, plays=0;
    bool resolving=false;
};
// Standalone deterministic board simulation; display and input are host responsibilities.
class BoardModule final : public GameModule {
public:
    std::string name() const override { return "board"; }
    void setup(Simulation&,const Json&,const Json&) override;
    void applyCommand(Simulation&,const Command&) override;
    void hashState(Simulation&,Hasher&) override;
    bool serializeState(Simulation&,Archive&) override;
    int result(Simulation& sim) override { return state(sim).winner; }
    BoardState& state(Simulation& sim) const { return *sim.world().resource<BoardState>(); }
    BoardKind kind() const { return kind_; }
    const std::vector<CardDef>& cards() const { return cards_; }
    int width() const { return width_; }
    int height() const { return height_; }
    bool legalSwap(const BoardState&,int a,int b) const;
private:
    void tick(Simulation&);
    void draw(Simulation&,CardPlayer&,int count);
    void endTurn(Simulation&);
    std::vector<int> matches(const std::vector<int32_t>&) const;
    bool hasMove(const BoardState&) const;
    void finishPuzzle(BoardState&) const;
    BoardKind kind_=BoardKind::Cards;
    std::vector<CardDef> cards_;
    std::vector<bool> bots_;
    int width_=6,height_=6,colors_=5,goal_=100,hand_=5,energy_=3,maxTurns_=200;
    uint64_t contentHash_=0;
};
void io(Archive&,CardPlayer&);
void io(Archive&,BoardState&);
}
