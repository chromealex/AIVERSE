#include "BoardModule.h"
#include <algorithm>
#include <set>

namespace aiverse::genres {
namespace {
int number(const Json& j,const char* key,int fallback,int lo,int hi) {
    const auto& v=j[key];
    if(j.has(key)&&(!v.isNumber()||v.fx().raw%Fixed::kOne))throw JsonError(std::string("board.")+key+": expected integer");
    auto n=j.getInt(key,fallback);
    if(n<lo||n>hi)throw JsonError(std::string("board.")+key+": outside supported range");
    return int(n);
}
void shuffle(Simulation& sim,std::vector<int32_t>& values) {
    for(size_t n=values.size();n>1;--n)std::swap(values[n-1],values[sim.rng().below(uint32_t(n))]);
}
}
void io(Archive& a,CardPlayer& p) {a(p.health,p.guard,p.energy,p.deck,p.hand,p.discard);}
void io(Archive& a,BoardState& s) {a(s.players,s.cells,s.active,s.turn,s.winner,s.moves,s.score,s.plays,s.resolving);}
void BoardModule::setup(Simulation& sim,const Json& rules,const Json& map) {
    const auto& j=rules["board"];
    if(!j.isObject())throw JsonError("rules.board: expected object");
    auto kind=j.getStr("kind");
    if(kind!="cards"&&kind!="match3")throw JsonError("board.kind: expected cards or match3");
    kind_=kind=="cards"?BoardKind::Cards:BoardKind::Match3;
    BoardState s;cards_.clear();bots_.clear();
    auto playerCount=kind_==BoardKind::Cards?2u:1u;
    if(sim.players().size()!=playerCount)throw JsonError("board.players: wrong player count");
    for(size_t i=0;i<playerCount;++i){
        if(sim.players()[i].team!=i+1)throw JsonError("board.players: teams must match slots");
        bots_.push_back(map["players"][i].getBool("bot",false));
    }
    if(kind_==BoardKind::Cards) {
        hand_=number(j,"hand",5,1,20);energy_=number(j,"energy",3,1,100);
        maxTurns_=number(j,"maxTurns",200,1,10000);
        auto health=number(j,"health",30,1,100000);
        if(!j["cards"].isArray()||j["cards"].size()==0||j["cards"].size()>256)throw JsonError("board.cards: expected 1..256 definitions");
        std::set<std::string> ids;
        for(const auto& c:j["cards"].items()) {
            CardDef d;d.id=c.getStr("id");
            if(d.id.empty()||!ids.insert(d.id).second)throw JsonError("board.cards: empty or duplicate id");
            d.cost=number(c,"cost",1,0,100);d.damage=number(c,"damage",0,0,10000);
            d.guard=number(c,"guard",0,0,10000);d.draw=number(c,"draw",0,0,20);
            cards_.push_back(d);
        }
        const auto& decks=map["decks"];
        if(!decks.isArray()||decks.size()!=2)throw JsonError("board.decks: expected two decks");
        for(const auto& deck:decks.items()) {
            if(!deck.isArray()||deck.size()==0||deck.size()>256)throw JsonError("board.deck: expected 1..256 cards");
            CardPlayer p;p.health=health;p.energy=energy_;
            for(const auto& card:deck.items()) {
                auto it=std::find_if(cards_.begin(),cards_.end(),[&](const auto& d){return card.isString()&&d.id==card.str();});
                if(it==cards_.end())throw JsonError("board.deck: unknown card");
                p.deck.push_back(int32_t(it-cards_.begin()));
            }
            shuffle(sim,p.deck);draw(sim,p,hand_);s.players.push_back(std::move(p));
        }
    } else {
        width_=number(j,"width",6,3,32);height_=number(j,"height",6,3,32);
        colors_=number(j,"colors",5,3,12);goal_=number(j,"goal",100,1,10000000);
        s.moves=number(j,"moves",20,1,10000);
        const auto& cells=map["cells"];
        if(!cells.isArray()||cells.size()!=size_t(width_*height_))throw JsonError("board.cells: expected width * height colors");
        for(const auto& cell:cells.items()) {
            if(!cell.isNumber()||cell.fx().raw%Fixed::kOne||cell.integer()<1||cell.integer()>colors_)throw JsonError("board.cells: invalid color");
            s.cells.push_back(int32_t(cell.integer()));
        }
        if(!matches(s.cells).empty()||!hasMove(s))throw JsonError("board.cells: initial board must have a legal move and no existing matches");
    }
    Hasher h;h.str(rules.dump(-1));h.str(map.dump(-1));contentHash_=h.value();
    sim.world().setResource<BoardState>(std::move(s));
    sim.addSystem("board",[this](Simulation& world){tick(world);});
}
void BoardModule::draw(Simulation& sim,CardPlayer& p,int count) {
    for(int i=0;i<count&&p.hand.size()<20;++i) {
        if(p.deck.empty()){p.deck=std::move(p.discard);p.discard.clear();shuffle(sim,p.deck);}
        if(p.deck.empty())return;
        p.hand.push_back(p.deck.back());p.deck.pop_back();
    }
}
void BoardModule::endTurn(Simulation& sim) {
    auto& s=state(sim);auto& old=s.players[size_t(s.active)];
    old.discard.insert(old.discard.end(),old.hand.begin(),old.hand.end());old.hand.clear();
    if(s.turn>=maxTurns_){s.winner=-1;return;}
    ++s.turn;s.active=1-s.active;s.plays=0;
    auto& p=s.players[size_t(s.active)];p.energy=energy_;p.guard=0;
    draw(sim,p,hand_-int(p.hand.size()));
}
void BoardModule::applyCommand(Simulation& sim,const Command& c) {
    auto& s=state(sim);
    if(s.winner||c.player==0||c.player>sim.players().size()||!c.units.empty()||!c.args.empty()||c.target.valid())return;
    if(kind_==BoardKind::Cards) {
        if(c.player!=s.active+1)return;
        if(c.type==EndTurn){endTurn(sim);return;}
        if(c.type!=PlayCard)return;
        auto& p=s.players[size_t(s.active)];auto& enemy=s.players[size_t(1-s.active)];
        if(c.param<0||size_t(c.param)>=p.hand.size())return;
        auto id=p.hand[size_t(c.param)];const auto& d=cards_[size_t(id)];
        if(p.energy<d.cost||s.plays>=64)return;
        ++s.plays;
        p.energy-=d.cost;p.hand.erase(p.hand.begin()+c.param);p.discard.push_back(id);
        auto blocked=std::min(enemy.guard,d.damage);enemy.guard-=blocked;
        enemy.health=std::max(0,enemy.health-d.damage+blocked);
        p.guard=std::min(100000,p.guard+d.guard);draw(sim,p,d.draw);
        sim.emit({PlayCard,c.player,{},{},id,{}});
        if(!enemy.health)s.winner=c.player;
    } else {
        if(c.type!=SwapCells||c.player!=1||s.resolving||c.param<0||c.param>=int64_t(s.cells.size())||c.point.x.raw%Fixed::kOne)return;
        auto other=c.point.x.floorInt();
        if(other<0||other>=int64_t(s.cells.size())||!legalSwap(s,int(c.param),int(other)))return;
        std::swap(s.cells[size_t(c.param)],s.cells[size_t(other)]);--s.moves;s.resolving=true;
    }
}
std::vector<int> BoardModule::matches(const std::vector<int32_t>& cells) const {
    std::vector<bool> marked(cells.size(),false);
    for(int y=0;y<height_;++y)for(int x=0;x<width_;++x) {
        int at=y*width_+x,color=cells[size_t(at)];if(!color)continue;
        if(x+2<width_&&cells[size_t(at+1)]==color&&cells[size_t(at+2)]==color)
            marked[size_t(at)]=marked[size_t(at+1)]=marked[size_t(at+2)]=true;
        if(y+2<height_&&cells[size_t(at+width_)]==color&&cells[size_t(at+2*width_)]==color)
            marked[size_t(at)]=marked[size_t(at+width_)]=marked[size_t(at+2*width_)]=true;
    }
    std::vector<int> out;for(size_t i=0;i<marked.size();++i)if(marked[i])out.push_back(int(i));return out;
}
bool BoardModule::legalSwap(const BoardState& s,int a,int b) const {
    if(s.cells.size()!=size_t(width_*height_)||a<0||b<0||size_t(a)>=s.cells.size()||size_t(b)>=s.cells.size()||s.moves<=0||s.winner||s.resolving)return false;
    if(std::abs(a/width_-b/width_)+std::abs(a%width_-b%width_)!=1)return false;
    auto cells=s.cells;std::swap(cells[size_t(a)],cells[size_t(b)]);
    auto result=matches(cells);
    return std::find(result.begin(),result.end(),a)!=result.end()||std::find(result.begin(),result.end(),b)!=result.end();
}
bool BoardModule::hasMove(const BoardState& s) const {
    for(int a=0;a<int(s.cells.size());++a) {
        if(a%width_+1<width_&&legalSwap(s,a,a+1))return true;
        if(a+width_<int(s.cells.size())&&legalSwap(s,a,a+width_))return true;
    }
    return false;
}
void BoardModule::finishPuzzle(BoardState& s) const {
    if(s.score>=goal_)s.winner=1;
    else if(s.moves<=0||!hasMove(s))s.winner=-1;
}
void BoardModule::tick(Simulation& sim) {
    auto& s=state(sim);if(s.winner)return;
    if(kind_==BoardKind::Cards) {
        if(!bots_[size_t(s.active)]||sim.tick()%10)return;
        auto& p=s.players[size_t(s.active)];Command c;c.player=uint8_t(s.active+1);c.type=EndTurn;
        for(size_t i=0;i<p.hand.size();++i)if(s.plays<64&&cards_[size_t(p.hand[i])].cost<=p.energy){c.type=PlayCard;c.param=int64_t(i);break;}
        applyCommand(sim,c);
    } else if(s.resolving) {
        auto found=matches(s.cells);
        if(found.empty()){s.resolving=false;finishPuzzle(s);return;}
        s.score=std::min(10000000,s.score+int(found.size())*10);
        for(auto i:found)s.cells[size_t(i)]=0;
        for(int x=0;x<width_;++x) {
            int write=height_-1;
            for(int y=height_-1;y>=0;--y)if(s.cells[size_t(y*width_+x)])s.cells[size_t((write--)*width_+x)]=s.cells[size_t(y*width_+x)];
            for(;write>=0;--write)s.cells[size_t(write*width_+x)]=int32_t(sim.rng().below(uint32_t(colors_)))+1;
        }
        sim.emit({SwapCells,1,{},{},int64_t(found.size()),{}});
        if(s.score>=goal_){s.resolving=false;s.winner=1;}
    } else if(bots_[0]&&sim.tick()%10==0) {
        for(int a=0;a<int(s.cells.size());++a)for(auto b:{a+1,a+width_})if(legalSwap(s,a,b)) {
            Command c;c.player=1;c.type=SwapCells;c.param=a;c.point.x=Fixed::fromInt(b);applyCommand(sim,c);return;
        }
        finishPuzzle(s);
    }
}
void BoardModule::hashState(Simulation& sim,Hasher& h) {
    std::string bytes;Archive a(&bytes);a(state(sim));h.u64(contentHash_);h.str(bytes);
}
bool BoardModule::serializeState(Simulation& sim,Archive& a) {
    uint32_t version=1;a(version);if(version!=1){a.fail();return false;}
    if(a.writing()){a(state(sim));return a.ok();}
    BoardState loaded;a(loaded);if(!a.ok())return false;
    bool valid=loaded.winner>=-1&&loaded.winner<=int(bots_.size())&&loaded.turn>=1&&loaded.turn<=maxTurns_&&loaded.plays>=0&&loaded.plays<=64;
    if(kind_==BoardKind::Cards) {
        valid=valid&&loaded.players.size()==2&&loaded.cells.empty()&&loaded.active>=0&&loaded.active<2;
        for(const auto& p:loaded.players) {
            valid=valid&&p.health>=0&&p.health<=100000&&p.guard>=0&&p.guard<=100000&&p.energy>=0&&p.energy<=energy_;
            valid=valid&&p.hand.size()<=20&&p.hand.size()+p.deck.size()+p.discard.size()<=256;
            for(const auto* zone:{&p.hand,&p.deck,&p.discard})for(auto card:*zone)valid=valid&&card>=0&&size_t(card)<cards_.size();
        }
    }else {
        valid=valid&&loaded.players.empty()&&loaded.cells.size()==size_t(width_*height_)&&loaded.moves>=0&&loaded.moves<=10000&&loaded.score>=0&&loaded.score<=10000000;
        for(auto cell:loaded.cells)valid=valid&&cell>=1&&cell<=colors_;
    }
    if(!valid){a.fail();return false;}
    state(sim)=std::move(loaded);return true;
}
}
