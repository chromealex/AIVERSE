#include <aiverselib/strategy/Persistence.h>
#include <iostream>
#include <stdexcept>
using namespace aiverse::lib::strategy;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string(__FILE__)+":"+std::to_string(__LINE__)+": " #x);}while(false)
template<class T> void roundtrip(const T& s){auto b=save(s,7);CHECK(!b.empty());T copy;CHECK(load(b,copy,7));CHECK(save(copy,7)==b);CHECK(!load(b,copy,8));CHECK(!load(b+"x",copy,7));CHECK(!load(b.substr(0,b.size()-1),copy,7));CHECK(save(copy,7)==b);}
void numbers(){
    auto a=BigNumber::parse("999999999999999999999999999999999999");CHECK(a);auto b=a->plus(BigNumber{1});CHECK(b&&b->decimal()=="1000000000000000000000000000000000000");CHECK(b->minus(*a)==BigNumber{1});CHECK(!a->minus(*b));
    CHECK(!BigNumber::parse("1e20"));CHECK(!BigNumber::parse("-1"));CHECK(!BigNumber::parse(""));CHECK(BigNumber::parse("000")->zero());
    auto p=BigNumber{999999999}.times(BigNumber{999999999});CHECK(p&&p->decimal()=="999999998000000001");
    auto huge=BigNumber::parse(std::string(BigNumber::maxLimbs*9,'9'));CHECK(huge&&!huge->plus(BigNumber{1})&&!huge->times(BigNumber{2}));
    for(uint64_t i=0;i<1000;++i){BigNumber n{i*1234567},m{i+1};CHECK(n.times(m)->decimal()==std::to_string(i*1234567*(i+1)));uint32_t rest=0;CHECK(n.dividedBy(17,&rest)->decimal()==std::to_string(i*1234567/17));CHECK(rest==i*1234567%17);}
    CHECK(!a->dividedBy(0));roundtrip(*a);roundtrip(BigNumber{});
}
void offline(){
    ProductionLaw law{{{1,1},{0,1}},{0,1}};OfflineState fast{0,{0,0}},slow=fast;
    for(uint64_t t=1;t<=1000;++t)CHECK(advanceOffline(law,slow,t));
    CHECK(advanceOffline(law,fast,1000)&&fast==slow);CHECK(fast.amounts[0].decimal()=="499500");CHECK(fast.amounts[1].decimal()=="1000");roundtrip(fast);
    auto before=save(fast);CHECK(!advanceOffline(law,fast,999)&&save(fast)==before);CHECK(advanceOffline(law,fast,1000)&&save(fast)==before);CHECK(!advanceOffline(law,fast,2000,999)&&save(fast)==before);
    ProductionLaw doubling{{{2}},{0}};OfflineState growth{0,{1}};CHECK(advanceOffline(doubling,growth,100));CHECK(growth.amounts[0].decimal()=="1267650600228229401496703205376");
    auto initial=growth;CHECK(!advanceOffline(doubling,growth,100000)&&growth==initial);
}
void turns(){
    auto s=beginTurns({{10,1,20,3,0,true},{20,2,20,3,0,true},{30,1,5,3,0,true}});CHECK(s&&activeActor(*s)==10);auto allow=[](const auto&,const auto&){return true;};
    CHECK(queueAction(*s,1,{1,10,20,1,2,false},allow));auto saved=save(*s);CHECK(!queueAction(*s,2,{2,10,20,1,0,true},allow)&&save(*s)==saved);CHECK(!endTurn(*s,1));
    CHECK(queueAction(*s,2,{2,20,10,2,1,true},allow));roundtrip(*s);CHECK(resolveAction(*s)->actor==20);CHECK(resolveAction(*s)->actor==10);CHECK(s->actors.at(10).ap==1);
    CHECK(!queueAction(*s,1,{2,10,20,1,0,false},allow));CHECK(endTurn(*s,1)&&activeActor(*s)==20);CHECK(endTurn(*s,2)&&activeActor(*s)==30);CHECK(endTurn(*s,1)&&s->round==2&&s->actors.at(10).ap==3);
    auto copy=*s;CHECK(!queueAction(*s,1,{3,10,20,1,1,false},[](const auto&,const auto&){return false;})&&*s==copy);
    CHECK(!beginTurns({{1,1,1,1,0,false}}));CHECK(!beginTurns({{1,1,1,1,0,true},{1,2,1,1,0,true}}));
}
CardRules rules(){return {{1,{1,1,false,false,{{CardEffectKind::Damage,7,true}}}},{2,{2,1,true,false,{{CardEffectKind::Guard,5,false}}}},{3,{3,0,false,true,{{CardEffectKind::Draw,2,false}}}}};}
void cards(){
    auto r=rules();auto s=makeCards(r,{{3,1},{1,2}},1);CHECK(s);auto view=cardView(*s,0);CHECK(view&&view->seats[1].hand.empty()&&view->seats[1].handCount==1&&view->seats[0].hand.size()==1);CHECK(cardView(*s)->seats[0].hand.empty());
    auto attack=s->seats[0].zones[1].back(),response=s->seats[1].zones[1].back();CHECK(playCard(r,*s,0,attack,1,1));CHECK(playCard(r,*s,1,response,0,2));roundtrip(*s);
    CHECK(passCards(r,*s,0,3));CHECK(passCards(r,*s,1,4));CHECK(s->seats[1].guard==5&&s->stack.size()==1);
    CHECK(passCards(r,*s,0,5));CHECK(passCards(r,*s,1,6));CHECK(s->seats[1].health==28&&s->seats[1].guard==0&&s->stack.empty());CHECK(s->seats[0].zones[3].back()==attack);
    auto before=*s;CHECK(!playCard(r,*s,0,attack,1,7)&&*s==before);CHECK(!passCards(r,*s,0,6)&&*s==before);CHECK(nextCardTurn(*s,0,7)&&s->active==1);roundtrip(*s);
    auto overflow=makeCards(r,{{2},{2}},1);overflow->seats[0].guard=1000000;CHECK(playCard(r,*overflow,0,1,1,1));CHECK(passCards(r,*overflow,1,2));auto saved=*overflow;CHECK(!passCards(r,*overflow,0,3)&&*overflow==saved);
    auto shuffled=makeCards(r,{{1,2,3},{1}},0);CHECK(shuffled);before=*shuffled;CHECK(!shuffleDeck(*shuffled,0,[](uint32_t bound){return bound;})&&*shuffled==before);CHECK(shuffleDeck(*shuffled,0,[](uint32_t){return 0;}));CHECK(shuffled->seats[0].zones[0]!=before.seats[0].zones[0]);
}
void puzzles(){
    PushBoard b{5,3,std::vector<uint8_t>(15),{8}};PushState s{6,{7}};CHECK(valid(b,s));auto solution=solvePush(b,s);CHECK(solution.status==SolveStatus::Solved&&solution.moves==std::vector<int32_t>{1});
    PushHistory h{s,{},{}};CHECK(pushMove(b,h,1)&&pushSolved(b,h.current));roundtrip(h);CHECK(pushUndo(b,h)&&h.current==s);CHECK(pushRedo(b,h)&&pushSolved(b,h.current));
    CHECK(pushUndo(b,h));CHECK(pushMove(b,h,2)&&h.future.empty());
    b.walls[9]=1;PushState dead{1,{0}};CHECK(solvePush(b,dead).status==SolveStatus::Unsolvable);CHECK(solvePush(b,s,1).status==SolveStatus::BudgetExceeded);
    PushState edge{4,{7}};auto before=edge;CHECK(!pushMove(b,edge,1)&&edge==before);
}
void factory(){
    FactoryState s{0,{{1,{1,{10,0},{10,10}}},{2,{2,{0,0},{10,10}}},{3,{3,{0,0},{10,10}}}}};
    FactoryRules r{{{1,{{0,2}},{{1,1}}}},{{1,1,2,1,1},{2,2,3,1,1}}};CHECK(compatible(r,s));
    CHECK(stepFactory(r,s,1)&&s.nodes.at(1).stock[0]==8&&s.nodes.at(2).stock[1]==1&&s.nodes.at(3).stock[1]==0);CHECK(stepFactory(r,s,2)&&s.nodes.at(3).stock[1]==1);roundtrip(s);
    auto before=s;CHECK(!stepFactory(r,s,2)&&s==before);auto bad=r;bad.links[1].id=1;CHECK(!stepFactory(bad,s,3)&&s==before);
    FactoryState jam{0,{{1,{1,{2,10},{10,10}}}}};FactoryRules recipe{{{1,{{0,2}},{{1,1}}}}, {}};CHECK(stepFactory(recipe,jam,1)&&jam.nodes.at(1).stock[0]==2);
    FactoryState chain{0,{{1,{1,{5},{10}}},{2,{2,{0},{10}}},{3,{3,{0},{10}}}}};FactoryRules fan{{},{{1,1,2,0,4},{2,1,3,0,4}}};CHECK(stepFactory(fan,chain,1));CHECK(chain.nodes.at(2).stock[0]==4&&chain.nodes.at(3).stock[0]==1&&chain.nodes.at(1).stock[0]==0);
}
int main(){try{numbers();offline();turns();cards();puzzles();factory();std::cout<<"Strategy libraries: arithmetic, offline equivalence, reactions, hidden-card views, puzzles, flow and restore OK\n";}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
