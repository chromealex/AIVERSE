// Tests for card-games, tactics-grid and their bots (CardBot, TacticsBot). Usage: <test> <repository root>
#include <aiverselib/genre/CardGameData.h>
#include <aiverselib/genre/CardGamePersistence.h>
#include <aiverselib/genre/CardGameConfigured.h>
#include <aiverselib/genre/TacticsData.h>
#include <aiverselib/genre/TacticsPersistence.h>
#include <aiverselib/genre/TacticsConfigured.h>
#include <aiverselib/bots/CardBot.h>
#include <aiverselib/bots/TacticsBot.h>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
using namespace aiverse;using namespace aiverse::lib;using namespace aiverse::lib::genre;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string(__FILE__)+":"+std::to_string(__LINE__)+": " #x);}while(false)
template<class State> void roundtrip(const State& s){
    auto bytes=save(s);CHECK(!bytes.empty());State copy;CHECK(load(bytes,copy));CHECK(copy==s);CHECK(save(copy)==bytes);
    State other=copy;CHECK(!load(bytes+"x",other));CHECK(!load(bytes.substr(0,bytes.size()-1),other));CHECK(other==copy);
}
template<class F> std::string jsonError(F f){try{f();}catch(const JsonError& e){return e.what();}return {};}
static bool hasEvent(const std::vector<CardEvent>& ev,CardEventKind k){for(const auto& e:ev)if(e.kind==k)return true;return false;}
static bool hasEvent(const std::vector<TacticsEvent>& ev,TacticsEventKind k){for(const auto& e:ev)if(e.kind==k)return true;return false;}
// Test helper: puts cards with the wanted definitions into a seat's hand (rest stays in the deck, order kept).
static void arrange(CardGameState& s,int32_t seat,const std::vector<int64_t>& want){
    auto& p=s.seats[std::size_t(seat)];std::vector<uint32_t> all=p.hand;all.insert(all.end(),p.deck.begin(),p.deck.end());p.hand.clear();p.deck.clear();
    for(auto def:want)for(auto it=all.begin();it!=all.end();++it)if(s.cards.at(*it).def==def){p.hand.push_back(*it);all.erase(it);break;}
    CHECK(p.hand.size()==want.size());p.deck=all;
    for(auto id:p.hand){s.cards.at(id).zone=CardGameZone::Hand;s.cards.at(id).known=uint8_t(1u<<uint32_t(seat));}
    for(auto id:p.deck){s.cards.at(id).zone=CardGameZone::Deck;s.cards.at(id).known=0;}
    CHECK(valid(s));
}
static uint32_t handCard(const CardGameState& s,int32_t seat,int64_t def){for(auto id:s.seats[std::size_t(seat)].hand)if(s.cards.at(id).def==def)return id;return 0;}
static const char* kCards=R"({"rules":{"startingHand":3,"maxHand":6,"startingHealth":10,"energy":[5,0,5],"deck":[1,30],"mulligan":"none","fatigue":false,"turnLimit":0},
 "cards":[{"id":1,"name":"c.bolt","kind":"instant","cost":1,"target":"character","side":"enemy","effects":[{"op":"damage","amount":2}]},
  {"id":2,"name":"c.squire","kind":"unit","cost":1,"power":1,"health":2,"guard":true},
  {"id":3,"name":"c.knight","kind":"unit","cost":3,"power":3,"health":3},
  {"id":4,"name":"c.wolf","kind":"unit","cost":2,"power":2,"health":1,"rush":true},
  {"id":5,"name":"c.negate","kind":"instant","cost":2,"target":"stackItem","side":"enemy","effects":[{"op":"counter"}]},
  {"id":6,"name":"c.insight","kind":"spell","cost":1,"effects":[{"op":"draw","amount":2,"to":"controller"}]},
  {"id":7,"name":"c.fireball","kind":"spell","cost":4,"target":"character","side":"enemy","effects":[{"op":"damage","amount":5}]},
  {"id":8,"name":"c.rally","kind":"spell","cost":2,"effects":[{"op":"buff","amount":1,"to":"friendlyUnits"}]},
  {"id":9,"name":"c.spyglass","kind":"spell","cost":0,"effects":[{"op":"reveal","to":"opponents"}]},
  {"id":10,"name":"c.mend","kind":"instant","cost":2,"effects":[{"op":"heal","amount":4,"to":"controller"}]},
  {"id":11,"name":"c.mill","kind":"spell","cost":0,"target":"player","side":"enemy","effects":[{"op":"mill","amount":2}]}]})";
static CardLibrary cardLib(const std::string& rules=""){
    auto j=Json::parse(kCards);auto lib=readCardLibrary(j);
    if(!rules.empty())lib.rules=readCardLibrary(Json::parse(std::string(R"({"rules":)")+rules+R"(,"cards":[{"id":1,"name":"x","kind":"spell","cost":0}]})"),lib.rules).rules;
    return lib;
}
struct CardGame{CardLibrary lib;CardGameState s;Random rng;uint64_t seq=0;std::vector<CardEvent> last;
    bool act(int32_t p,CardAction a){auto r=stepCardGame(lib,s,{++seq,p,a},rng);if(r)last=r->events;return bool(r);}
    bool pass(int32_t p){return act(p,{});}
    bool play(int32_t p,uint32_t card,CardTargetRef t={}){return act(p,{CardActionKind::Play,card,t,0});}
};
static CardGame cardGame(CardLibrary lib,std::vector<std::vector<int64_t>> decks,int32_t first=0,uint64_t seed=1){
    CardGame g{lib,{},Random(seed),0,{}};auto setup=cardGameSetup(lib,decks,first,g.rng);CHECK(setup);g.s=setup->state;g.last=setup->events;return g;
}
void cardData(const std::string& root){
    auto duel=Json::parseFile(root+"/examples/libraries/genre/cards-turns/duel.json");auto lib=readCardLibrary(duel);auto decks=readCardDecks(duel,lib);
    CHECK(lib.cards.size()==10&&decks.size()==2&&decks[0].size()==20&&lib.rules.energyMax==8&&lib.rules.mulligan==CardMulligan::Partial);
    CHECK(cardLibraryHash(lib)==cardLibraryHash(readCardLibrary(duel)));
    auto bad=[&](const char* text){return jsonError([&]{readCardLibrary(Json::parse(text));});};
    CHECK(bad(R"({"cards":[{"id":1,"name":"a","kind":"spell","cost":0,"foo":1}]})")=="cards.cards[0]: unknown key 'foo'");
    CHECK(bad(R"({"cards":[{"id":1,"name":"a","kind":"trap","cost":0}]})").starts_with("cards.cards[0].kind: unknown value 'trap'"));
    CHECK(bad(R"({"cards":[{"id":1,"name":"a","kind":"spell","cost":0,"effects":[{"op":"counter","to":"controller"}]}]})")=="cards.card 1.effects[0]: counter needs a stackItem target");
    CHECK(bad(R"({"cards":[{"id":1,"name":"a","kind":"spell","cost":0},{"id":1,"name":"b","kind":"spell","cost":0}]})")=="cards.cards[1].id: duplicate id 1");
    CHECK(bad(R"({"rules":{"seats":9},"cards":[{"id":1,"name":"a","kind":"spell","cost":0}]})")=="cards.rules.seats: must be in 2..4");
    CHECK(bad(R"({"cards":[{"id":1,"name":"a","kind":"unit","cost":0,"power":1,"health":1,"target":"player"}]})").starts_with("cards.card 1.target"));
    CHECK(jsonError([&]{readCardDecks(Json::parse(R"({"decks":[[1],[99]]})"),cardLib());})=="cards.decks[1][0]: unknown card 99");
    auto settings=gameplay::readGameplaySettings(Json::parse(R"({"schemaVersion":1,"cardGame":{"startingHand":5,"maxHand":7,"botDepth":3}})"));
    CHECK(settings.value);auto rules=cardRulesFromSettings(*settings.value);CHECK(rules.startingHand==5&&rules.maxHand==7);CHECK(bots::cardBotConfig(*settings.value).depth==3);
    auto badSettings=gameplay::readGameplaySettings(Json::parse(R"({"schemaVersion":1,"cardGame":{"startingHand":9,"maxHand":7}})"));
    CHECK(!badSettings.value&&badSettings.errors.front()=="cardGame.startingHand: must not exceed maxHand");
}
void cardStackAndPriority(){
    auto g=cardGame(cardLib(),{{7,1,1},{5,2,4}});
    CHECK(g.s.phase==CardPhase::Main&&g.s.turn==1&&g.s.active==0&&g.s.seats[0].hand.size()==3&&g.s.seats[0].energy==5&&g.s.seats[1].energy==0);
    CHECK(g.pass(0)&&g.s.turn==2&&g.s.active==1&&g.pass(1)&&g.s.turn==3&&g.s.seats[1].energy==5);  // seat 1 keeps its unspent energy
    auto fireball=handCard(g.s,0,7),negate=handCard(g.s,1,5),squire=handCard(g.s,1,2);
    CHECK(cardLegalActions(g.lib,g.s,1).empty());                                     // no priority
    CHECK(g.play(0,fireball,{CardTargetType::Player,1})&&g.s.priority==1&&g.s.stack.size()==1&&g.s.seats[0].energy==1);
    auto legal=cardLegalActions(g.lib,g.s,1);
    CHECK(std::binary_search(legal.begin(),legal.end(),CardAction{CardActionKind::Play,negate,{CardTargetType::Stack,1},0}));
    CHECK(!std::binary_search(legal.begin(),legal.end(),CardAction{CardActionKind::Play,squire,{},0}));   // units are sorcery speed
    auto before=g.s;auto rng=g.rng.state();
    CHECK(!g.play(1,squire));CHECK(!g.play(0,handCard(g.s,0,1),{CardTargetType::Player,1}));          // illegal / no priority
    CHECK(!stepCardGame(g.lib,g.s,{g.s.lastCommand,1,{}},g.rng));                                             // repeated sequence
    CHECK(g.s==before&&g.rng.state()==rng);
    CHECK(g.play(1,negate,{CardTargetType::Stack,1})&&g.s.priority==0&&g.s.stack.size()==2);
    CHECK(g.pass(0)&&g.s.priority==1&&g.s.stack.size()==2);
    CHECK(g.pass(1)&&g.s.stack.empty()&&g.s.priority==0&&hasEvent(g.last,CardEventKind::Countered));   // LIFO: negate first, fireball countered
    CHECK(g.s.seats[1].health==10&&g.s.cards.at(fireball).zone==CardGameZone::Discard&&g.s.cards.at(negate).zone==CardGameZone::Discard);
    roundtrip(g.s);
    // Fizzle: two bolts at one unit; the second resolves first and kills it, the first fizzles.
    auto f=cardGame(cardLib(),{{1,1,1},{2,2,2}},1);
    CHECK(f.play(1,handCard(f.s,1,2))&&f.pass(0)&&f.pass(1)&&f.s.seats[1].board.size()==1);
    auto unit=f.s.seats[1].board[0];CHECK(f.s.cards.at(unit).exhausted);
    CHECK(f.pass(1)&&f.s.active==0&&f.s.turn==2);
    auto a=f.s.seats[0].hand[0],b=f.s.seats[0].hand[1];
    CHECK(f.play(0,a,{CardTargetType::Unit,unit})&&f.pass(1)&&f.s.priority==0);
    CHECK(f.play(0,b,{CardTargetType::Unit,unit})&&f.pass(1)&&f.pass(0)&&hasEvent(f.last,CardEventKind::UnitDied)&&f.s.stack.size()==1);
    roundtrip(f.s);
    CHECK(f.pass(0)&&f.pass(1)&&hasEvent(f.last,CardEventKind::Fizzled)&&f.s.stack.empty()&&f.s.cards.at(a).zone==CardGameZone::Discard);
    CHECK(cardGameHash(f.s)!=cardGameHash(g.s));
}
void cardRulesVariants(){
    // Guard: attackers must hit guard units; rush units attack on the turn they enter.
    auto g=cardGame(cardLib(),{{4,4,4},{2,2,2}},1);
    CHECK(g.play(1,handCard(g.s,1,2))&&g.pass(0)&&g.pass(1)&&g.pass(1));
    auto wolf=handCard(g.s,0,4);CHECK(g.play(0,wolf)&&g.pass(1)&&g.pass(0)&&!g.s.cards.at(wolf).exhausted);
    auto squire=g.s.seats[1].board[0];std::vector<CardAction> attacks;
    for(const auto& a:cardLegalActions(g.lib,g.s,0))if(a.kind==CardActionKind::Attack)attacks.push_back(a);
    CHECK((attacks.size()==1&&attacks[0].target==CardTargetRef{CardTargetType::Unit,squire}));
    CHECK(g.act(0,attacks[0])&&g.pass(1)&&g.pass(0)&&hasEvent(g.last,CardEventKind::UnitDied));
    CHECK(g.s.cards.at(wolf).zone==CardGameZone::Discard&&g.s.cards.at(squire).zone==CardGameZone::Discard);   // 2/1 rush vs 1/2 guard: both die
    // Separate combat phase: attacks only after the main phase is passed.
    auto c=cardGame(cardLib(R"({"separateCombat":true})"),{{4,4,4},{1,1,1}});
    auto w=handCard(c.s,0,4);CHECK(c.play(0,w)&&c.pass(1)&&c.pass(0));
    auto attackLegal=[&]{for(const auto& a:cardLegalActions(c.lib,c.s,0))if(a.kind==CardActionKind::Attack)return true;return false;};
    CHECK(!attackLegal()&&c.pass(0)&&c.s.phase==CardPhase::Combat&&attackLegal());
    CHECK(c.pass(0)&&c.s.active==1&&c.s.phase==CardPhase::Main);
    // Partial mulligan (bit = hand position), then full mulligan with a penalty.
    auto m=cardGame(cardLib(R"({"mulligan":"partial"})"),{{1,2,3,4,5,6},{1,2,3,4,5,6}});
    CHECK(m.s.phase==CardPhase::Mulligan&&m.s.priority==0&&cardLegalActions(m.lib,m.s,0).size()==8&&cardLegalActions(m.lib,m.s,1).empty());
    auto kept=m.s.seats[0].hand[1];CHECK(m.act(0,{CardActionKind::Mulligan,0,{},0b101}));
    CHECK(m.s.seats[0].hand.size()==3&&m.s.seats[0].hand[0]==kept&&m.s.seats[0].deck.size()==3&&m.s.priority==1&&hasEvent(m.last,CardEventKind::Shuffled));
    CHECK(m.act(1,{CardActionKind::Mulligan,0,{},0})&&m.s.phase==CardPhase::Main&&m.s.turn==1);
    auto full=cardGame(cardLib(R"({"mulligan":"full","mulliganPenalty":1})"),{{1,2,3,4,5,6},{1,2,3,4,5,6}});
    CHECK(cardLegalActions(full.lib,full.s,0).size()==2&&full.act(0,{CardActionKind::Mulligan,0,{},0b111})&&full.s.seats[0].hand.size()==2);
    // Overdraw burns, fatigue grows, the turn limit ends the game by health.
    auto o=cardGame(cardLib(R"({"maxHand":3,"fatigue":true,"turnLimit":3})"),{{1,1,1,1},{2,2,2}});
    CHECK(o.pass(0)&&o.s.turn==2&&hasEvent(o.last,CardEventKind::Fatigue)&&o.s.seats[1].health==9);
    CHECK(o.pass(1)&&hasEvent(o.last,CardEventKind::Burned)&&o.s.seats[0].discard.size()==1);
    CHECK(o.pass(0)&&o.s.phase==CardPhase::GameOver&&o.s.winner==0&&cardLegalActions(o.lib,o.s,0).empty());
    roundtrip(o.s);
    // Draw spell, buff, mill (public), heal capped at starting health.
    auto e=cardGame(cardLib(),{{6,8,2,2,2,3,3},{11,10,1,1,1}},0);arrange(e.s,0,{6,8,2});arrange(e.s,1,{11,10,1});
    CHECK(e.play(0,handCard(e.s,0,6))&&e.pass(1)&&e.pass(0)&&e.s.seats[0].hand.size()==4&&e.s.seats[0].deck.size()==2);
    CHECK(e.play(0,handCard(e.s,0,2))&&e.pass(1)&&e.pass(0)&&e.play(0,handCard(e.s,0,8))&&e.pass(1)&&e.pass(0));
    CHECK(cardPower(e.lib,e.s.cards.at(e.s.seats[0].board[0]))==2&&cardHealth(e.lib,e.s.cards.at(e.s.seats[0].board[0]))==3&&e.pass(0));
    CHECK(e.play(1,handCard(e.s,1,11),{CardTargetType::Player,0})&&e.pass(0)&&e.pass(1)&&e.s.seats[0].discard.size()==4&&hasEvent(e.last,CardEventKind::Milled));
    CHECK(e.play(1,handCard(e.s,1,10))&&e.pass(0)&&e.pass(1)&&e.s.seats[1].health==10&&hasEvent(e.last,CardEventKind::Healed));
}
void cardHiddenInformation(){
    auto lib=cardLib();auto g=cardGame(lib,{{9,1,1,1},{3,3,4,4,5}},0,3);arrange(g.s,0,{9,1,1});
    auto v0=cardViewFor(lib,g.s,0),spect=cardViewFor(lib,g.s,-1);CHECK(v0&&spect);
    CHECK(v0->seats[0].hand.size()==3&&v0->seats[1].hand.empty()&&v0->seats[1].handCount==3&&v0->seats[1].deckCount==2);
    CHECK(spect->seats[0].hand.empty()&&spect->seats[1].hand.empty());
    // Hidden cards never reach the opponent: draws are redacted, and changing hidden cards leaves its view unchanged.
    for(const auto& e:cardEventsFor(g.last,0))if(e.kind==CardEventKind::Drew&&e.player==1)CHECK(e.card==0&&e.def==0);
    bool ownDraw=false;for(const auto& e:cardEventsFor(g.last,1))if(e.kind==CardEventKind::Drew&&e.player==1)ownDraw=ownDraw||e.def>0;CHECK(ownDraw);
    auto altered=g.s;for(auto id:altered.seats[1].hand)altered.cards.at(id).def=1;for(auto id:altered.seats[1].deck)altered.cards.at(id).def=7;
    CHECK(cardViewFor(lib,altered,0)==v0&&cardViewFor(lib,altered,1)!=cardViewFor(lib,g.s,1));
    auto c1=bots::cardBotChoose(lib,*v0),c2=bots::cardBotChoose(lib,*cardViewFor(lib,altered,0));CHECK(c1&&c2&&c1->action==c2->action&&c1->score==c2->score);
    // Reveal: the spyglass shows seat 1's hand to seat 0 only.
    CHECK(g.play(0,handCard(g.s,0,9))&&g.pass(1)&&g.pass(0)&&hasEvent(g.last,CardEventKind::Revealed));
    CHECK(cardViewFor(lib,g.s,0)->seats[1].hand.size()==3&&cardViewFor(lib,g.s,-1)->seats[1].hand.empty());
    for(const auto& e:cardEventsFor(g.last,-1))if(e.kind==CardEventKind::Revealed)CHECK(e.def==0);
    CHECK((g.s.cards.at(g.s.seats[1].hand[0]).known&1u)!=0);
    roundtrip(g.s);
}
void cardBotGames(const std::string& root){
    auto duel=Json::parseFile(root+"/examples/libraries/genre/cards-turns/duel.json");auto lib=readCardLibrary(duel);auto decks=readCardDecks(duel,lib);
    auto play=[&](uint64_t seed){
        Random rng(seed);auto st=cardGameSetup(lib,decks,int32_t(seed%2),rng)->state;uint64_t seq=0,h=0;
        for(uint32_t step=0;st.phase!=CardPhase::GameOver;++step){
            CHECK(step<20000);auto seat=st.priority;auto view=cardViewFor(lib,st,seat);CHECK(view);
            auto choice=bots::cardBotChoose(lib,*view);CHECK(choice&&choice->nodes<=256);
            auto r=stepCardGame(lib,st,{++seq,seat,choice->action},rng);CHECK(r);for(const auto& e:r->events)h^=cardGameHash(e)+seq;
            if(step%37==0){CardGameState copy;CHECK(load(save(st),copy)&&copy==st);}
        }
        CHECK(st.winner!=kCardNoWinner);return std::pair{st.winner,h^cardGameHash(st)};
    };
    for(uint64_t seed=1;seed<=4;++seed){auto a=play(seed),b=play(seed);CHECK((a==b));}
    CHECK(play(1).second!=play(2).second);
    bots::CardBotConfig bad;bad.depth=0;CHECK(!bots::cardBotChoose(lib,*cardViewFor(lib,cardGameSetup(lib,decks,0,*std::make_unique<Random>(1))->state,0),bad));
}
static const char* kSquare=R"({"grid":{"shape":"square","rows":[".....",".,#..","..h..",".....","..H.."]},
 "unitTypes":[{"id":1,"name":"u.a","hp":6,"ap":2,"mobility":2,"aimMilli":700,"damage":3,"flankDamage":1,"range":[4,6],"sight":8,"initiative":10},
              {"id":2,"name":"u.b","hp":4,"ap":2,"mobility":3,"aimMilli":700,"damage":2,"range":[3,5],"sight":8,"initiative":14}],
 "units":[{"id":1,"type":1,"team":1,"at":[0,0]},{"id":2,"type":2,"team":1,"at":[4,0]},{"id":3,"type":1,"team":2,"at":[2,3]}]})";
void tacticsGrid(){
    auto data=readTacticsScenario(Json::parse(kSquare));const auto& g=data.scenario.grid;
    CHECK(g.width==5&&g.height==5&&g.cells[std::size_t(tacticsIndex(g,{2,1}))].cost==0&&g.cells[std::size_t(tacticsIndex(g,{1,1}))].cost==2);
    std::vector<uint8_t> free(g.cells.size(),kTacticsFree);
    auto r=tacticsReachable(g,0,6,free);CHECK(r);
    CHECK(r->cost[std::size_t(tacticsIndex(g,{1,0}))]==2&&r->cost[std::size_t(tacticsIndex(g,{2,0}))]==4&&r->cost[std::size_t(tacticsIndex(g,{1,1}))]==6);   // rough diagonal 3x2
    CHECK(r->cost[std::size_t(tacticsIndex(g,{2,1}))]==-1&&r->cost[std::size_t(tacticsIndex(g,{3,0}))]==6);
    CHECK(tacticsStepCost(g,tacticsIndex(g,{3,0}),tacticsIndex(g,{2,1}))==-1&&tacticsStepCost(g,tacticsIndex(g,{1,0}),tacticsIndex(g,{2,1}))==-1); // wall, corner
    auto big=tacticsReachable(g,0,40,free);auto path=tacticsPath(*big,tacticsIndex(g,{4,4}));
    CHECK(!path.empty()&&path.back()==tacticsIndex(g,{4,4})&&path==tacticsPath(*tacticsReachable(g,0,40,free),tacticsIndex(g,{4,4})));
    auto blocked=free;blocked[std::size_t(tacticsIndex(g,{1,0}))]=kTacticsBlocked;blocked[std::size_t(tacticsIndex(g,{0,1}))]=kTacticsPassOnly;
    auto rb=tacticsReachable(g,0,4,blocked);CHECK(rb->cost[std::size_t(tacticsIndex(g,{1,0}))]==-1&&!rb->canStop(tacticsIndex(g,{0,1}),blocked)&&rb->canStop(tacticsIndex(g,{0,2}),blocked));
    // Hex distance, neighbours and lines.
    TacticsGrid hex{TacticsShape::Hex,7,7,std::vector<TacticsCell>(49)};
    CHECK(tacticsDistance(hex,tacticsIndex(hex,{0,0}),tacticsIndex(hex,{3,0}))==3&&tacticsDistance(hex,tacticsIndex(hex,{0,3}),tacticsIndex(hex,{3,0}))==3);
    CHECK(tacticsDistance(hex,tacticsIndex(hex,{0,0}),tacticsIndex(hex,{3,3}))==6&&tacticsNeighbors(hex,tacticsIndex(hex,{3,3})).size()==6);
    auto line=tacticsLine(hex,tacticsIndex(hex,{0,0}),tacticsIndex(hex,{3,3}));CHECK(line.cells.size()==6&&!line.blocked);
    for(std::size_t i=1;i<line.cells.size();++i)CHECK(tacticsDistance(hex,line.cells[i-1],line.cells[i])==1);
    hex.cells[std::size_t(line.cells[2])].blocksSight=true;CHECK(tacticsLine(hex,tacticsIndex(hex,{0,0}),tacticsIndex(hex,{3,3})).blocked);
    auto sq=tacticsLine(g,tacticsIndex(g,{0,4}),tacticsIndex(g,{4,0}));CHECK(sq.cells.size()==4&&sq.cells.back()==tacticsIndex(g,{4,0}));
    CHECK(tacticsLine(g,tacticsIndex(g,{2,0}),tacticsIndex(g,{2,3})).blocked&&!tacticsLine(g,tacticsIndex(g,{2,3}),tacticsIndex(g,{2,0})).cells.empty());
    // Cover: low wall (2,2) covers (2,3) from the north; the side is a flank; open ground is exposed; H is full cover.
    auto t=tacticsIndex(g,{2,3});
    auto north=tacticsCoverAgainst(g,t,tacticsIndex(g,{2,0}));CHECK(north.cover==TacticsCover::Half&&!north.flanked&&!north.exposed);
    auto side=tacticsCoverAgainst(g,t,tacticsIndex(g,{4,3}));CHECK(side.cover==TacticsCover::None&&side.flanked);
    CHECK(tacticsCoverAgainst(g,tacticsIndex(g,{0,3}),tacticsIndex(g,{4,0})).exposed);
    CHECK(tacticsCoverAgainst(g,tacticsIndex(g,{2,3}),tacticsIndex(g,{2,4})).cover==TacticsCover::Full);
    CHECK(tacticsCoverAgainst(g,t,tacticsIndex(g,{3,0})).cover==TacticsCover::Half&&tacticsCoverAgainst(g,t,tacticsIndex(g,{4,1})).cover==TacticsCover::Half&&tacticsCoverAgainst(g,t,tacticsIndex(g,{4,2})).cover==TacticsCover::None); // 45 degree half-arc, inclusive
    TacticsFireRules fr;TacticsWeapon w{700,2,6};
    CHECK(tacticsPreviewShot(g,fr,tacticsIndex(g,{2,0}),t,w).hitMilli==0);                              // '#' blocks the line
    TacticsGrid open{TacticsShape::Square,5,5,std::vector<TacticsCell>(25)};open.cells[12]={0,TacticsCover::Half,false};
    auto shot=tacticsPreviewShot(open,fr,tacticsIndex(open,{2,0}),t,w);
    CHECK(shot.clear&&shot.inRange&&shot.distance==3&&shot.cover.cover==TacticsCover::Half&&shot.hitMilli==700-200-40);
    CHECK(tacticsPreviewShot(open,fr,tacticsIndex(open,{4,3}),t,w).hitMilli==700+150);
    CHECK(tacticsPreviewShot(open,fr,tacticsIndex(open,{4,3}),t,w,true).hitMilli==700);
    CHECK(tacticsPreviewShot(open,fr,tacticsIndex(open,{0,0}),tacticsIndex(open,{4,4}),{700,2,3}).hitMilli==0);   // out of range
    CHECK(tacticsPreviewShot(open,fr,tacticsIndex(open,{0,0}),tacticsIndex(open,{1,1}),{990,2,3}).hitMilli==950);   // clamped
    CHECK(!tacticsCanSee(g,tacticsIndex(g,{2,0}),tacticsIndex(g,{2,3}),8)&&tacticsCanSee(open,tacticsIndex(open,{2,0}),t,8)&&!tacticsCanSee(open,tacticsIndex(open,{2,0}),t,2));
}
struct TacticsGame{TacticsScenario sc;TacticsState s;Random rng;uint64_t seq=0;std::vector<TacticsEvent> last;
    bool act(int64_t unit,TacticsAction a){auto r=stepTactics(sc,s,{++seq,s.units.at(unit).team,unit,a},rng);if(r)last=r->events;return bool(r);}
};
void tacticsTurns(){
    auto data=readTacticsScenario(Json::parse(kSquare));TacticsGame g{data.scenario,*tacticsBegin(data.scenario,data.units),Random(5),0,{}};
    CHECK((g.s.order==std::vector<int64_t>{2,1,3})&&tacticsActiveUnit(g.s)==2&&g.s.units.at(2).ap==2);  // initiative 14, then 10/10 by id
    auto before=g.s;
    CHECK(!g.act(1,{})&&!stepTactics(g.sc,g.s,{++g.seq,2,2,{}},g.rng)&&!g.act(2,{TacticsActionKind::Move,tacticsIndex(g.sc.grid,{2,1}),0}));
    CHECK(g.s==before);
    // Delay: unit 2 re-queues after the others once per round.
    CHECK(g.act(2,{TacticsActionKind::Delay,-1,0})&&(g.s.order==std::vector<int64_t>{1,3,2})&&tacticsActiveUnit(g.s)==1&&hasEvent(g.last,TacticsEventKind::Delayed));
    CHECK(!stepTactics(g.sc,g.s,{g.s.lastCommand,1,1,{}},g.rng));                                     // repeated sequence
    // AP: a 1-AP move (mobility 2 = 4 half-units) then attack, which ends the turn.
    auto legal=tacticsLegalActions(g.sc,g.s,1);
    CHECK(tacticsMoveCost(g.sc,g.s,1,tacticsIndex(g.sc.grid,{1,0}))==1&&tacticsMoveCost(g.sc,g.s,1,tacticsIndex(g.sc.grid,{0,4}))==2);
    CHECK(g.act(1,{TacticsActionKind::Move,tacticsIndex(g.sc.grid,{0,2}),0})&&g.s.units.at(1).ap==1&&tacticsActiveUnit(g.s)==1);
    legal=tacticsLegalActions(g.sc,g.s,1);
    CHECK(std::binary_search(legal.begin(),legal.end(),TacticsAction{TacticsActionKind::Attack,-1,3}));
    CHECK(g.act(1,{TacticsActionKind::Attack,-1,3})&&hasEvent(g.last,TacticsEventKind::Shot)&&tacticsActiveUnit(g.s)==3);
    // Overwatch on unit 3; unit 2 then walks through its line of fire and draws exactly one reaction shot.
    CHECK(g.act(3,{TacticsActionKind::Overwatch,-1,0})&&g.s.reservations.size()==1&&tacticsActiveUnit(g.s)==2);
    CHECK(tacticsLegalActions(g.sc,g.s,2).size()>2&&g.act(2,{TacticsActionKind::Move,tacticsIndex(g.sc.grid,{4,2}),0}));
    CHECK(hasEvent(g.last,TacticsEventKind::ReactionShot)&&g.s.reservations[0].spent);
    roundtrip(g.s);
    if(g.s.units.at(2).alive&&tacticsActiveUnit(g.s)==2){CHECK(g.act(2,{TacticsActionKind::Move,tacticsIndex(g.sc.grid,{4,3}),0})&&!hasEvent(g.last,TacticsEventKind::ReactionShot));}
    // Speed modifier: unit 3 gets +10 initiative for round 2 only.
    auto m=TacticsGame{data.scenario,*tacticsBegin(data.scenario,data.units),Random(1),0,{}};
    CHECK(tacticsSetSpeedModifier(m.s,3,{7,10,2})&&!tacticsSetSpeedModifier(m.s,3,{0,1,2}));
    for(int i=0;i<3;++i)CHECK(m.act(tacticsActiveUnit(m.s),{}));
    CHECK(m.s.round==2&&(m.s.order==std::vector<int64_t>{3,2,1}));
    for(int i=0;i<3;++i)CHECK(m.act(tacticsActiveUnit(m.s),{}));
    CHECK(m.s.round==3&&(m.s.order==std::vector<int64_t>{2,1,3}));
    // Visibility: the enemy behind the high wall is not in team 2's view; events about it are dropped.
    auto hidden=readTacticsScenario(Json::parse(R"({"grid":{"shape":"square","rows":["..H..","..H..","..H.."]},
        "unitTypes":[{"id":1,"name":"u","hp":5,"mobility":2,"range":[3,6],"sight":6}],
        "units":[{"id":1,"type":1,"team":1,"at":[0,1]},{"id":2,"type":1,"team":2,"at":[4,1]},{"id":3,"type":1,"team":2,"at":[4,0]}]})"));
    auto hs=*tacticsBegin(hidden.scenario,hidden.units);auto v2=tacticsViewFor(hidden.scenario,hs,2);CHECK(v2&&v2->units.size()==2&&v2->active==0&&v2->order.size()==2);
    auto v1=tacticsViewFor(hidden.scenario,hs,1);CHECK(v1->units.size()==1&&v1->active==1);
    auto moved=hs;moved.units.at(3).cell=tacticsIndex(hidden.scenario.grid,{3,2});CHECK(tacticsViewFor(hidden.scenario,moved,1)==v1);
    auto b1=bots::tacticsBotChoose(hidden.scenario,*v1,tacticsIndex(hidden.scenario.grid,{4,2})),b2=bots::tacticsBotChoose(hidden.scenario,*tacticsViewFor(hidden.scenario,moved,1),tacticsIndex(hidden.scenario.grid,{4,2}));
    CHECK(b1&&b2&&b1->action==b2->action&&b1->score==b2->score&&b1->evaluated<=512);
    std::vector<TacticsEvent> ev{{TacticsEventKind::Stepped,2,0,4,0},{TacticsEventKind::Stepped,1,0,3,0},{TacticsEventKind::Shot,2,1,5,500}};
    auto seen=tacticsEventsFor(*v1,ev);CHECK(seen.size()==2&&seen[0].unit==1&&seen[1].kind==TacticsEventKind::Shot&&seen[1].cell==5);
    // JSON errors carry paths.
    auto bad=[&](const std::string& text){return jsonError([&]{readTacticsScenario(Json::parse(text));});};
    CHECK(bad(R"({"grid":{"shape":"square","rows":["..","x."]},"unitTypes":[{"id":1,"name":"u"}],"units":[{"id":1,"type":1,"team":1,"at":[0,0]}]})")=="tactics.grid.rows[1][0]: unknown cell 'x'");
    CHECK(bad(R"({"grid":{"shape":"hex","rows":["..","..."]},"unitTypes":[{"id":1,"name":"u"}],"units":[{"id":1,"type":1,"team":1,"at":[0,0]}]})")=="tactics.grid.rows[1]: expected 2 cells like row 0");
    CHECK(bad(R"({"grid":{"shape":"square","rows":[".#"]},"unitTypes":[{"id":1,"name":"u"}],"units":[{"id":1,"type":1,"team":1,"at":[1,0]}]})")=="tactics.units[0].at: cell is impassable");
    CHECK(bad(R"({"grid":{"shape":"square","rows":[".."]},"unitTypes":[{"id":1,"name":"u","range":[5,2]}],"units":[{"id":1,"type":1,"team":1,"at":[1,0]}]})")=="tactics.unitTypes[0].range: optimal must not exceed max");
    CHECK(bad(R"({"grid":{"shape":"square","rows":[".."]},"rules":{"minHitMilli":900,"maxHitMilli":100},"unitTypes":[{"id":1,"name":"u"}],"units":[{"id":1,"type":1,"team":1,"at":[1,0]}]})")=="tactics.rules.minHitMilli: must not exceed maxHitMilli");
    auto settings=gameplay::readGameplaySettings(Json::parse(R"({"schemaVersion":1,"tactics":{"roundLimit":12,"halfCoverMilli":250,"botSearchBudget":64}})"));
    CHECK(settings.value&&tacticsRulesFromSettings(*settings.value).roundLimit==12&&tacticsRulesFromSettings(*settings.value).fire.halfCoverMilli==250&&bots::tacticsBotConfig(*settings.value).searchBudget==64);
    CHECK(!gameplay::readGameplaySettings(Json::parse(R"({"schemaVersion":1,"tactics":{"halfCoverMilli":500,"fullCoverMilli":400}})")).value);
}
void tacticsWeGo(){
    auto data=readTacticsScenario(Json::parse(R"({"grid":{"shape":"square","rows":["......","......","......"]},
        "rules":{"maxHitMilli":1000,"overwatchPenaltyMilli":0},
        "unitTypes":[{"id":1,"name":"u.fast","hp":3,"mobility":3,"aimMilli":1000,"damage":3,"range":[6,6],"sight":8,"initiative":12},
                     {"id":2,"name":"u.slow","hp":3,"mobility":3,"aimMilli":1000,"damage":3,"range":[6,6],"sight":8,"initiative":4}],
        "units":[{"id":1,"type":1,"team":1,"at":[0,0]},{"id":2,"type":2,"team":2,"at":[2,0]},{"id":3,"type":2,"team":1,"at":[1,2]},
                 {"id":4,"type":1,"team":2,"at":[5,1]},{"id":5,"type":2,"team":1,"at":[2,2]}]})"));
    const auto& sc=data.scenario;auto s=*tacticsBegin(sc,data.units);Random rng(9);TacticsOrders orders;auto at=[&](int32_t x,int32_t y){return tacticsIndex(sc.grid,{x,y});};
    // Unit 1 (initiative 12) and unit 2 (4) contest (1,0); allies 3 and 5 swap places head-on: both stop.
    CHECK(!tacticsSubmitOrders(sc,s,orders,1,{{2,at(1,1),0,false}}));                                  // not its unit
    CHECK(!tacticsSubmitOrders(sc,s,orders,1,{{1,at(1,1),0,true}}));                                   // overwatch + move
    CHECK(!tacticsSubmitOrders(sc,s,orders,1,{{1,at(5,0),0,false}}));                                  // beyond 1 AP of mobility
    CHECK(tacticsSubmitOrders(sc,s,orders,1,{{1,at(1,0),0,false},{3,at(3,2),0,false},{5,at(0,2),0,false}}));
    CHECK(!tacticsSubmitOrders(sc,s,orders,1,{}));                                                      // once per round
    CHECK(!tacticsResolveWeGo(sc,s,orders,1,rng));                                                      // incomplete
    CHECK(tacticsSubmitOrders(sc,s,orders,2,{{2,at(1,0),0,false}}));
    roundtrip(orders);
    auto r=tacticsResolveWeGo(sc,s,orders,1,rng);CHECK(r&&orders.teams.empty()&&s.round==2);
    CHECK(s.units.at(1).cell==at(1,0)&&s.units.at(2).cell==at(2,0)&&s.units.at(3).cell==at(1,2)&&s.units.at(5).cell==at(2,2));
    int bumps=0;for(const auto& e:r->events)if(e.kind==TacticsEventKind::Bumped)++bumps;CHECK(bumps==3);
    CHECK(!tacticsResolveWeGo(sc,s,orders,1,rng));                                                      // repeated sequence
    // Simultaneous fire: 1 and 2 shoot each other, so do 3 and 4: all four die, unit 5 wins it for team 1.
    CHECK(tacticsSubmitOrders(sc,s,orders,1,{{1,-1,2,false},{3,-1,4,false}})&&tacticsSubmitOrders(sc,s,orders,2,{{2,-1,1,false},{4,-1,3,false}}));
    r=tacticsResolveWeGo(sc,s,orders,2,rng);CHECK(r&&s.winner==1);
    for(int64_t id:{1,2,3,4})CHECK(!s.units.at(id).alive);
    CHECK(s.units.at(5).alive);roundtrip(s);
    // Overwatch in WeGo: a mover crossing an armed unit's line of fire is shot during movement.
    auto o=*tacticsBegin(sc,data.units);TacticsOrders ow;
    CHECK(tacticsSubmitOrders(sc,o,ow,1,{{1,-1,0,true}})&&tacticsSubmitOrders(sc,o,ow,2,{{2,at(3,1),0,false}}));
    r=tacticsResolveWeGo(sc,o,ow,1,rng);CHECK(r&&hasEvent(r->events,TacticsEventKind::ReactionShot)&&!o.units.at(2).alive);
}
void tacticsBotGames(const std::string& root){
    auto data=readTacticsScenario(Json::parseFile(root+"/examples/libraries/genre/cards-turns/skirmish.json"));const auto& sc=data.scenario;
    auto play=[&](uint64_t seed){
        Random rng(seed);auto s=*tacticsBegin(sc,data.units);uint64_t seq=0,h=0;
        for(uint32_t step=0;s.winner==kTacticsNoWinner;++step){
            CHECK(step<5000);auto unit=tacticsActiveUnit(s);auto team=s.units.at(unit).team;auto view=tacticsViewFor(sc,s,team);CHECK(view);
            auto goal=tacticsIndex(sc.grid,{team==1?sc.grid.width-2:1,sc.grid.height/2});
            auto c=bots::tacticsBotChoose(sc,*view,goal);CHECK(c&&c->evaluated<=512);
            auto r=stepTactics(sc,s,{++seq,team,unit,c->action},rng);if(!r)r=stepTactics(sc,s,{++seq,team,unit,{}},rng);CHECK(r);
            for(const auto& e:r->events)h^=tacticsHash(e)+seq;
        }
        return std::pair{s.winner,h^tacticsHash(s)};
    };
    int decided=0;for(uint64_t seed=1;seed<=6;++seed){auto a=play(seed),b=play(seed);CHECK((a==b));if(a.first>0)++decided;}
    CHECK(decided==6);
}
int main(int argc,char** argv){
    try{
        CHECK(argc==2);std::string root=argv[1];
        cardData(root);cardStackAndPriority();cardRulesVariants();cardHiddenInformation();cardBotGames(root);
        tacticsGrid();tacticsTurns();tacticsWeGo();tacticsBotGames(root);
        std::cout<<"Cards + turns: card data/rules variants, stack/priority/fizzle, hidden information, card bot games, tactics grid/cover/line of fire, AP/initiative/overwatch, WeGo, tactics bot games: OK\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
