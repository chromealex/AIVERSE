#include <aiverselib/net/Admission.h>
// Invoked by the native acceptance host, never from setup/tick/command.
bool nativeAdmissionProbe(){
    using namespace aiverse::lib::session;
    auto pair=aiverse::net::makeLoopbackPair();
    ClientAdmission client(std::move(pair.first),"fixture-token");AdmissionGate gate("fixture-match");
    if(!gate.enqueue(std::move(pair.second),0))return false;
    std::vector<Admission> peers;
    for(uint64_t now=0;now<10&&peers.empty();++now){client.update(now);peers=gate.poll(now,[](const std::string& token,const std::string& audience,uint64_t)->std::optional<Principal>{if(token!="fixture-token")return {};return Principal{"fixture-player",audience,1,1000};});}
    if(peers.size()!=1||peers[0].access->principal.player!="fixture-player")return false;
    client.update(10);if(client.state()!=aiverse::net::ConnState::Connected)return false;
    client.send({1,2,3});client.update(11);peers[0].connection->update(11);aiverse::net::Bytes bytes;
    if(!peers[0].connection->recv(bytes)||bytes!=aiverse::net::Bytes({1,2,3}))return false;
    peers[0].access->revoked=true;peers[0].connection->update(12);
    return peers[0].connection->state()==aiverse::net::ConnState::Closed;
}
