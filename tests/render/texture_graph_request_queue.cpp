#include "engine/render/graph/TextureGraphRequestQueue.h"
#include <cstdlib>
#include <iostream>
using aiverse::render::texture_graph::gpu::TextureRequestQueue;
static void check(bool ok){if(!ok)std::abort();}
template<class F> static void rejects(F f){bool bad=false;try{f();}catch(const std::invalid_argument&){bad=true;}check(bad);}
int main(){
    TextureRequestQueue q;
    auto a=q.push("a",1),b=q.push("bb",3),c=q.push("ccc",3),d=q.push("dddd",0);
    check(q.bytes()==10&&q.pending().size()==4);
    check(q.pop()->ticket==b);check(q.pop()->ticket==c);check(q.cancel(a));check(!q.cancel(a));
    check(q.pop()->ticket==d&&q.bytes()==0&&!q.pop());
    rejects([&]{q.push("x",4);});rejects([&]{q.push("",0);});
    rejects([&]{q.push(std::string(TextureRequestQueue::maxPayloadBytes+1,'x'),0);});
    for(size_t i=0;i<TextureRequestQueue::maxPending;++i)q.push("x",0);
    rejects([&]{q.push("x",0);});q.clear();check(q.bytes()==0&&q.pending().empty());
    for(size_t i=0;i<4;++i)q.push(std::string(TextureRequestQueue::maxPayloadBytes,'x'),0);
    check(q.bytes()==TextureRequestQueue::maxPendingBytes);
    rejects([&]{q.push("x",0);});q.clear();check(q.push("last",1)>d);
    std::cout<<"PASS: priority, FIFO, cancellation, count/byte caps, nonrecycled tickets\n";
}
