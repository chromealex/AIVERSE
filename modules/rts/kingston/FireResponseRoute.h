#pragma once
#include <aiverselib/Graphs.h>
#include "FireResponseDetour.h"
#include "libraries/include/aiverselib/genre/Traffic.h"
namespace kingston {
// Keep station/address access, but avoid narrow connecting streets when a wide
// route exists. Reuse the engine BFS; all choices depend only on the immutable road net.
inline int wideResponseExit(const aiverse::lib::genre::TrafficNet& net,int junction,int incoming,int from,int destination,int fallback){
 using namespace aiverse::lib;
 if(from==destination)return fallback;
 const auto sink=net.junctions.size();Graph graph(sink+1);
 for(size_t i=0;i<net.pieces.size();++i){const auto&p=net.pieces[i];
  if(p.lanes<=0||(p.width()<4&&int(i)!=from&&int(i)!=destination))continue;
  int a=p.junction[0],b=p.junction[1];if(a>=0&&b>=0){graph[a].push_back(b);graph[b].push_back(a);}
  if(int(i)==destination)for(int j:p.junction)if(j>=0)graph[sink].push_back(j);
 }
 auto route=bfs(graph,sink);if(!route)return fallback;
 size_t best=kUnreachable;int exit=fallback;
 for(int side=0;side<4;++side){int q=net.junctions[junction].piece[side];if(side==incoming||q<0)continue;
  const auto&p=net.pieces[q];if(p.lanes<=0||(p.width()<4&&q!=destination))continue;
  if(q==destination)return side;
  int other=p.junction[0]==junction?p.junction[1]:p.junction[0];
  if(other>=0&&route->distance[other]<route->distance[junction]&&route->distance[other]<best){best=route->distance[other];exit=side;}
 }
 return exit;
}
}
