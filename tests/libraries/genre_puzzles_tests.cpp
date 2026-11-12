// puzzle-solvers tests: match-3, flood/pipes, sliding/blocks/packing, Sokoban (grid-puzzles 0.2.0), constraint
// puzzles, JSON data errors, persistence (record kinds 112..119) and the puzzle bot. Usage: <exe> <repo root>
#if defined(__GNUC__)&&!defined(__clang__)
#pragma GCC diagnostic ignored "-Wmisleading-indentation" // compact one-line checks below
#endif
#include <aiverselib/bots/PuzzleBot.h>
#include <iostream>
#include <stdexcept>
#include <string>
using namespace aiverse;using namespace aiverse::lib::genre;
namespace strategy=aiverse::lib::strategy;namespace bots=aiverse::lib::bots;
#define CHECK(x) do{if(!(x)){throw std::runtime_error(std::string(__FILE__)+":"+std::to_string(__LINE__)+": " #x);}}while(false)
template<class State> void roundtrip(const State& s){
    auto bytes=save(s);CHECK(!bytes.empty());State copy;CHECK(load(bytes,copy));CHECK(save(copy)==bytes);CHECK(puzzleHash(copy)==puzzleHash(s));
    CHECK(!load(bytes+"x",copy));CHECK(!load(bytes.substr(0,bytes.size()-1),copy));
    auto wrong=bytes;wrong[4]=char(wrong[4]+1);CHECK(!load(wrong,copy));
}
template<class F> void throwsWith(F&& f,const std::string& needle){
    try{f();}catch(const JsonError& e){if(std::string(e.what()).find(needle)==std::string::npos)throw std::runtime_error("unexpected error: "+std::string(e.what())+" (wanted "+needle+")");return;}
    throw std::runtime_error("expected JsonError containing "+needle);
}
Json levels(const std::string& root){return Json::parseFile(root+"/examples/libraries/genre/puzzles/levels.json");}
std::vector<Match3Tile> board(const std::vector<std::string>& rows){
    std::vector<Match3Tile> t;for(const auto& r:rows)for(char c:r)t.push_back(c=='#'?Match3Tile{kMatch3Blocked,Match3Special::None}:Match3Tile{uint8_t(c-'0'),Match3Special::None});return t;
}
void common(){
    std::vector<uint8_t> g{1,1,0,2, 0,1,0,2, 3,0,2,2};
    auto fill=puzzleFloodFill(4,3,0,[&](int32_t c){return g[size_t(c)]==1;});CHECK((fill==std::vector<int32_t>{0,1,5}));
    CHECK(puzzleFloodFill(4,3,2,[&](int32_t c){return g[size_t(c)]==1;}).empty());
    auto r=puzzleRegions(4,3,g);CHECK(r.count==3&&r.regions[0]==1&&r.regions[3]==2&&r.regions[8]==3&&r.regions[2]==0&&(r.sizes==std::vector<int32_t>{3,4,1}));
    CHECK(puzzleStep(4,3,3,1)==-1&&puzzleStep(4,3,4,0)==-1&&puzzleStep(4,3,5,3)==9);
    PuzzleBudget b{2,0};CHECK(b.take()&&b.take()&&!b.take()&&b.exhausted());
}
void match3Rules(){
    Match3Rules r;r.width=6;r.height=6;r.colors=4;
    r.specials={{Match3Shape::Line5,Match3Special::ColorBomb,false},{Match3Shape::Corner,Match3Special::Bomb,false},{Match3Shape::Line4,Match3Special::StripeColumn,true}};
    CHECK(valid(r));
    // Detection: a line of 4, an L and a T, ordered by lowest cell.
    auto t=board({"111123","234342","343423","422234","232342","343434"});
    auto groups=match3Groups(r,t);CHECK(groups.size()==2);
    CHECK(groups[0].shape==Match3Shape::Line4&&groups[0].horizontal&&(groups[0].cells==std::vector<int32_t>{0,1,2,3}));
    CHECK(groups[1].shape==Match3Shape::Line4||groups[1].shape==Match3Shape::None);
    auto l=board({"123412","143234","111342","234123","342341","413412"});
    auto lg=match3Groups(r,l);CHECK(lg.size()==1&&lg[0].shape==Match3Shape::Corner&&lg[0].crossing==12&&lg[0].cells.size()==5);
    auto tt=board({"232342","111234","212423","314341","423412","234123"});
    auto tg=match3Groups(r,tt);CHECK(tg.size()==1&&tg[0].shape==Match3Shape::Corner&&tg[0].crossing==7);
    auto five=board({"111112","234342","343423","422341","232342","343434"});CHECK(match3Groups(r,five)[0].shape==Match3Shape::Line5);
    // Swap validation: adjacency, holes and the need for a match.
    auto s=board({"121342","234123","243414","312341","421232","134123"});
    CHECK(match3Groups(r,s).empty());
    CHECK(match3Legal(r,s,0,1));                 // column 0 becomes 2,2,2
    CHECK(!match3Legal(r,s,0,2)&&!match3Legal(r,s,5,6)&&!match3Legal(r,s,0,0));
    CHECK(!match3Legal(r,s,14,15));
    auto moves=match3Moves(r,s);CHECK(!moves.empty()&&moves.front()==std::pair(0,1));
    for(size_t i=1;i<moves.size();++i)CHECK(moves[i-1]<moves[i]);
    // Holes never take tiles and break runs.
    r.holes.assign(36,0);r.holes[3]=1;auto h=s;h[3]={kMatch3Blocked,Match3Special::None};CHECK(!match3Legal(r,h,2,3));r.holes.clear();
}
void match3Resolution(){
    Match3Level level;auto& r=level.rules;r.width=6;r.height=6;r.colors=4;
    r.specials={{Match3Shape::Line5,Match3Special::ColorBomb,false},{Match3Shape::Corner,Match3Special::Bomb,false},{Match3Shape::Line4,Match3Special::StripeColumn,true}};
    level.goals.moveLimit=10;level.goals.targetScore=100000;
    // Swapping cell 8 up into cell 2 completes "1111": a perpendicular stripe spawns at the swap destination.
    level.start=board({"112134","231423","342312","423141","234234","341321"});
    Random rng(5,1);auto st=match3Begin(level,rng);CHECK(st);
    CHECK(!match3Apply(level,*st,{0,8,2},rng));                       // move numbers start above 0
    auto before=*st;CHECK(!match3Apply(level,*st,{1,0,2},rng)&&*st==before); // not adjacent: untouched
    auto rep=match3Apply(level,*st,{1,8,2},rng);CHECK(rep);
    CHECK(rep->created.size()>=1&&rep->created[0]==std::pair(2,Match3Special::StripeColumn));
    CHECK(st->moves==1&&st->lastMove==1&&rep->cleared>=3&&st->score>=30&&st->collected[1]>=3);
    CHECK(!match3Apply(level,*st,{1,0,1},rng));                       // stale move number
    for(const auto& t:st->tiles)CHECK(match3Colored(t));             // gravity + refill leave no gaps
    CHECK(match3Groups(r,st->tiles).empty());                        // cascades ran to a stable board
    // Same seed, same commands: identical boards (refill draws in fixed order).
    Random a(9,2),b(9,2);auto sa=match3Begin(level,a),sb=match3Begin(level,b);CHECK(sa&&sb);
    CHECK(match3Apply(level,*sa,{1,8,2},a)&&match3Apply(level,*sb,{1,8,2},b)&&*sa==*sb);
    // A stripe in a cleared run clears its whole column; a color bomb swap clears every tile of that color.
    Match3State bombState=*sb;bombState.tiles=board({"121342","234123","243414","312341","421232","134123"});
    bombState.tiles[14].special=Match3Special::ColorBomb;
    int32_t colorFour=0;for(const auto& t:bombState.tiles)colorFour+=t.color==4;
    auto beforeFour=bombState.collected[4];Random rb(3,3);
    auto br=match3Apply(level,bombState,{bombState.lastMove+1,14,15},rb);CHECK(br);
    CHECK(int32_t(bombState.collected[4]-beforeFour)>=colorFour);
    Match3State stripe=*sb;stripe.tiles=board({"121342","234123","243414","312341","421232","134123"});
    stripe.tiles[1]={2,Match3Special::StripeRow};                     // swapped into column 0's run: clears row 0 too
    Random rs(4,4);auto sr=match3Apply(level,stripe,{stripe.lastMove+1,1,0},rs);CHECK(sr&&sr->cleared>=8);
    // Goals: winning and losing.
    Match3Level easy=level;easy.goals.targetScore=10;Random re(1,1);auto se=match3Begin(easy,re);CHECK(se);
    auto hint=match3Hint(easy,*se,Match3HintMode::First);CHECK(hint);CHECK(match3Apply(easy,*se,{1,hint->first,hint->second},re)->status==Match3Status::Won);
    CHECK(!match3Apply(easy,*se,{2,hint->first,hint->second},re));
    Match3Level hard=level;hard.goals.moveLimit=1;Random rh(1,1);auto sh=match3Begin(hard,rh);hint=match3Hint(hard,*sh);CHECK(hint);
    CHECK(match3Apply(hard,*sh,{7,hint->first,hint->second},rh)->status==Match3Status::Lost&&sh->lastMove==7);
    roundtrip(*sh);CHECK(match3Compatible(hard.rules,*sh));
}
void match3Shuffles(){
    Match3Rules r;r.width=5;r.height=5;r.colors=3;CHECK(valid(r));
    // A dead board (no run, no legal swap).
    auto dead=board({"12312","23123","31231","12312","23123"});CHECK(match3Groups(r,dead).empty()&&!match3HasMove(r,dead));
    for(uint64_t seed=1;seed<=40;++seed){
        Random rng(seed,7);auto t=dead;CHECK(match3Shuffle(r,t,rng));CHECK(match3Groups(r,t).empty()&&match3HasMove(r,t));
    }
    r.shuffleAttempts=0;Random rng(3,3);auto t=dead;CHECK(match3Shuffle(r,t,rng));CHECK(match3Groups(r,t).empty()&&match3HasMove(r,t));
    // Holes everywhere except a 1x2 strip: no shape guarantees a legal move -> invalid rules.
    Match3Rules tiny;tiny.width=2;tiny.height=2;tiny.colors=3;CHECK(!valid(tiny));
    // Generated boards: no runs, a move, deterministic per seed.
    Match3Rules g;g.width=8;g.height=8;g.colors=6;g.holes.assign(64,0);g.holes[0]=g.holes[63]=1;
    for(uint64_t seed=1;seed<=20;++seed){Random a(seed,1),b(seed,1);auto x=match3Generate(g,a),y=match3Generate(g,b);CHECK(x==y&&match3Groups(g,x).empty()&&match3HasMove(g,x)&&x[0].color==kMatch3Blocked);}
    // Best hint prefers a bigger preview; ties go to the earlier swap.
    Match3Level level;level.rules.width=6;level.rules.height=6;level.rules.colors=4;
    level.start=board({"112134","231423","342312","423141","234234","341321"});Random rl(2,2);auto s=match3Begin(level,rl);CHECK(s);
    auto first=match3Hint(level,*s,Match3HintMode::First),best=match3Hint(level,*s,Match3HintMode::Best);CHECK(first&&best);
    auto all=match3Moves(level.rules,s->tiles);CHECK(*first==all.front()&&std::find(all.begin(),all.end(),*best)!=all.end());
}
void flood(const Json& data){
    auto l=readFloodLevel(data["flood"]);auto s=puzzleFloodBegin(l);CHECK(puzzleFloodCompatible(l,s));
    auto sol=puzzleFloodSolve(l,s,{200000,0});CHECK(sol.status==PuzzleSearch::Solved&&sol.colors.size()<=l.moveLimit);
    auto copy=s;for(auto c:sol.colors)CHECK(puzzleFloodApply(l,copy,{copy.lastMove+1,c}));CHECK(copy.status==PuzzleFloodStatus::Won);roundtrip(copy);
    CHECK(puzzleFloodSolve(l,s,{1,0}).status==PuzzleSearch::Unknown);
    auto tight=l;tight.moveLimit=2;CHECK(puzzleFloodSolve(tight,puzzleFloodBegin(tight)).status==PuzzleSearch::Unsolvable);
    CHECK(!puzzleFloodApply(l,s,{1,s.cells[0]}));CHECK(!puzzleFloodApply(l,s,{1,9}));
    auto hint=puzzleFloodHint(l,s,{200000,0});CHECK(hint&&*hint==sol.colors.front());
    auto greedy=puzzleFloodHint(l,s,{1,0});CHECK(greedy);
    PuzzleFloodLevel small{2,2,2,{1,2,2,1},0,1};auto ss=puzzleFloodBegin(small);CHECK(puzzleFloodApply(small,ss,{5,2})==2u);CHECK(ss.status==PuzzleFloodStatus::Lost&&ss.lastMove==5);
}
void pipes(const Json& data){
    auto [l,rotation]=readPipeLevel(data["pipes"]);auto s=puzzlePipeBegin(l,rotation);CHECK(!s.solved&&puzzlePipeCompatible(l,s));
    CHECK(puzzlePipeTurn(1,1)==4&&puzzlePipeTurn(4,1)==2&&puzzlePipeTurn(3,1)==12&&puzzlePipeTurn(9,4)==9);
    auto sol=puzzlePipeSolve(l,{100000,0});CHECK(sol.status==PuzzleSearch::Solved&&puzzlePipeSolved(l,sol.rotation));
    for(int i=0;i<20&&!s.solved;++i){auto h=puzzlePipeHint(l,s);CHECK(h);CHECK(puzzlePipeApply(l,s,*h));}
    CHECK(s.solved);roundtrip(s);
    auto reach=puzzlePipeReach(l,s.rotation);CHECK(reach[0]&&reach[8]&&!reach[2]);
    auto s2=puzzlePipeBegin(l,rotation);CHECK(!puzzlePipeApply(l,s2,{1,2,1}));CHECK(!puzzlePipeApply(l,s2,{0,1,1}));CHECK(!puzzlePipeApply(l,s2,{1,1,4}));
    CHECK(puzzlePipeSolve(l,{1,0}).status==PuzzleSearch::Unknown);
    PuzzlePipeLevel broken{2,1,{2,0},{kPuzzlePipeSource,kPuzzlePipeSink},false};CHECK(puzzlePipeSolve(broken).status==PuzzleSearch::Unsolvable);
    // Leaks: a T piece next to the sink leaves an open end.
    PuzzlePipeLevel leak{2,1,{2,1|4},{kPuzzlePipeSource,kPuzzlePipeSink},true};CHECK(!puzzlePipeSolved(leak,{0,0}));leak.noLeaks=false;CHECK(puzzlePipeSolved(leak,{0,0}));
}
void tiles(const Json& data){
    auto s=readTilesLevel(data["tiles"]);CHECK(!s.solved);
    CHECK(puzzleTilesSolvable(3,3,{1,2,3,4,5,6,7,8,0})&&!puzzleTilesSolvable(3,3,{2,1,3,4,5,6,7,8,0}));
    CHECK(puzzleTilesSolvable(4,4,{1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,0})&&!puzzleTilesSolvable(4,4,{1,2,3,4,5,6,7,8,9,10,11,12,13,15,14,0}));
    CHECK(puzzleTilesSolvable(4,4,{1,2,3,4,5,6,7,8,9,10,11,12,13,14,0,15}));
    CHECK(puzzleTilesSolvable(4,4,{1,2,3,4,5,6,7,8,9,10,11,0,13,14,15,12}));
    for(uint64_t seed=1;seed<=200;++seed){Random rng(seed,5);auto t=puzzleTilesShuffle(4,4,rng);CHECK(puzzleTilesSolvable(4,4,t));}
    auto sol=puzzleTilesSolve(s,{200000,0});CHECK(sol.status==PuzzleSearch::Solved);
    auto copy=s;for(auto c:sol.cells)CHECK(puzzleTilesApply(copy,{copy.lastMove+1,c}));CHECK(copy.solved);roundtrip(copy);
    CHECK(puzzleTilesSolve(s,{1,0}).status==PuzzleSearch::Unknown);
    PuzzleTilesState bad{3,3,{2,1,3,4,5,6,7,8,0}};CHECK(puzzleTilesSolve(bad).status==PuzzleSearch::Unsolvable);CHECK(!puzzleTilesBegin(3,3,bad.tiles));
    auto t=s;CHECK(!puzzleTilesApply(t,{1,0}));                         // not next to the blank
    CHECK(puzzleTilesHint(s,{200000,0})==sol.cells.front());
    Random rng(77,1);auto shuffled=puzzleTilesBegin(3,3,puzzleTilesShuffle(3,3,rng));CHECK(shuffled);
    CHECK(puzzleTilesSolve(*shuffled,{500000,0}).status==PuzzleSearch::Solved);
}
void blocks(const Json& data){
    auto l=readBlocksLevel(data["blocks"]);CHECK(l.blocks.size()==5&&l.blocks[0].width==2&&l.blocks[0].height==2);
    auto s=puzzleBlocksBegin(l);auto sol=puzzleBlocksSolve(l,s.positions,{100000,0});CHECK(sol.status==PuzzleSearch::Solved);
    for(auto [b,d]:sol.moves)CHECK(puzzleBlocksApply(l,s,{s.lastMove+1,b,d}));CHECK(s.solved&&s.positions[0]==l.goal);roundtrip(s);
    auto t=puzzleBlocksBegin(l);CHECK(!puzzleBlocksApply(l,t,{1,0,0}));CHECK(!puzzleBlocksApply(l,t,{1,1,2}));
    CHECK(puzzleBlocksSolve(l,t.positions,{1,0}).status==PuzzleSearch::Unknown);
    PuzzleBlocksLevel boxed{3,1,{0,1,0},{{1,1,true}},{0},0,2};CHECK(puzzleBlocksSolve(boxed,boxed.start).status==PuzzleSearch::Unsolvable);
}
void packing(const Json& data){
    auto l=readPackLevel(data["packing"]);auto sol=puzzlePackSolve(l,{},2,{200000,0});CHECK(sol.status==PuzzleSearch::Solved&&sol.count==2);
    auto orient=puzzlePieceOrientations(l.pieces[0]);CHECK(orient.size()==4);CHECK(puzzlePieceOrientations(l.pieces[2]).size()==1);
    PuzzlePieceDef s{{{0,0},{1,0},{1,1},{2,1}},true,true};CHECK(puzzlePieceOrientations(s).size()==4);s.flip=false;CHECK(puzzlePieceOrientations(s).size()==2);
    PuzzlePackState st;for(int i=0;i<10&&!st.solved;++i){auto h=puzzlePackHint(l,st);CHECK(h);CHECK(puzzlePackApply(l,st,*h));}
    CHECK(st.solved&&st.placed.size()==4);roundtrip(st);
    PuzzlePackState wrong;CHECK(puzzlePackApply(l,wrong,{1,PuzzlePackAction::Place,{1,0,0,2}}));
    CHECK(!puzzlePackApply(l,wrong,{2,PuzzlePackAction::Place,{1,0,0,0}}));                         // already placed
    CHECK(!puzzlePackApply(l,wrong,{2,PuzzlePackAction::Place,{3,0,3,0}}));                         // off the board
    CHECK(puzzlePackApply(l,wrong,{2,PuzzlePackAction::Place,{2,0,1,0}}));                          // leaves (0,0),(0,1) + (3,*) open
    for(int i=0;i<12&&!wrong.solved;++i){auto h=puzzlePackHint(l,wrong);CHECK(h);CHECK(puzzlePackApply(l,wrong,*h));}
    CHECK(wrong.solved);
    PuzzlePackLevel odd{2,2,{1,1,1,1},{{{{0,0},{1,0},{2,0}},true,false},{{{0,0}},true,false}},true};CHECK(puzzlePackSolve(odd).status==PuzzleSearch::Unsolvable);
    CHECK(puzzlePackSolve(l,{},2,{1,0}).status==PuzzleSearch::Unknown);
}
void constraints(const Json& data){
    auto sudoku=readSudoku(data["sudoku"]);auto u=puzzleCspUnique(sudoku,{200000,0});CHECK(u.status==PuzzleSearch::Solved&&u.unique);
    CHECK(puzzleCspComplete(sudoku,u.values)&&u.values[2]==4&&u.values[80]==9);
    auto open=sudoku;open.domains[0]=open.domains[1]=0x3FE;auto two=puzzleCspUnique(open,{200000,0});CHECK(two.status==PuzzleSearch::Solved&&!two.unique);
    CHECK(puzzleCspSolve(sudoku,{},1,{3,0}).status==PuzzleSearch::Unknown);
    auto s=puzzleCspBegin(sudoku);CHECK(!s.solved&&s.values[0]==5&&s.values[2]==-1);
    CHECK(!puzzleCspApply(sudoku,s,{1,0,1}));                               // given
    CHECK(!puzzleCspApply(sudoku,s,{1,2,0}));                               // outside the domain
    CHECK(puzzleCspApply(sudoku,s,{1,2,5}));CHECK(!puzzleCspViolations(sudoku,s.values).empty());
    auto h=puzzleCspHint(sudoku,s);CHECK(h&&h->cell==2&&h->value==4);       // repairs the wrong value first
    CHECK(puzzleCspApply(sudoku,s,*h)&&puzzleCspViolations(sudoku,s.values).empty());
    CHECK(!puzzleCspApply(sudoku,s,{h->move,3,6}));                          // stale move number
    roundtrip(s);
    auto nono=readNonogram(data["nonogram"]);auto nu=puzzleCspUnique(nono,{100000,0});CHECK(nu.status==PuzzleSearch::Solved&&nu.unique);
    CHECK(nu.values[0]==0&&nu.values[1]==1&&nu.values[22]==1&&nu.values[24]==0);
    auto generic=readCsp(data["csp"]);auto g=puzzleCspUnique(generic);CHECK(g.unique&&(g.values==std::vector<int8_t>{1,2,3}));
    PuzzleCsp impossible{2,1,{2u|4u,2u|4u},{{PuzzleConstraintKind::Sum,{0,1},5,0,{}},{PuzzleConstraintKind::AllDifferent,{0,1},0,0,{}},{PuzzleConstraintKind::Count,{0,1},1,1,{}}}};
    CHECK(valid(impossible)&&puzzleCspSolve(impossible).status==PuzzleSearch::Unsolvable);
    PuzzleCsp line{5,1,std::vector<uint32_t>(5,3u),{{PuzzleConstraintKind::Runs,{0,1,2,3,4},1,0,{3}}}};
    std::vector<uint32_t> d=line.domains;PuzzleBudget pb{100,0};CHECK(detail_csp::propagate(line,d,pb)==1&&d[2]==2u&&d[0]==3u);
    line.constraints[0].runs={2,2};d=line.domains;CHECK(detail_csp::propagate(line,d,pb)==1&&d[0]==2u&&d[2]==1u&&d[4]==2u);
    line.constraints[0].runs={3,2};CHECK(!valid(line));
}
void sokoban(const Json& data){
    // grid-puzzles 0.2.0: dead cells, freeze deadlocks, A* and hints.
    strategy::PushBoard b{6,5,{1,1,1,1,1,1, 1,0,0,0,0,1, 1,0,0,0,0,1, 1,0,0,0,0,1, 1,1,1,1,1,1},{14}};
    auto dead=strategy::pushDeadCells(b);CHECK(dead.size()==30&&dead[7]&&dead[10]&&dead[19]&&dead[22]&&!dead[14]&&!dead[15]&&dead[13]&&dead[8]&&dead[20]);
    CHECK(strategy::pushDeadlocked(b,{13,{7}})&&!strategy::pushDeadlocked(b,{8,{15}}));
    strategy::PushBoard two{6,5,b.walls,{14,15}};
    CHECK(strategy::pushDeadlocked(two,{9,{19,20}}));                        // dead row along the bottom wall
    strategy::PushBoard wide{7,5,{1,1,1,1,1,1,1, 1,0,0,0,0,0,1, 1,0,0,0,0,0,1, 1,0,0,0,0,0,1, 1,1,1,1,1,1,1},{8,16,26}};
    CHECK(strategy::pushDeadlocked(wide,{17,{9,10,24}}));                    // boxes (2,1),(3,1) freeze each other
    auto level=readSokobanLevel(data["sokoban"]);
    auto a=strategy::solvePushAStar(level.board,level.start,100000);CHECK(a.status==strategy::SolveStatus::Solved);
    auto bfs=strategy::solvePush(level.board,level.start,100000);CHECK(bfs.status==strategy::SolveStatus::Solved&&bfs.moves.size()==a.moves.size());
    CHECK(strategy::solvePushAStar(level.board,level.start,1).status==strategy::SolveStatus::BudgetExceeded);
    CHECK(strategy::solvePushAStar(b,{13,{7}}).status==strategy::SolveStatus::Unsolvable);
    CHECK(strategy::pushHint(level.board,level.start)==a.moves.front());
    auto s=puzzleSokobanBegin(level);
    for(auto d:a.moves)CHECK(puzzleSokobanApply(level,s,{s.lastMove+1,PuzzleSokobanAction::Move,d}));CHECK(s.solved);
    CHECK(!puzzleSokobanApply(level,s,{s.lastMove+1,PuzzleSokobanAction::Move,0}));
    CHECK(puzzleSokobanApply(level,s,{s.lastMove+1,PuzzleSokobanAction::Undo,0})&&!s.solved&&s.undos==1);
    CHECK(puzzleSokobanApply(level,s,{s.lastMove+1,PuzzleSokobanAction::Redo,0})&&s.solved);
    CHECK(!puzzleSokobanApply(level,s,{s.lastMove,PuzzleSokobanAction::Undo,0}));roundtrip(s);
    // Push a box into a dead corner: the hint backs out with Undo.
    auto t=puzzleSokobanBegin(level);
    // Walk round to (2,1), push the left box down to the bottom row, then left into the (1,4) corner.
    uint32_t n=0;for(int32_t d:{0,0,2,2,1,3,3,1,3,0}){CHECK(!puzzleSokobanDeadlocked(level,t));CHECK(puzzleSokobanApply(level,t,{++n,PuzzleSokobanAction::Move,d}));}
    CHECK(t.pushes==3&&(t.history.current.boxes==std::vector<int32_t>{18,29}));
    CHECK(puzzleSokobanDeadlocked(level,t));auto h=puzzleSokobanHint(level,t);CHECK(h&&h->action==PuzzleSokobanAction::Undo);
    CHECK(puzzleSokobanSolve(level,t.history.current).status==PuzzleSearch::Unsolvable);
}
void data(const Json& root){
    auto bad=[&](const char* key,const std::string& text,const std::string& needle){auto j=Json::parse(text);(void)key;throwsWith([&]{
        if(std::string(key)=="match3")readMatch3Level(j);else if(std::string(key)=="sokoban")readSokobanLevel(j);else if(std::string(key)=="sudoku")readSudoku(j);
        else if(std::string(key)=="csp")readCsp(j);else if(std::string(key)=="pipes")readPipeLevel(j);else if(std::string(key)=="blocks")readBlocksLevel(j);
        else if(std::string(key)=="tiles")readTilesLevel(j);else if(std::string(key)=="packing")readPackLevel(j);else readFloodLevel(j);},needle);};
    bad("match3",R"({"rows":["123","231"],"colour":3})","puzzle.match3: unknown key 'colour'");
    bad("match3",R"({"rows":["12x"]})","puzzle.match3.rows[0][2]: expected");
    bad("match3",R"({"rows":["111","231","312"]})","puzzle.match3.rows: authored board already contains a match");
    bad("match3",R"({"rows":["....","...."],"goals":{"collect":[{"color":9,"count":1}]}})","puzzle.match3.goals.collect[0].color: must be in 1..5");
    bad("match3",R"({"rows":["....","...."],"specials":[{"shape":"square","special":"bomb"}]})","puzzle.match3.specials[0].shape");
    bad("sokoban",R"({"rows":["#@$#"]})","puzzle.sokoban.rows: box and goal counts");
    bad("sokoban",R"({"rows":["#@$.x"]})","puzzle.sokoban.rows[0][4]");
    bad("sudoku",R"({"rows":["12","21"]})","puzzle.sudoku.rows: expected 9 rows");
    bad("csp",R"({"width":2,"height":1,"values":[1,2],"constraints":[{"kind":"same","cells":[0,1]}]})","puzzle.csp.constraints[0].kind");
    bad("csp",R"({"width":2,"height":1,"values":[1,2],"constraints":[{"kind":"sum","cells":[0,5]}]})","puzzle.csp.constraints[0].cells[1]: must be in 0..1");
    bad("pipes",R"({"rows":["2g"],"kinds":["ST"]})","puzzle.pipes.rows[0][1]: expected a hex");
    bad("blocks",R"({"rows":["AB","BA"],"target":"A","goal":[0,0]})","not a filled rectangle");
    bad("tiles",R"({"width":2,"height":2,"tiles":[2,1,3,0]})","puzzle.tiles.tiles: unsolvable");
    bad("packing",R"({"rows":["..."],"pieces":[{"rows":["XX"]}]})","piece area");
    bad("flood",R"({"rows":["15"],"colors":3,"moves":2})","puzzle.flood.rows[0][1]");
    auto custom=root["match3"];auto l=readMatch3Level(custom,"levels.first");CHECK(l.rules.width==8&&l.rules.holes[0]==1&&l.start.empty()&&l.goals.collect.size()==2);
}
void persistence(){
    Match3State m;CHECK(save(m).empty());                                   // invalid states never save
    PuzzleCspState c{{-1,1,2},1,3,false};roundtrip(c);
    PuzzleCspState bad{{-1,1,2},3,1,false};CHECK(save(bad).empty());        // lastMove < moves
    PuzzleFloodState f{{1,2},0,0,PuzzleFloodStatus::Playing};auto bytes=save(f);PuzzleTilesState wrongKind;CHECK(!load(bytes,wrongKind));
    CHECK(stateKind(m)==112&&stateKind(PuzzleSokobanSession{})==119);
}
void bot(const Json& data){
    CHECK(bots::puzzleBotSkillPreset(0)&&!bots::puzzleBotSkillPreset(3)&&bots::valid(*bots::puzzleBotSkillPreset(2)));
    auto hard=*bots::puzzleBotSkillPreset(2);auto sloppy=hard;sloppy.mistakeMilli=300;
    // Tiles, flood, pipes, blocks, packing, nonogram and Sokoban all finish through the player command path.
    for(auto skill:{hard,sloppy}){
        Random rng(11,skill.mistakeMilli);
        auto t=readTilesLevel(data["tiles"]);for(int i=0;i<400&&!t.solved;++i){auto c=bots::puzzleBotTiles(t,skill,rng);CHECK(c&&puzzleTilesApply(t,*c));}CHECK(t.solved);
        auto bl=readBlocksLevel(data["blocks"]);auto bs=puzzleBlocksBegin(bl);for(int i=0;i<400&&!bs.solved;++i){auto c=bots::puzzleBotBlocks(bl,bs,skill,rng);CHECK(c&&puzzleBlocksApply(bl,bs,*c));}CHECK(bs.solved);
        auto pk=readPackLevel(data["packing"]);PuzzlePackState ps;for(int i=0;i<100&&!ps.solved;++i){auto c=bots::puzzleBotPack(pk,ps,skill,rng);CHECK(c&&puzzlePackApply(pk,ps,*c));}CHECK(ps.solved);
        auto [pl,rot]=readPipeLevel(data["pipes"]);auto pst=puzzlePipeBegin(pl,rot);for(int i=0;i<200&&!pst.solved;++i){auto c=bots::puzzleBotPipes(pl,pst,skill,rng);CHECK(c&&puzzlePipeApply(pl,pst,*c));}CHECK(pst.solved);
        auto nono=readNonogram(data["nonogram"]);auto ns=puzzleCspBegin(nono);for(int i=0;i<200&&!ns.solved;++i){auto c=bots::puzzleBotCsp(nono,ns,skill,rng);CHECK(c&&puzzleCspApply(nono,ns,*c));}CHECK(ns.solved);
        auto sk=readSokobanLevel(data["sokoban"]);auto ss=puzzleSokobanBegin(sk);for(int i=0;i<600&&!ss.solved;++i){auto c=bots::puzzleBotSokoban(sk,ss,skill,rng);CHECK(c&&puzzleSokobanApply(sk,ss,*c));}CHECK(ss.solved);
    }
    // Flood within its move limit needs the perfect bot.
    {Random rng(1,1);auto fl=readFloodLevel(data["flood"]);auto fs=puzzleFloodBegin(fl);while(fs.status==PuzzleFloodStatus::Playing){auto c=bots::puzzleBotFlood(fl,fs,hard,rng);CHECK(c&&puzzleFloodApply(fl,fs,*c));}CHECK(fs.status==PuzzleFloodStatus::Won);}
    // A perfect bot never draws from the RNG; mistakes do, deterministically.
    Random untouched(5,5),reference(5,5);auto t=readTilesLevel(data["tiles"]);CHECK(bots::puzzleBotTiles(t,hard,untouched));CHECK(untouched.state()==reference.state());
    Random m1(8,8),m2(8,8);auto level=readMatch3Level(data["match3"]);auto s1=match3Begin(level,m1),s2=match3Begin(level,m2);CHECK(s1&&s2&&*s1==*s2);
    auto normal=*bots::puzzleBotSkillPreset(1);
    for(int i=0;i<10;++i){auto c1=bots::puzzleBotMatch3(level,*s1,normal,m1),c2=bots::puzzleBotMatch3(level,*s2,normal,m2);CHECK(c1&&c2&&c1->from==c2->from&&c1->to==c2->to);CHECK(match3Apply(level,*s1,*c1,m1)&&match3Apply(level,*s2,*c2,m2));}
    CHECK(puzzleHash(*s1)==puzzleHash(*s2));
    bots::PuzzleBotSkill broken{0,0,Match3HintMode::Best};CHECK(!bots::puzzleBotTiles(t,broken,m1));
}
int main(int argc,char** argv){
    try{
        CHECK(argc==2);const auto j=levels(argv[1]);
        common();match3Rules();match3Resolution();match3Shuffles();flood(j);pipes(j);tiles(j);blocks(j);packing(j);constraints(j);sokoban(j);data(j);persistence();bot(j);
        std::cout<<"genre puzzle tests passed\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}
}
