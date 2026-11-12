#pragma once
// Validated JSON level data for puzzle-solvers (example: examples/libraries/genre/puzzles/levels.json). Grids are
// arrays of equal-length row strings. Unknown keys, wrong types, out-of-range integers and inconsistent boards throw
// JsonError("puzzle.<path>: ..."), where <path> starts with the caller's prefix (e.g. "puzzle.match3").
//  match3:   {"rows":["12312","#...."], "colors":5, "specials":[{"shape":"line4|line5|corner","special":"stripe|stripeRow|
//             stripeColumn|bomb|colorBomb"}], "colorBombSwap":true, "tileScore":10, "shuffleAttempts":32,
//             "autoShuffle":true, "goals":{"moves":20,"score":0,"collect":[{"color":1,"count":10}]}}
//             rows: '#' hole, digits 1..8 authored tiles; a board of only '.' and '#' is generated from the seed.
//  sokoban:  {"rows":["#####","#@$.#","#####"], "undo":256}   (# wall, @ player, + player on goal, $ box, * box on goal, . goal)
//  sudoku:   {"box":[3,3], "rows":["53..7....", ...]}         ('.' or '0' empty, 1-9 then A-V for larger boards)
//  nonogram: {"rows":[[1,1],[3]], "columns":[[2],[1],[2]]}
//  csp:      {"width":3,"height":1,"values":[1,3],"givens":[{"cell":0,"value":1}],
//             "constraints":[{"kind":"allDifferent|sum|count|runs","cells":[0,1,2],"value":6,"count":1,"runs":[2]}]}
//  flood:    {"rows":["1122","2311"], "colors":3, "moves":6, "origin":0}
//  pipes:    {"rows":["6a","5c"], "kinds":["S.","#T"], "rotation":["00","10"], "noLeaks":true}
//             rows: hex opening masks (bit 1 left, 2 right, 4 up, 8 down); kinds: S source, T sink, '.' rotatable,
//             '#' fixed (S/T may be lowercase to mark them rotatable).
//  tiles:    {"width":3,"height":3,"tiles":[1,2,3,4,5,6,7,0,8]}   (0 = blank; must satisfy the parity rule)
//  blocks:   {"rows":["AAB.","CC.#"], "target":"A", "goal":[2,0], "interchangeable":true}   (letters = rectangular blocks)
//  packing:  {"rows":["...","..#"], "pieces":[{"rows":["XX","X."],"rotate":true,"flip":false}], "useAll":true}
#include "PuzzleMatch3.h"
#include "PuzzleFlood.h"
#include "PuzzleSliding.h"
#include "PuzzleConstraints.h"
#include "PuzzleSokoban.h"
#include "engine/data/Json.h"
#include <charconv>
#include <initializer_list>
#include <string>
#include <string_view>
namespace aiverse::lib::genre {
namespace detail_puzzledata {
inline int64_t integer(const Json& j,const std::string& path,int64_t low,int64_t high){
    if(!j.isNumber())throw JsonError(path+": expected integer");
    const auto& text=j.numberLiteral();int64_t value=0;auto parsed=std::from_chars(text.data(),text.data()+text.size(),value);
    if(parsed.ec!=std::errc{}||parsed.ptr!=text.data()+text.size())throw JsonError(path+": expected integer");
    if(value<low||value>high)throw JsonError(path+": must be in "+std::to_string(low)+".."+std::to_string(high));
    return value;
}
inline int64_t optInteger(const Json& j,const char* key,const std::string& path,int64_t low,int64_t high,int64_t fallback){return j.has(key)?integer(j[key],path+"."+key,low,high):fallback;}
inline bool optBool(const Json& j,const char* key,const std::string& path,bool fallback){
    if(!j.has(key))return fallback;
    if(j[key].type()!=Json::Type::Bool)throw JsonError(path+"."+key+": expected boolean");
    return j[key].boolean();
}
inline void keys(const Json& j,const std::string& path,std::initializer_list<std::string_view> names){
    if(!j.isObject())throw JsonError(path+": expected object");
    for(const auto& [key,value]:j.members()){(void)value;bool found=false;for(auto n:names)if(n==key)found=true;if(!found)throw JsonError(path+": unknown key '"+key+"'");}
}
inline const Json& array(const Json& j,const char* key,const std::string& path,std::size_t limit,bool required){
    static const Json empty=Json::array();
    if(!j.has(key)){if(required)throw JsonError(path+"."+key+": required");return empty;}
    if(!j[key].isArray()||j[key].size()>limit)throw JsonError(path+"."+key+": expected an array of at most "+std::to_string(limit)+" entries");
    return j[key];
}
struct Rows {int32_t width=0,height=0;std::string cells;};
inline Rows rows(const Json& j,const char* key,const std::string& path){
    const auto& a=array(j,key,path,size_t(kPuzzleMaxSide),true);Rows out;
    for(std::size_t y=0;y<a.size();++y){
        const auto p=path+"."+key+"["+std::to_string(y)+"]";if(!a[y].isString())throw JsonError(p+": expected string");
        const auto s=a[y].str();if(s.empty()||s.size()>size_t(kPuzzleMaxSide))throw JsonError(p+": expected 1..32 characters");
        if(y&&int32_t(s.size())!=out.width)throw JsonError(p+": rows must have equal length");
        out.width=int32_t(s.size());out.cells+=s;
    }
    out.height=int32_t(a.size());if(!out.height)throw JsonError(path+"."+key+": expected at least one row");return out;
}
inline std::vector<int32_t> ints(const Json& j,const std::string& path,std::size_t limit,int64_t low,int64_t high){
    if(!j.isArray()||j.size()>limit)throw JsonError(path+": expected an array of at most "+std::to_string(limit)+" integers");
    std::vector<int32_t> out;for(std::size_t i=0;i<j.size();++i)out.push_back(int32_t(integer(j[i],path+"["+std::to_string(i)+"]",low,high)));return out;
}
inline int32_t symbol(char c){if(c>='1'&&c<='9')return c-'0';if(c>='A'&&c<='V')return c-'A'+10;return -1;}
}
inline Match3Level readMatch3Level(const Json& j,const std::string& path="puzzle.match3"){
    using namespace detail_puzzledata;
    keys(j,path,{"rows","colors","specials","colorBombSwap","tileScore","shuffleAttempts","autoShuffle","goals"});
    Match3Level l;auto& r=l.rules;const auto grid=rows(j,"rows",path);r.width=grid.width;r.height=grid.height;
    r.colors=uint8_t(optInteger(j,"colors",path,3,kMatch3MaxColors,5));
    r.holes.assign(grid.cells.size(),0);bool authored=false,blank=false;
    for(size_t c=0;c<grid.cells.size();++c){
        const char ch=grid.cells[c];const auto at=path+".rows["+std::to_string(c/size_t(r.width))+"]["+std::to_string(c%size_t(r.width))+"]";
        if(ch=='#')r.holes[c]=1;else if(ch=='.')blank=true;
        else if(ch>='1'&&ch<='8'){if(ch-'0'>r.colors)throw JsonError(at+": color exceeds colors");authored=true;}
        else throw JsonError(at+": expected '#', '.' or a color digit");
    }
    if(authored&&blank)throw JsonError(path+".rows: either author every tile or leave every tile '.'");
    if(authored){l.start.resize(grid.cells.size());for(size_t c=0;c<grid.cells.size();++c)l.start[c]=r.holes[c]?Match3Tile{kMatch3Blocked,Match3Special::None}:Match3Tile{uint8_t(grid.cells[c]-'0'),Match3Special::None};}
    const auto& specials=array(j,"specials",path,8,false);
    for(std::size_t i=0;i<specials.size();++i){
        const auto p=path+".specials["+std::to_string(i)+"]";keys(specials[i],p,{"shape","special"});Match3SpecialRule rule;
        const auto shape=specials[i]["shape"].str(),special=specials[i]["special"].str();
        if(shape=="line4")rule.shape=Match3Shape::Line4;else if(shape=="line5")rule.shape=Match3Shape::Line5;else if(shape=="corner")rule.shape=Match3Shape::Corner;
        else throw JsonError(p+".shape: expected line4, line5 or corner");
        if(special=="stripe"){rule.special=Match3Special::StripeColumn;rule.perpendicular=true;}
        else if(special=="stripeRow")rule.special=Match3Special::StripeRow;else if(special=="stripeColumn")rule.special=Match3Special::StripeColumn;
        else if(special=="bomb")rule.special=Match3Special::Bomb;else if(special=="colorBomb")rule.special=Match3Special::ColorBomb;
        else throw JsonError(p+".special: expected stripe, stripeRow, stripeColumn, bomb or colorBomb");
        r.specials.push_back(rule);
    }
    r.colorBombSwap=optBool(j,"colorBombSwap",path,true);r.tileScore=optInteger(j,"tileScore",path,0,1000000,10);
    r.shuffleAttempts=uint32_t(optInteger(j,"shuffleAttempts",path,0,1024,32));r.autoShuffle=optBool(j,"autoShuffle",path,true);
    if(j.has("goals")){
        const auto p=path+".goals";const auto& g=j["goals"];keys(g,p,{"moves","score","collect"});
        l.goals.moveLimit=uint32_t(optInteger(g,"moves",p,1,100000,20));l.goals.targetScore=optInteger(g,"score",p,0,INT32_MAX,0);
        const auto& collect=array(g,"collect",p,kMatch3MaxColors,false);
        for(std::size_t i=0;i<collect.size();++i){
            const auto q=p+".collect["+std::to_string(i)+"]";keys(collect[i],q,{"color","count"});
            l.goals.collect.push_back({uint8_t(integer(collect[i]["color"],q+".color",1,r.colors)),uint32_t(integer(collect[i]["count"],q+".count",0,1000000))});
        }
        std::sort(l.goals.collect.begin(),l.goals.collect.end());
        for(size_t i=1;i<l.goals.collect.size();++i)if(l.goals.collect[i-1].first==l.goals.collect[i].first)throw JsonError(p+".collect: duplicate color");
    }
    if(!valid(r))throw JsonError(path+".rows: board needs three cells in a line next to a fourth playable cell for guaranteed shuffles");
    if(authored&&!match3Groups(r,l.start).empty())throw JsonError(path+".rows: authored board already contains a match");
    if(!valid(l))throw JsonError(path+": invalid level");
    return l;
}
inline PuzzleSokobanLevel readSokobanLevel(const Json& j,const std::string& path="puzzle.sokoban"){
    using namespace detail_puzzledata;keys(j,path,{"rows","undo"});const auto grid=rows(j,"rows",path);
    PuzzleSokobanLevel l;l.board.width=grid.width;l.board.height=grid.height;l.board.walls.assign(grid.cells.size(),0);int players=0;
    for(int32_t c=0;c<int32_t(grid.cells.size());++c){
        switch(grid.cells[size_t(c)]){
        case '#':l.board.walls[size_t(c)]=1;break;
        case '@':l.start.player=c;++players;break;
        case '+':l.start.player=c;++players;l.board.goals.push_back(c);break;
        case '$':l.start.boxes.push_back(c);break;
        case '*':l.start.boxes.push_back(c);l.board.goals.push_back(c);break;
        case '.':l.board.goals.push_back(c);break;
        case ' ':case '-':case '_':break;
        default:throw JsonError(path+".rows["+std::to_string(c/grid.width)+"]["+std::to_string(c%grid.width)+"]: unknown cell character");
        }
    }
    if(players!=1)throw JsonError(path+".rows: expected exactly one player");
    if(l.start.boxes.empty()||l.start.boxes.size()!=l.board.goals.size())throw JsonError(path+".rows: box and goal counts must match and be nonzero");
    l.undoCapacity=uint32_t(optInteger(j,"undo",path,1,4096,256));
    if(!valid(l))throw JsonError(path+": invalid board");
    return l;
}
inline PuzzleCsp readSudoku(const Json& j,const std::string& path="puzzle.sudoku"){
    using namespace detail_puzzledata;keys(j,path,{"box","rows"});
    const auto box=j.has("box")?ints(j["box"],path+".box",2,1,8):std::vector<int32_t>{3,3};if(box.size()!=2)throw JsonError(path+".box: expected [width, height]");
    const int32_t n=box[0]*box[1];const auto grid=rows(j,"rows",path);if(grid.width!=n||grid.height!=n)throw JsonError(path+".rows: expected "+std::to_string(n)+" rows of "+std::to_string(n)+" symbols");
    std::vector<int8_t> givens(grid.cells.size(),0);
    for(size_t c=0;c<grid.cells.size();++c){
        const char ch=grid.cells[c];if(ch=='.'||ch=='0')continue;const auto v=symbol(ch);
        if(v<1||v>n)throw JsonError(path+".rows["+std::to_string(c/size_t(n))+"]["+std::to_string(c%size_t(n))+"]: expected '.' or a symbol 1.."+std::to_string(n));
        givens[c]=int8_t(v);
    }
    auto p=puzzleSudokuCsp(box[0],box[1],givens);if(!p)throw JsonError(path+": invalid sudoku");return *p;
}
inline PuzzleCsp readNonogram(const Json& j,const std::string& path="puzzle.nonogram"){
    using namespace detail_puzzledata;keys(j,path,{"rows","columns"});std::vector<std::vector<int32_t>> lines[2];const char* names[2]{"rows","columns"};
    for(int k=0;k<2;++k){const auto& a=array(j,names[k],path,size_t(kPuzzleMaxSide),true);for(std::size_t i=0;i<a.size();++i)lines[k].push_back(ints(a[i],path+"."+names[k]+"["+std::to_string(i)+"]",16,1,kPuzzleMaxSide));}
    auto p=puzzleNonogramCsp(lines[0],lines[1]);if(!p)throw JsonError(path+": clues do not fit the grid");return *p;
}
inline PuzzleCsp readCsp(const Json& j,const std::string& path="puzzle.csp"){
    using namespace detail_puzzledata;keys(j,path,{"width","height","values","givens","constraints"});
    PuzzleCsp p;p.width=int32_t(integer(j["width"],path+".width",1,kPuzzleMaxSide));p.height=int32_t(integer(j["height"],path+".height",1,kPuzzleMaxSide));
    const auto range=ints(j["values"],path+".values",2,0,kPuzzleCspMaxValue);if(range.size()!=2||range[0]>range[1])throw JsonError(path+".values: expected [min, max]");
    uint32_t all=0;for(int32_t v=range[0];v<=range[1];++v)all|=1u<<v;p.domains.assign(size_t(p.width*p.height),all);
    const auto& givens=array(j,"givens",path,size_t(kPuzzleMaxCells),false);
    for(std::size_t i=0;i<givens.size();++i){
        const auto q=path+".givens["+std::to_string(i)+"]";keys(givens[i],q,{"cell","value"});
        const auto cell=integer(givens[i]["cell"],q+".cell",0,p.width*p.height-1);p.domains[size_t(cell)]=1u<<integer(givens[i]["value"],q+".value",range[0],range[1]);
    }
    const auto& cs=array(j,"constraints",path,4096,true);
    for(std::size_t i=0;i<cs.size();++i){
        const auto q=path+".constraints["+std::to_string(i)+"]";keys(cs[i],q,{"kind","cells","value","count","runs"});PuzzleConstraint c;
        const auto kind=cs[i]["kind"].str();
        if(kind=="allDifferent")c.kind=PuzzleConstraintKind::AllDifferent;else if(kind=="sum")c.kind=PuzzleConstraintKind::Sum;
        else if(kind=="count")c.kind=PuzzleConstraintKind::Count;else if(kind=="runs")c.kind=PuzzleConstraintKind::Runs;
        else throw JsonError(q+".kind: expected allDifferent, sum, count or runs");
        c.cells=ints(cs[i]["cells"],q+".cells",64,0,p.width*p.height-1);
        c.value=int32_t(optInteger(cs[i],"value",q,0,kPuzzleCspMaxValue*64,c.kind==PuzzleConstraintKind::Runs?1:0));c.count=int32_t(optInteger(cs[i],"count",q,0,64,0));
        if(cs[i].has("runs"))c.runs=ints(cs[i]["runs"],q+".runs",32,1,64);
        p.constraints.push_back(std::move(c));
        if(!valid(p))throw JsonError(q+": duplicate cells, out-of-range value/count or runs longer than the line");
    }
    if(!valid(p))throw JsonError(path+": invalid constraint problem");
    return p;
}
inline PuzzleFloodLevel readFloodLevel(const Json& j,const std::string& path="puzzle.flood"){
    using namespace detail_puzzledata;keys(j,path,{"rows","colors","moves","origin"});const auto grid=rows(j,"rows",path);
    PuzzleFloodLevel l{grid.width,grid.height,uint8_t(integer(j["colors"],path+".colors",2,9)),{},0,0};
    for(size_t c=0;c<grid.cells.size();++c){const auto v=grid.cells[c]-'0';if(v<1||v>l.colors)throw JsonError(path+".rows["+std::to_string(c/size_t(l.width))+"]["+std::to_string(c%size_t(l.width))+"]: expected a color 1..colors");l.cells.push_back(uint8_t(v));}
    l.moveLimit=uint32_t(integer(j["moves"],path+".moves",1,1000));l.origin=int32_t(optInteger(j,"origin",path,0,l.width*l.height-1,0));
    if(!valid(l))throw JsonError(path+": invalid level");
    return l;
}
inline std::pair<PuzzlePipeLevel,std::vector<uint8_t>> readPipeLevel(const Json& j,const std::string& path="puzzle.pipes"){
    using namespace detail_puzzledata;keys(j,path,{"rows","kinds","rotation","noLeaks"});const auto grid=rows(j,"rows",path),kinds=rows(j,"kinds",path);
    if(kinds.width!=grid.width||kinds.height!=grid.height)throw JsonError(path+".kinds: must match rows");
    PuzzlePipeLevel l{grid.width,grid.height,{},{},optBool(j,"noLeaks",path,false)};std::vector<uint8_t> rotation(grid.cells.size(),0);
    for(size_t c=0;c<grid.cells.size();++c){
        const auto at="["+std::to_string(c/size_t(l.width))+"]["+std::to_string(c%size_t(l.width))+"]";const char m=grid.cells[c],k=kinds.cells[c];
        int v=m>='0'&&m<='9'?m-'0':m>='a'&&m<='f'?m-'a'+10:-1;if(v<0)throw JsonError(path+".rows"+at+": expected a hex opening mask");l.masks.push_back(uint8_t(v));
        uint8_t f=0;switch(k){case 'S':f=kPuzzlePipeSource;break;case 's':f=kPuzzlePipeSource|kPuzzlePipeRotatable;break;case 'T':f=kPuzzlePipeSink;break;
            case 't':f=kPuzzlePipeSink|kPuzzlePipeRotatable;break;case '.':f=kPuzzlePipeRotatable;break;case '#':f=0;break;default:throw JsonError(path+".kinds"+at+": expected S, s, T, t, '.' or '#'");}
        l.flags.push_back(f);
    }
    if(j.has("rotation")){
        const auto rot=rows(j,"rotation",path);if(rot.width!=grid.width||rot.height!=grid.height)throw JsonError(path+".rotation: must match rows");
        for(size_t c=0;c<rot.cells.size();++c){const int v=rot.cells[c]-'0';if(v<0||v>3||(v&&!(l.flags[c]&kPuzzlePipeRotatable)))throw JsonError(path+".rotation["+std::to_string(c/size_t(l.width))+"]["+std::to_string(c%size_t(l.width))+"]: expected 0..3 (0 on fixed cells)");rotation[c]=uint8_t(v);}
    }
    if(!valid(l))throw JsonError(path+": needs at least one source and one sink");
    return {std::move(l),std::move(rotation)};
}
inline PuzzleTilesState readTilesLevel(const Json& j,const std::string& path="puzzle.tiles"){
    using namespace detail_puzzledata;keys(j,path,{"width","height","tiles"});
    const int32_t w=int32_t(integer(j["width"],path+".width",2,8)),h=int32_t(integer(j["height"],path+".height",2,8));
    const auto t=ints(j["tiles"],path+".tiles",64,0,w*h-1);std::vector<uint16_t> tiles(t.begin(),t.end());
    if(!puzzleTilesPermutation(w,h,tiles))throw JsonError(path+".tiles: expected a permutation of 0..width*height-1");
    auto s=puzzleTilesBegin(w,h,std::move(tiles));if(!s)throw JsonError(path+".tiles: unsolvable (parity)");return *s;
}
inline PuzzleBlocksLevel readBlocksLevel(const Json& j,const std::string& path="puzzle.blocks"){
    using namespace detail_puzzledata;keys(j,path,{"rows","target","goal","interchangeable"});const auto grid=rows(j,"rows",path);
    PuzzleBlocksLevel l;l.width=grid.width;l.height=grid.height;l.walls.assign(grid.cells.size(),0);std::vector<char> letters;
    for(size_t c=0;c<grid.cells.size();++c){const char ch=grid.cells[c];if(ch=='#')l.walls[c]=1;else if((ch>='A'&&ch<='Z')||(ch>='a'&&ch<='z')){if(std::find(letters.begin(),letters.end(),ch)==letters.end())letters.push_back(ch);}else if(ch!='.')throw JsonError(path+".rows["+std::to_string(c/size_t(l.width))+"]["+std::to_string(c%size_t(l.width))+"]: expected '#', '.' or a block letter");}
    std::sort(letters.begin(),letters.end());const bool swap=optBool(j,"interchangeable",path,true);
    for(char ch:letters){
        int32_t x0=INT32_MAX,y0=INT32_MAX,x1=-1,y1=-1,n=0;
        for(int32_t c=0;c<int32_t(grid.cells.size());++c)if(grid.cells[size_t(c)]==ch){x0=std::min(x0,c%l.width);y0=std::min(y0,c/l.width);x1=std::max(x1,c%l.width);y1=std::max(y1,c/l.width);++n;}
        if(n!=(x1-x0+1)*(y1-y0+1))throw JsonError(path+".rows: block '"+std::string(1,ch)+"' is not a filled rectangle");
        l.blocks.push_back({x1-x0+1,y1-y0+1,swap});l.start.push_back(y0*l.width+x0);
    }
    const auto target=j["target"].str();if(target.size()!=1)throw JsonError(path+".target: expected one block letter");
    auto it=std::find(letters.begin(),letters.end(),target[0]);if(it==letters.end())throw JsonError(path+".target: unknown block '"+target+"'");l.target=int32_t(it-letters.begin());
    const auto goal=ints(j["goal"],path+".goal",2,0,kPuzzleMaxSide-1);if(goal.size()!=2||goal[0]>=l.width||goal[1]>=l.height)throw JsonError(path+".goal: expected [x, y] inside the grid");l.goal=goal[1]*l.width+goal[0];
    if(!valid(l))throw JsonError(path+": invalid level");
    return l;
}
inline PuzzlePackLevel readPackLevel(const Json& j,const std::string& path="puzzle.packing"){
    using namespace detail_puzzledata;keys(j,path,{"rows","pieces","useAll"});const auto grid=rows(j,"rows",path);
    PuzzlePackLevel l;l.width=grid.width;l.height=grid.height;for(size_t c=0;c<grid.cells.size();++c){const char ch=grid.cells[c];if(ch!='.'&&ch!='#')throw JsonError(path+".rows["+std::to_string(c/size_t(l.width))+"]["+std::to_string(c%size_t(l.width))+"]: expected '.' (cover) or '#'");l.target.push_back(ch=='.');}
    const auto& pieces=array(j,"pieces",path,64,true);
    for(std::size_t i=0;i<pieces.size();++i){
        const auto q=path+".pieces["+std::to_string(i)+"]";keys(pieces[i],q,{"rows","rotate","flip"});const auto shape=rows(pieces[i],"rows",q);PuzzlePieceDef p;
        for(int32_t c=0;c<int32_t(shape.cells.size());++c){if(shape.cells[size_t(c)]=='X')p.cells.push_back({c%shape.width,c/shape.width});else if(shape.cells[size_t(c)]!='.')throw JsonError(q+".rows: expected 'X' or '.'");}
        if(p.cells.empty())throw JsonError(q+".rows: piece has no cells");
        p.rotate=optBool(pieces[i],"rotate",q,true);p.flip=optBool(pieces[i],"flip",q,false);l.pieces.push_back(std::move(p));
    }
    l.useAll=optBool(j,"useAll",path,true);if(!valid(l))throw JsonError(path+": piece area does not match the cells to cover");return l;
}
}
