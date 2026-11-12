#pragma once
// Generic constraint puzzles over small grids (puzzle-solvers): cell domains are bit masks of values 0..31,
// constraints are data (AllDifferent, Sum, Count, Runs = nonogram line clues), propagation runs to a fixpoint in
// constraint order, and bounded backtracking (smallest domain first, lowest cell on ties, values ascending) solves
// or counts solutions (limit 2 = uniqueness check). Sudoku and nonogram builders produce ordinary PuzzleCsp data.
#include "PuzzleCommon.h"
#include <bit>
namespace aiverse::lib::genre {
inline constexpr int32_t kPuzzleCspMaxValue=31;
enum class PuzzleConstraintKind:uint8_t {AllDifferent,Sum,Count,Runs};
// Sum: assigned values add up to `value`. Count: exactly `count` cells take `value`. Runs: along `cells` in order,
// the cells equal to `value` form exactly the blocks `runs` (each separated by at least one other value).
struct PuzzleConstraint {PuzzleConstraintKind kind=PuzzleConstraintKind::AllDifferent;std::vector<int32_t> cells;int32_t value=0,count=0;std::vector<int32_t> runs;};
struct PuzzleCsp {int32_t width=0,height=0;std::vector<uint32_t> domains;std::vector<PuzzleConstraint> constraints;};
struct PuzzleCspState {std::vector<int8_t> values;uint32_t moves=0,lastMove=0;bool solved=false;bool operator==(const PuzzleCspState&)const=default;};
struct PuzzleCspAssign {uint32_t move=0;int32_t cell=-1;int8_t value=-1;};   // value -1 clears the cell
struct PuzzleCspSolution {PuzzleSearch status=PuzzleSearch::Invalid;std::vector<int8_t> values;uint32_t count=0;uint64_t nodes=0;};
inline bool valid(const PuzzleCsp& p){
    if(!puzzleGridSize(p.width,p.height)||p.domains.size()!=size_t(p.width*p.height)||p.constraints.size()>4096)return false;
    for(auto d:p.domains)if(!d)return false;
    for(const auto& c:p.constraints){
        if(c.kind>PuzzleConstraintKind::Runs||c.cells.empty()||c.cells.size()>64||c.value<0||c.value>kPuzzleCspMaxValue*64)return false;
        auto cells=c.cells;std::sort(cells.begin(),cells.end());if(std::adjacent_find(cells.begin(),cells.end())!=cells.end()||cells.front()<0||cells.back()>=int32_t(p.domains.size()))return false;
        if((c.kind==PuzzleConstraintKind::Count||c.kind==PuzzleConstraintKind::Runs)&&c.value>kPuzzleCspMaxValue)return false;
        if(c.kind==PuzzleConstraintKind::Count&&(c.count<0||c.count>int32_t(c.cells.size())))return false;
        if(c.kind==PuzzleConstraintKind::Runs){int64_t need=0;for(auto r:c.runs){if(r<1)return false;need+=r+1;}if(c.runs.size()>32||need-1>int64_t(c.cells.size()))return false;}
    }
    return true;
}
inline bool valid(const PuzzleCspState& s){if(s.values.empty()||s.values.size()>size_t(kPuzzleMaxCells)||s.lastMove<s.moves)return false;for(auto v:s.values)if(v< -1||v>kPuzzleCspMaxValue)return false;return true;}
inline bool puzzleCspGiven(const PuzzleCsp& p,int32_t cell){return std::popcount(p.domains[size_t(cell)])==1;}
namespace detail_csp {
inline int32_t low(uint32_t d){return std::countr_zero(d);}
inline int32_t high(uint32_t d){return 31-std::countl_zero(d);}
inline uint32_t range(int32_t lo,int32_t hi){uint32_t m=0;for(int32_t v=std::max(lo,0);v<=std::min(hi,kPuzzleCspMaxValue);++v)m|=1u<<v;return m;}
// Restricts domains for one constraint; false on contradiction. `changed` is set when a domain shrinks.
inline bool revise(const PuzzleConstraint& c,std::vector<uint32_t>& d,bool& changed){
    auto set=[&](int32_t cell,uint32_t mask){const auto n=d[size_t(cell)]&mask;if(n!=d[size_t(cell)]){d[size_t(cell)]=n;changed=true;}return n!=0;};
    switch(c.kind){
    case PuzzleConstraintKind::AllDifferent:{
        uint32_t fixed=0,all=0;
        for(auto x:c.cells){const auto m=d[size_t(x)];all|=m;if(std::popcount(m)==1){if(fixed&m)return false;fixed|=m;}}
        if(std::popcount(all)<int(c.cells.size()))return false;
        for(auto x:c.cells)if(std::popcount(d[size_t(x)])>1&&!set(x,~fixed))return false;
        if(std::popcount(all)==int(c.cells.size()))for(int32_t v=0;v<=kPuzzleCspMaxValue;++v){
            if(!(all&(1u<<v)))continue;
            int32_t where=-1,n=0;for(auto x:c.cells)if(d[size_t(x)]&(1u<<v)){where=x;++n;}
            if(n==0)return false;
            if(n==1&&!set(where,1u<<v))return false;
        }
        return true;
    }
    case PuzzleConstraintKind::Sum:{
        int64_t lo=0,hi=0;for(auto x:c.cells){lo+=low(d[size_t(x)]);hi+=high(d[size_t(x)]);}
        if(c.value<lo||c.value>hi)return false;
        for(auto x:c.cells){const auto m=d[size_t(x)];if(!set(x,range(int32_t(c.value-(hi-high(m))),int32_t(c.value-(lo-low(m))))))return false;}
        return true;
    }
    case PuzzleConstraintKind::Count:{
        const uint32_t bit=1u<<c.value;int32_t sure=0,may=0;for(auto x:c.cells){if(d[size_t(x)]==bit)++sure;if(d[size_t(x)]&bit)++may;}
        if(sure>c.count||may<c.count)return false;
        if(sure==c.count){for(auto x:c.cells)if(d[size_t(x)]!=bit&&!set(x,~bit))return false;}
        else if(may==c.count){for(auto x:c.cells)if((d[size_t(x)]&bit)&&!set(x,bit))return false;}
        return true;
    }
    case PuzzleConstraintKind::Runs:{
        const uint32_t bit=1u<<c.value;const int32_t n=int32_t(c.cells.size()),k=int32_t(c.runs.size());
        std::vector<uint8_t> canV(c.cells.size(),0),canN(c.cells.size(),0);for(int32_t i=0;i<n;++i){canV[size_t(i)]=(d[size_t(c.cells[size_t(i)])]&bit)!=0;canN[size_t(i)]=(d[size_t(c.cells[size_t(i)])]&~bit)!=0;}
        auto fits=[&](int32_t j,int32_t s){const int32_t len=c.runs[size_t(j)];if(s+len>n)return false;for(int32_t t=s;t<s+len;++t)if(!canV[size_t(t)])return false;return s+len==n||canN[size_t(s+len)];};
        auto after=[&](int32_t j,int32_t s){const int32_t e=s+c.runs[size_t(j)];return e==n?n:e+1;};
        // fwd[j][i]: cells [0,i) hold runs 0..j-1 exactly; bwd[j][i]: cells [i,n) hold runs j..k-1 exactly.
        std::vector<std::vector<uint8_t>> fwd(size_t(k+1),std::vector<uint8_t>(size_t(n+1),0)),bwd=fwd;
        fwd[0][0]=1;
        for(int32_t i=0;i<=n;++i)for(int32_t j=0;j<=k;++j){
            if(!fwd[size_t(j)][size_t(i)]||i==n)continue;
            if(canN[size_t(i)])fwd[size_t(j)][size_t(i+1)]=1;
            if(j<k&&fits(j,i))fwd[size_t(j+1)][size_t(after(j,i))]=1;
        }
        if(!fwd[size_t(k)][size_t(n)])return false;
        bwd[size_t(k)][size_t(n)]=1;
        for(int32_t i=n-1;i>=0;--i)for(int32_t j=k;j>=0;--j){
            bool ok=canN[size_t(i)]&&bwd[size_t(j)][size_t(i+1)];
            if(!ok&&j<k&&fits(j,i))ok=bwd[size_t(j+1)][size_t(after(j,i))];
            bwd[size_t(j)][size_t(i)]=ok;
        }
        std::vector<uint8_t> supV(size_t(n),0),supN(size_t(n),0);
        for(int32_t i=0;i<n;++i)for(int32_t j=0;j<=k;++j){
            if(!fwd[size_t(j)][size_t(i)])continue;
            if(canN[size_t(i)]&&bwd[size_t(j)][size_t(i+1)])supN[size_t(i)]=1;
            if(j<k&&fits(j,i)&&bwd[size_t(j+1)][size_t(after(j,i))]){
                for(int32_t t=i;t<i+c.runs[size_t(j)];++t)supV[size_t(t)]=1;
                if(i+c.runs[size_t(j)]<n)supN[size_t(i+c.runs[size_t(j)])]=1;
            }
        }
        for(int32_t i=0;i<n;++i){
            const auto x=c.cells[size_t(i)];
            if(!supV[size_t(i)]&&!set(x,~bit))return false;
            if(!supN[size_t(i)]&&!set(x,bit))return false;
        }
        return true;
    }
    }
    return false;
}
// 1 = fixpoint, 0 = contradiction, -1 = budget exhausted (one node per constraint revision).
inline int propagate(const PuzzleCsp& p,std::vector<uint32_t>& d,PuzzleBudget& budget){
    for(bool changed=true;changed;){
        changed=false;
        for(const auto& c:p.constraints){if(!budget.take())return -1;if(!revise(c,d,changed))return 0;}
    }
    return 1;
}
struct Search {const PuzzleCsp& p;PuzzleBudget& budget;uint32_t limit;uint32_t count=0;std::vector<int8_t> first;bool out=false;};
inline void run(Search& s,std::vector<uint32_t> d){
    if(s.count>=s.limit||s.out)return;
    const int r=propagate(s.p,d,s.budget);if(r<0){s.out=true;return;}if(r==0)return;
    int32_t pick=-1;int best=64;
    for(int32_t i=0;i<int32_t(d.size());++i){const int n=std::popcount(d[size_t(i)]);if(n>1&&n<best){best=n;pick=i;}}
    if(pick<0){if(!s.count){s.first.resize(d.size());for(size_t i=0;i<d.size();++i)s.first[i]=int8_t(low(d[i]));}++s.count;return;}
    for(int32_t v=0;v<=kPuzzleCspMaxValue;++v){
        if(!(d[size_t(pick)]&(1u<<v)))continue;
        if(!s.budget.take()){s.out=true;return;}
        auto copy=d;copy[size_t(pick)]=1u<<v;run(s,std::move(copy));if(s.count>=s.limit||s.out)return;
    }
}
}
// Domains with the cells of `values` (>= 0) fixed; nullopt when a value is outside its domain.
inline std::optional<std::vector<uint32_t>> puzzleCspDomains(const PuzzleCsp& p,const std::vector<int8_t>& values){
    auto d=p.domains;if(!values.empty()&&values.size()!=d.size())return std::nullopt;
    for(size_t i=0;i<values.size();++i)if(values[i]>=0){if(!(d[i]&(1u<<values[i])))return std::nullopt;d[i]=1u<<values[i];}
    return d;
}
// Solves/counts from the given partial assignment (empty = givens only). Solved: >= 1 solution (values = first);
// Unsolvable: proven none; Unknown: budget exhausted before the count reached `limit` (a found solution is kept).
inline PuzzleCspSolution puzzleCspSolve(const PuzzleCsp& p,const std::vector<int8_t>& values={},uint32_t limit=1,PuzzleBudget budget={}){
    PuzzleCspSolution out;if(!valid(p)||!puzzleValidBudget(budget)||limit<1)return out;auto d=puzzleCspDomains(p,values);if(!d)return out;
    detail_csp::Search s{p,budget,limit,0,{},false};detail_csp::run(s,std::move(*d));
    out.count=s.count;out.values=std::move(s.first);out.nodes=budget.used;
    out.status=s.out?PuzzleSearch::Unknown:s.count?PuzzleSearch::Solved:PuzzleSearch::Unsolvable;return out;
}
struct PuzzleCspUniqueness {PuzzleSearch status=PuzzleSearch::Invalid;bool unique=false;std::vector<int8_t> values;uint64_t nodes=0;};
// Exactly one solution, proven within the budget (status Solved + unique); Unknown when the budget ran out.
inline PuzzleCspUniqueness puzzleCspUnique(const PuzzleCsp& p,PuzzleBudget budget={}){
    auto r=puzzleCspSolve(p,{},2,budget);return {r.status,r.status==PuzzleSearch::Solved&&r.count==1,std::move(r.values),r.nodes};
}
// Indices of constraints the (partial) assignment already breaks; empty cells (-1) never violate anything.
inline std::vector<int32_t> puzzleCspViolations(const PuzzleCsp& p,const std::vector<int8_t>& v){
    std::vector<int32_t> out;if(v.size()!=p.domains.size())return out;
    for(int32_t i=0;i<int32_t(p.constraints.size());++i){
        const auto& c=p.constraints[size_t(i)];bool full=true,bad=false;for(auto x:c.cells)full=full&&v[size_t(x)]>=0;
        switch(c.kind){
        case PuzzleConstraintKind::AllDifferent:{uint32_t seen=0;for(auto x:c.cells)if(v[size_t(x)]>=0){if(seen&(1u<<v[size_t(x)]))bad=true;seen|=1u<<v[size_t(x)];}break;}
        case PuzzleConstraintKind::Sum:{int64_t s=0;for(auto x:c.cells)if(v[size_t(x)]>=0)s+=v[size_t(x)];bad=s>c.value||(full&&s!=c.value);break;}
        case PuzzleConstraintKind::Count:{int32_t n=0;for(auto x:c.cells)if(v[size_t(x)]==c.value)++n;bad=n>c.count||(full&&n!=c.count);break;}
        case PuzzleConstraintKind::Runs:{
            if(!full)break;
            std::vector<int32_t> runs;int32_t len=0;
            for(auto x:c.cells){if(v[size_t(x)]==c.value)++len;else if(len){runs.push_back(len);len=0;}}
            if(len)runs.push_back(len);
            bad=runs!=c.runs;break;
        }
        }
        if(bad)out.push_back(i);
    }
    return out;
}
inline bool puzzleCspComplete(const PuzzleCsp& p,const std::vector<int8_t>& v){
    if(v.size()!=p.domains.size())return false;
    for(auto x:v)if(x<0)return false;
    return puzzleCspViolations(p,v).empty();
}
inline PuzzleCspState puzzleCspBegin(const PuzzleCsp& p){
    PuzzleCspState s;if(!valid(p))return s;s.values.assign(p.domains.size(),-1);
    for(int32_t i=0;i<int32_t(p.domains.size());++i)if(puzzleCspGiven(p,i))s.values[size_t(i)]=int8_t(detail_csp::low(p.domains[size_t(i)]));
    s.solved=puzzleCspComplete(p,s.values);return s;
}
// Player/bot entry: assigns or clears one non-given cell with a value from its domain. Conflicts are allowed (the
// player may be wrong); `solved` is set once every cell is filled and no constraint is violated.
inline bool puzzleCspApply(const PuzzleCsp& p,PuzzleCspState& s,const PuzzleCspAssign& a){
    if(s.solved||a.move<=s.lastMove||!valid(p)||!valid(s)||s.values.size()!=p.domains.size()||a.cell<0||a.cell>=int32_t(p.domains.size())||puzzleCspGiven(p,a.cell))return false;
    if(a.value< -1||a.value>kPuzzleCspMaxValue||(a.value>=0&&!(p.domains[size_t(a.cell)]&(1u<<a.value)))||s.values[size_t(a.cell)]==a.value)return false;
    s.values[size_t(a.cell)]=a.value;++s.moves;s.lastMove=a.move;s.solved=puzzleCspComplete(p,s.values);return true;
}
// Hint: fixes the first wrong cell against the givens-only solution; otherwise fills the empty cell with the
// smallest propagated domain (lowest cell on ties) with its solution value.
inline std::optional<PuzzleCspAssign> puzzleCspHint(const PuzzleCsp& p,const PuzzleCspState& s,PuzzleBudget budget={}){
    if(s.solved||!valid(p)||s.values.size()!=p.domains.size())return std::nullopt;
    auto solution=puzzleCspSolve(p,{},1,budget);if(solution.status!=PuzzleSearch::Solved)return std::nullopt;
    std::vector<int8_t> correct(s.values.size(),-1);
    for(int32_t i=0;i<int32_t(s.values.size());++i){
        if(s.values[size_t(i)]>=0&&s.values[size_t(i)]!=solution.values[size_t(i)])return PuzzleCspAssign{s.lastMove+1,i,solution.values[size_t(i)]};
        correct[size_t(i)]=s.values[size_t(i)];
    }
    auto d=puzzleCspDomains(p,correct);if(!d)return std::nullopt;PuzzleBudget local{budget.nodes};detail_csp::propagate(p,*d,local);
    int32_t pick=-1;int best=64;
    for(int32_t i=0;i<int32_t(s.values.size());++i)if(s.values[size_t(i)]<0){const int n=std::popcount((*d)[size_t(i)]);if(n<best){best=n;pick=i;}}
    if(pick<0)return std::nullopt;
    return PuzzleCspAssign{s.lastMove+1,pick,solution.values[size_t(pick)]};
}
// Sudoku of boxW*boxH symbols (values 1..N); `givens` row-major, 0 = empty.
inline std::optional<PuzzleCsp> puzzleSudokuCsp(int32_t boxW,int32_t boxH,const std::vector<int8_t>& givens){
    const int32_t n=boxW*boxH;if(boxW<1||boxH<1||n<2||n>kPuzzleCspMaxValue||n>kPuzzleMaxSide||givens.size()!=size_t(n*n))return std::nullopt;
    PuzzleCsp p{n,n,std::vector<uint32_t>(size_t(n*n),0),{}};
    for(int32_t i=0;i<n*n;++i){const auto g=givens[size_t(i)];if(g<0||g>n)return std::nullopt;p.domains[size_t(i)]=g?(1u<<g):(((1u<<(n+1))-1)&~1u);}
    for(int32_t r=0;r<n;++r){PuzzleConstraint row,col;for(int32_t k=0;k<n;++k){row.cells.push_back(r*n+k);col.cells.push_back(k*n+r);}p.constraints.push_back(row);p.constraints.push_back(col);}
    for(int32_t by=0;by<n;by+=boxH)for(int32_t bx=0;bx<n;bx+=boxW){PuzzleConstraint box;for(int32_t y=by;y<by+boxH;++y)for(int32_t x=bx;x<bx+boxW;++x)box.cells.push_back(y*n+x);p.constraints.push_back(box);}
    if(!valid(p))return std::nullopt;
    return p;
}
// Nonogram: values 0 (empty) / 1 (filled); one Runs constraint per row then per column.
inline std::optional<PuzzleCsp> puzzleNonogramCsp(const std::vector<std::vector<int32_t>>& rows,const std::vector<std::vector<int32_t>>& columns){
    const int32_t h=int32_t(rows.size()),w=int32_t(columns.size());if(!puzzleGridSize(w,h))return std::nullopt;
    PuzzleCsp p{w,h,std::vector<uint32_t>(size_t(w*h),3u),{}};
    for(int32_t y=0;y<h;++y){PuzzleConstraint c{PuzzleConstraintKind::Runs,{},1,0,rows[size_t(y)]};for(int32_t x=0;x<w;++x)c.cells.push_back(y*w+x);p.constraints.push_back(std::move(c));}
    for(int32_t x=0;x<w;++x){PuzzleConstraint c{PuzzleConstraintKind::Runs,{},1,0,columns[size_t(x)]};for(int32_t y=0;y<h;++y)c.cells.push_back(y*w+x);p.constraints.push_back(std::move(c));}
    if(!valid(p))return std::nullopt;
    return p;
}
}
