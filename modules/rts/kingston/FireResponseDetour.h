#pragma once
// Bounded deterministic local routing around the complete nearby vehicle cluster.
// The caller supplies road/obstacle clearance; the normal swept collision checks remain authoritative.
#include <algorithm>
#include <aiverselib/Graphs.h>
#include "libraries/include/aiverselib/genre/Traffic.h"
namespace kingston {
template<class Clear> std::vector<aiverse::Vec2> fireLocalDetour(aiverse::Vec2 start,aiverse::Vec2 goal,Clear clear){
 using namespace aiverse;using namespace aiverse::lib;
 constexpr int radius=24,n=radius*2+1,count=n*n;const Fixed step=Fixed::fromRatio(1,4);
 auto point=[&](int i){return start+Vec2{step*(i%n-radius),step*(i/n-radius)};};
 std::vector<bool> free(count);for(int i=0;i<count;++i)free[i]=clear(point(i));
 const int origin=radius*n+radius;free[origin]=true;Graph graph(count);
 for(int y=1;y<n-1;++y)for(int x=1;x<n-1;++x){int i=y*n+x;if(!free[i])continue;
  for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx){if(!dx&&!dy)continue;int j=(y+dy)*n+x+dx;if(free[j]&&free[y*n+x+dx]&&free[(y+dy)*n+x])graph[i].push_back(j);}
 }
 auto paths=bfs(graph,origin);if(!paths)return {};
 int target=-1;Fixed best=Fixed::fromInt(100000);
 for(int i=0;i<count;++i)if(paths->distance[i]!=kUnreachable){Fixed d=(point(i)-goal).lengthSq();if(d<best){best=d;target=i;}}
 if(target<0||best>Fixed::fromRatio(9,4))return {};
 std::vector<Vec2> route;for(int i=target;i!=origin;i=int(paths->parent[i]))route.push_back(point(i));route.push_back(start);std::reverse(route.begin(),route.end());
 auto visible=[&](Vec2 a,Vec2 b){auto delta=b-a;int samples=std::max(1,int((delta.length()*8).floorInt()+1));for(int i=1;i<=samples;++i)if(!clear(a+delta*Fixed::fromRatio(i,samples)))return false;return true;};
 std::vector<Vec2> simple{start};for(size_t i=0;i+1<route.size();){size_t j=i+1;while(j+1<route.size()&&(route[j+1]-route[i]).lengthSq()<=Fixed::fromInt(4)&&visible(route[i],route[j+1]))++j;simple.push_back(route[j]);i=j;}return simple;
}
}
