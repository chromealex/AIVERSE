#include "engine/physis/PhysisRemoteClient.h"
#include "engine/physis/PhysisRemoteServer.h"
// Compile regression: the real Editor declares a separate editor::net namespace.
// The PHYSIS EditorLink must resolve its connection and crypto types against
// the engine transport namespace, independent of this shadow declaration.
namespace aiverse::editor::net { struct PhysisNamespaceCollisionFixture {}; }
#include "apps/editor/PhysisRemoteEdit.h"
#include "engine/net/Conn.h"
#include <algorithm>
#include <cstdio>
#include <memory>
#include <map>
#include <string>
#include <utility>
#include <vector>

using namespace aiverse;
using namespace aiverse::physis;
using namespace aiverse::physis::remote;
namespace {
int fails=0;
#define CHECK(x) do {if(!(x)){std::printf("PHYSIS REMOTE FAIL %s:%d: %s\n",__FILE__,__LINE__,#x);++fails;}}while(0)
constexpr uint16_t kEdit=53011;

Json map() {
    return Json::parse(R"({"physis":{
        "enabled":true,
        "grid":{"originX":0,"originZ":0,"width":2,"height":2,
                "cellSizeMeters":100,"seed":17},
        "climate":{"baseTemperature":15,"baseHumidity":0.5},
        "enableWindZones":true,"enableGravityFields":true,"enableStorms":true,
        "floodMode":"ExternalAuthority",
        "windZones":[{"id":"wind-1","center":[0,0,0],
                      "radiusMeters":100,"velocity":[5,0,0]}]
    }})");
}
class Host final:public GameModule {
public:
    std::string name()const override{return "physis-remote-test";}
    void setup(Simulation& sim,const Json&,const Json& m)override{
        (void)installEditableFromMap(sim,m);
    }
    void applyCommand(Simulation& sim,const Command& c)override{
        if(c.type==kEdit)(void)applyFieldEditCommand(sim,c,kEdit);
    }
    void hashState(Simulation&,Hasher&)override{}
    int result(Simulation&)override{return 0;}
    bool serializeState(Simulation&,Archive& a)override{return a.ok();}
};
std::unique_ptr<Simulation> make() {
    auto s=std::make_unique<Simulation>(SimConfig{},std::make_unique<Host>());
    s->setup(Json::object(),map());
    return s;
}
FieldEditBatch edit(Fixed speed,std::uint64_t revision=0) {
    const auto j=map();
    auto options=optionsFromMap(j);
    auto fields=parseFieldAssets(j["physis"],*options);
    WindZone w=std::get<WindZone>(fields[0].data);
    w.velocity.x=speed;
    fields[0].data=w;
    return {revision,std::move(fields)};
}
const FieldRuntimeState& registry(Simulation& s) {
    return *s.world().resourceConst<FieldRuntimeState>();
}
bool readAcl(const std::string& identity) {
    return identity=="trusted-editor";
}
bool writeAcl(const std::string& identity,const Command& cmd) {
    return identity=="trusted-editor" && cmd.player==4 && cmd.type==kEdit;
}
void drive(RemoteClient& client,ServerGateway& server,Simulation& simulation,
           net::Conn& c,net::Conn& s,
           std::uint64_t start=0,std::uint64_t end=2000,
           bool advance=true) {
    for(std::uint64_t ms=start;ms<end;ms+=40) {
        client.poll(c,ms);
        server.poll(simulation,s,"trusted-editor",readAcl,writeAcl,ms);
        if(advance)simulation.step();
        server.observe(simulation);
        client.poll(c,ms+1);
    }
}

void codec() {
    auto command=Command{};
    CHECK(encodeFieldEditCommand(edit(Fixed::fromInt(13)),kEdit,command));
    command.player=4;
    Packet p; p.kind=Message::Submit; p.clientInstance=19;p.requestId=1;
    p.command=command;
    net::Bytes bytes;
    CHECK(encode(p,bytes));
    Packet decoded;
    CHECK(decode(bytes,decoded));
    CHECK(decoded.kind==Message::Submit && decoded.requestId==1);
    CHECK(decoded.command.player==4);
    FieldEditBatch parsed;
    CHECK(decodeFieldEditCommand(decoded.command,parsed));
    CHECK(parsed.desired.size()==1);
    CHECK(!bytes.empty());
    auto corrupt=bytes;
    corrupt[0]^=0xff;
    CHECK(!decode(corrupt,decoded));
    CHECK(decoded.kind==Message::Submit); // failed decode leaves output intact
    corrupt=bytes;corrupt.pop_back();
    CHECK(!decode(corrupt,decoded));
    Packet query;query.kind=Message::Query;query.clientInstance=19;query.requestId=4;
    CHECK(encode(query,bytes));
    CHECK(decode(bytes,decoded) && decoded.kind==Message::Query);
    Packet ack;ack.kind=Message::Receipt;ack.clientInstance=19;ack.requestId=4;
    ack.receipt={Result::Conflict,19,3,"stale revision"};
    CHECK(encode(ack,bytes));
    CHECK(decode(bytes,decoded));
    CHECK(decoded.receipt.result==Result::Conflict && decoded.receipt.revision==3);
    ack.receipt.detail.assign(kMaxText+1,'x');
    CHECK(!encode(ack,bytes));
}

void duplicatedPayloadConflict() {
    auto sim=make();
    ServerGateway gateway;
    auto [client,server]=net::makeLoopbackPair();
    Packet request;request.kind=Message::Submit;
    request.clientInstance=123;request.requestId=77;
    CHECK(encodeFieldEditCommand(edit(Fixed::fromInt(7)),kEdit,request.command));
    request.command.player=4;
    net::Bytes encoded;
    CHECK(encode(request,encoded));
    client->send(encoded);
    server->update(10);
    gateway.poll(*sim,*server,"trusted-editor",readAcl,writeAcl,10);
    sim->step();sim->step();sim->step();
    gateway.observe(*sim);
    CHECK(registry(*sim).revision==1);
    client->send(encoded);
    gateway.poll(*sim,*server,"trusted-editor",readAcl,writeAcl,100);
    CHECK(registry(*sim).revision==1);
    // Same principal/instance/request ID with modified bytes is forbidden.
    CHECK(encodeFieldEditCommand(edit(Fixed::fromInt(15)),kEdit,request.command));
    request.command.player=4;
    CHECK(encode(request,encoded));
    client->send(encoded);
    gateway.poll(*sim,*server,"trusted-editor",readAcl,writeAcl,120);
    CHECK(registry(*sim).revision==1);
    net::Bytes response;
    Result last=Result::Unknown;
    client->update(120);
    while(client->recv(response)) {
        Packet receipt;
        if(decode(response,receipt))last=receipt.receipt.result;
    }
    CHECK(last==Result::Rejected);
}

void concurrentAdmissions() {
    auto sim=make();
    ServerGateway gateway;
    auto [client,server]=net::makeLoopbackPair();
    Packet one,two;
    one.kind=Message::Submit;
    one.clientInstance=9191;
    one.requestId=1;
    CHECK(encodeFieldEditCommand(edit(Fixed::fromInt(39)),kEdit,one.command));
    one.command.player=4;
    two=one;
    two.requestId=2; // different request for same expected revision
    net::Bytes a,b;
    CHECK(encode(one,a));
    CHECK(encode(two,b));
    client->send(a);
    client->send(b);
    gateway.poll(*sim,*server,"trusted-editor",readAcl,writeAcl,50);
    client->update(50);
    net::Bytes response;
    bool queued=false,conflict=false;
    while(client->recv(response)) {
        Packet ack;
        if(!decode(response,ack))continue;
        if(ack.requestId==1 && ack.receipt.result==Result::Queued)queued=true;
        if(ack.requestId==2 && ack.receipt.result==Result::Conflict)conflict=true;
    }
    CHECK(queued && conflict);
    CHECK(gateway.entries().size()==1);
    for(int i=0;i<4;++i)sim->step();
    gateway.observe(*sim);
    CHECK(registry(*sim).revision==1);
}

void reconnectAndRestore() {
    auto sim=make();
    ServerGateway gateway;
    RemoteClient original(4242);
    CHECK(original.begin(edit(Fixed::fromInt(31)),kEdit,4));
    auto snapshot=original.exportOutstanding();
    auto [c1,s1]=net::makeLoopbackPair();
    original.poll(*c1,0);
    gateway.poll(*sim,*s1,"trusted-editor",readAcl,writeAcl,0);
    // Simulate losing response before the next client poll.
    sim->step();sim->step();sim->step();
    gateway.observe(*sim);
    CHECK(registry(*sim).revision==1);
    c1->close();s1->close();
    RemoteClient restored(4242);
    CHECK(restored.restoreOutstanding(snapshot));
    CHECK(restored.requestId()==original.requestId());
    auto [c2,s2]=net::makeLoopbackPair();
    drive(restored,gateway,*sim,*c2,*s2,100,1400);
    CHECK(restored.status()==ClientStatus::Applied);
    CHECK(registry(*sim).revision==1);
    CHECK(gateway.entries().size()==1);
}

void latencyAndReorderSimulation() {
    auto sim=make();
    ServerGateway gateway;
    RemoteClient client(573112);
    CHECK(client.begin(edit(Fixed::fromInt(44)),kEdit,4));
    // A reliable Conn still has latency/jitter, and the receipt may arrive
    // well after the request has been executed on the authoritative tick.
    auto [c,s]=net::makeLoopbackPair({75,25,42});
    drive(client,gateway,*sim,*c,*s,0,4500);
    CHECK(client.status()==ClientStatus::Applied);
    CHECK(registry(*sim).revision==1);
    const auto entity=registry(*sim).byId.at("wind-1").entity;
    CHECK(sim->world().get<WindZone>(entity).velocity.x==Fixed::fromInt(44));
}

void denied() {
    auto sim=make();
    ServerGateway gateway;
    RemoteClient client(99);
    CHECK(client.begin(edit(Fixed::fromInt(51)),kEdit,4));
    auto [c,s]=net::makeLoopbackPair();
    for(std::uint64_t ms=0;ms<1200;ms+=40) {
        client.poll(*c,ms);
        gateway.poll(*sim,*s,"unauthorized",readAcl,writeAcl,ms);
        client.poll(*c,ms+1);
    }
    CHECK(client.status()==ClientStatus::Denied);
    CHECK(registry(*sim).revision==0);
}

void editorBridge() {
    using aiverse::editor::physis_remote::EditorLink;
    auto sim=make();
    ServerGateway server;
    EditorLink link;
    CHECK(!link.attached());
    CHECK(!link.submit(map()));
    auto [clientConn,serverConn]=net::makeLoopbackPair();
    std::string error;
    CHECK(link.attachAuthenticated(std::move(clientConn),91372,4,kEdit,&error));
    // First query a fresh unknown request ID: server tells us its
    // authoritative current revision without applying any changes.
    for(std::uint64_t ms=0;ms<600;ms+=40) {
        link.poll(ms);
        server.poll(*sim,*serverConn,"trusted-editor",readAcl,writeAcl,ms);
        link.poll(ms+1);
    }
    CHECK(link.client()!=nullptr);
    CHECK(link.client()->hasRemoteRevision());
    CHECK(link.client()->remoteRevision()==0);
    Json modified=map();
    Json config=modified["physis"];
    Json winds=config["windZones"];
    Json wind=winds[0];
    wind.set("velocity",Json::parse("[24,0,0]"));
    winds.at(0)=wind;
    config.set("windZones",winds);
    modified.set("physis",config);
    CHECK(link.submit(modified,&error));
    CHECK(!link.saveOutstanding().empty());
    for(std::uint64_t ms=600;ms<2200;ms+=40) {
        link.poll(ms);
        server.poll(*sim,*serverConn,"trusted-editor",readAcl,writeAcl,ms);
        sim->step();
        server.observe(*sim);
        link.poll(ms+1);
    }
    CHECK(link.client()->status()==ClientStatus::Applied);
    CHECK(registry(*sim).revision==1);
    CHECK(link.client()->remoteRevision()==1);
    CHECK(link.saveOutstanding().empty());
    const auto id=registry(*sim).byId.at("wind-1").entity;
    CHECK(sim->world().get<WindZone>(id).velocity.x==Fixed::fromInt(24));
    link.disconnect();
    CHECK(!link.connected());
}


void appliedReceiptAfterRollback() {
    auto sim=make();
    SimSnapshot original;
    sim->saveState(original);
    ServerGateway gateway;
    RemoteClient client(22288);
    CHECK(client.begin(edit(Fixed::fromInt(29)),kEdit,4));
    auto [c1,s1]=net::makeLoopbackPair();
    drive(client,gateway,*sim,*c1,*s1,0,2200);
    CHECK(client.status()==ClientStatus::Applied);
    CHECK(registry(*sim).revision==1);
    c1->close();s1->close();
    client.disconnected();
    sim->loadState(original);
    CHECK(registry(*sim).revision==0);
    gateway.observe(*sim);
    auto [c2,s2]=net::makeLoopbackPair();
    drive(client,gateway,*sim,*c2,*s2,2300,3800);
    CHECK(client.status()==ClientStatus::Conflict);
    CHECK(registry(*sim).revision==0);
}
// Review fix: a LATER legitimate edit (revision 1 -> 2) is not a rollback;
// the historical Applied receipt of revision 1 must stay Applied.
void appliedReceiptSurvivesLaterEdit() {
    auto sim=make();
    ServerGateway gateway;
    RemoteClient client(22301);
    CHECK(client.begin(edit(Fixed::fromInt(31)),kEdit,4));
    auto [c1,s1]=net::makeLoopbackPair();
    drive(client,gateway,*sim,*c1,*s1,0,2200);
    CHECK(client.status()==ClientStatus::Applied);
    CHECK(registry(*sim).revision==1);
    Command later;
    CHECK(encodeFieldEditCommand(edit(Fixed::fromInt(45),1),kEdit,later));
    later.tick=sim->tick();
    CHECK(applyFieldEditCommand(*sim,later,kEdit));
    CHECK(registry(*sim).revision==2);
    sim->step();
    gateway.observe(*sim);
    CHECK(gateway.entries().size()==1);
    CHECK(gateway.entries().begin()->second.receipt.result==Result::Applied);
    CHECK(gateway.entries().begin()->second.receipt.revision==1);
}
void queuedReceiptInvalidatedByRollback() {
    auto sim=make();
    SimSnapshot checkpoint;
    sim->saveState(checkpoint);
    ServerGateway gateway;
    RemoteClient client(83917);
    CHECK(client.begin(edit(Fixed::fromInt(37)),kEdit,4));
    auto [c1,s1]=net::makeLoopbackPair();
    // Deliver the request but do not advance Simulation: receipt is Queued.
    for(std::uint64_t ms=0;ms<400;ms+=40) {
        client.poll(*c1,ms);
        gateway.poll(*sim,*s1,"trusted-editor",readAcl,writeAcl,ms);
        client.poll(*c1,ms+1);
    }
    CHECK(registry(*sim).revision==0);
    CHECK(client.status()!=ClientStatus::Applied);
    c1->close();s1->close();
    client.disconnected();
    // Roll back before the command commits; the old in-memory queue and
    // scheduled tick must not be treated as an Applied acknowledgement.
    sim->loadState(checkpoint);
    gateway.observe(*sim);
    auto [c2,s2]=net::makeLoopbackPair();
    drive(client,gateway,*sim,*c2,*s2,500,1900,false);
    CHECK(client.status()!=ClientStatus::Applied);
    CHECK(registry(*sim).revision==0);
}
void unhealthyJournalStopsAutomaticResubmit() {
    // Unknown alone permits same-ID recovery, but Unknown explicitly marked
    // journal-unavailable must leave the client in Uncertain, not a busy
    // retry loop that the server cannot safely deduplicate.
    for(const int failOnWrite : {2,3}) {
        auto sim=make();
        ServerGateway gateway;
        int writes=0;
        gateway.setJournalWriter([&](const auto&,std::string* error) {
            if(++writes>=failOnWrite) {
                if(error)*error="injected journal failure";
                return false;
            }
            return true;
        });
        RemoteClient client(70800+std::uint64_t(failOnWrite));
        CHECK(client.begin(edit(Fixed::fromInt(41)),kEdit,4));
        auto [editorConn,hostConn]=net::makeLoopbackPair();
        std::uint64_t stoppedAt=0;
        for(std::uint64_t ms=0;ms<3000;ms+=40) {
            client.poll(*editorConn,ms);
            gateway.poll(*sim,*hostConn,"trusted-editor",
                         readAcl,writeAcl,ms);
            sim->step();
            gateway.observe(*sim);
            client.poll(*editorConn,ms+1);
            if(!gateway.journalHealthy() &&
               client.status()==ClientStatus::Uncertain) {
                stoppedAt=ms+1;
                break;
            }
        }
        CHECK(stoppedAt>0);
        CHECK(!gateway.journalHealthy());
        CHECK(client.status()==ClientStatus::Uncertain);
        CHECK(client.receipt().result==Result::Unknown);
        CHECK(!client.exportOutstanding().empty());
        // The client may still receive transport heartbeats internally,
        // but must not emit further PHYSIS application Submit/Query frames.
        for(std::uint64_t ms=stoppedAt+100;ms<stoppedAt+2000;ms+=100) {
            client.poll(*editorConn,ms);
            net::Bytes extra;
            CHECK(!hostConn->recv(extra));
        }
    }
}
void manualRetryAfterJournalRecovery() {
    auto sim=make();
    ServerGateway failing;
    std::map<ServerGateway::Key,ServerGateway::Entry> durable;
    int writes=0;
    // Persist the intent and Queued receipt, then lose receipt writes
    // after the World edit commits. The process sees Unknown/Uncertain.
    failing.setJournalWriter([&](const auto& entries,std::string* error) {
        if(++writes>=3) {
            if(error)*error="injected Applied receipt disk error";
            return false;
        }
        durable=entries;
        return true;
    });
    RemoteClient client(75511);
    CHECK(client.begin(edit(Fixed::fromInt(42)),kEdit,4));
    const auto originalId=client.requestId();
    const auto originalBytes=client.exportOutstanding();
    auto [c1,s1]=net::makeLoopbackPair();
    std::uint64_t uncertainAt=0;
    for(std::uint64_t ms=0;ms<3000;ms+=40) {
        client.poll(*c1,ms);
        failing.poll(*sim,*s1,"trusted-editor",readAcl,writeAcl,ms);
        sim->step();
        failing.observe(*sim);
        client.poll(*c1,ms+1);
        if(client.status()==ClientStatus::Uncertain) {
            uncertainAt=ms+1;
            break;
        }
    }
    CHECK(uncertainAt>0);
    CHECK(!failing.journalHealthy());
    CHECK(registry(*sim).revision==1);
    CHECK(durable.size()==1);
    CHECK(client.status()==ClientStatus::Uncertain);
    CHECK(client.exportOutstanding()==originalBytes);
    if(durable.size()!=1)return;
    CHECK(durable.begin()->second.receipt.result==Result::Queued);

    // A delayed pre-failure positive receipt is not proof of durability.
    // It must not clear the suspension or restart automatic traffic.
    Packet delayed;
    delayed.kind=Message::Receipt;
    delayed.clientInstance=client.instance();
    delayed.requestId=originalId;
    delayed.receipt.result=Result::Queued;
    delayed.receipt.revision=0;
    net::Bytes delayedFrame;
    CHECK(encode(delayed,delayedFrame));
    s1->send(delayedFrame);
    client.poll(*c1,uncertainAt+5);
    CHECK(client.status()==ClientStatus::Uncertain);
    CHECK(client.receipt().result==Result::Unknown);
    net::Bytes extra;
    CHECK(!s1->recv(extra));

    // On restart the original Queued journal is no longer evidence
    // that the pending command survived. Observe the RESTORED ECS World.
    ServerGateway recovered;
    std::string error;
    CHECK(recovered.restoreJournal(durable,&error));
    CHECK(recovered.entries().begin()->second.receipt.result==Result::Unknown);
    recovered.setJournalWriter([](const auto&,std::string*){return true;});
    recovered.observe(*sim);
    CHECK(recovered.entries().begin()->second.receipt.result==Result::Applied);
    c1->close();s1->close();
    client.disconnected();
    CHECK(client.status()==ClientStatus::Uncertain);

    // Reconnection MUST NOT silently restart application command traffic.
    auto [c2,s2]=net::makeLoopbackPair();
    for(std::uint64_t ms=uncertainAt+100;ms<uncertainAt+500;ms+=40) {
        client.poll(*c2,ms);
        net::Bytes unexpected;
        CHECK(!s2->recv(unexpected));
    }
    CHECK(client.status()==ClientStatus::Uncertain);

    // Explicit user-initiated retry first Queries the SAME original ID.
    client.retry();
    CHECK(client.requestId()==originalId);
    for(std::uint64_t ms=uncertainAt+600;ms<uncertainAt+2000;ms+=40) {
        client.poll(*c2,ms);
        recovered.poll(*sim,*s2,"trusted-editor",readAcl,writeAcl,ms);
        client.poll(*c2,ms+1);
        if(client.status()==ClientStatus::Applied)break;
    }
    CHECK(client.status()==ClientStatus::Applied);
    CHECK(registry(*sim).revision==1);
    CHECK(client.requestId()==originalId);
    CHECK(client.remoteRevision()==1);
}

void ordinaryUnknownStillRetriesOriginal() {
    auto sim=make();
    RemoteClient original(85511);
    CHECK(original.begin(edit(Fixed::fromInt(43)),kEdit,4));
    const auto bytes=original.exportOutstanding();
    RemoteClient restored(85511);
    CHECK(restored.restoreOutstanding(bytes));
    const auto id=restored.requestId();
    ServerGateway server;
    auto [c,s]=net::makeLoopbackPair();
    drive(restored,server,*sim,*c,*s,0,2100);
    CHECK(restored.status()==ClientStatus::Applied);
    CHECK(restored.requestId()==id);
    CHECK(registry(*sim).revision==1);
    CHECK(server.entries().size()==1);
}

void delayedReceiptCannotUndoApplied() {
    auto sim=make();
    ServerGateway gateway;
    RemoteClient client(99640);
    CHECK(client.begin(edit(Fixed::fromInt(71)),kEdit,4));
    const auto originalId=client.requestId();
    auto [c,s]=net::makeLoopbackPair();
    drive(client,gateway,*sim,*c,*s,0,2100);
    CHECK(client.status()==ClientStatus::Applied);
    CHECK(registry(*sim).revision==1);
    // Buffered ACKs on the same authenticated connection may arrive after
    // Applied. They are not new decisions and cannot resurrect an old edit.
    std::uint64_t ms=2200;
    for(const Result old : {Result::Queued,Result::Unknown,Result::Rejected,
                            Result::Conflict,Result::Denied}) {
        Packet stale;
        stale.kind=Message::Receipt;
        stale.clientInstance=client.instance();
        stale.requestId=originalId;
        stale.receipt={old,0,0,"stale buffered receipt"};
        net::Bytes raw;
        CHECK(encode(stale,raw));
        s->send(raw);
        client.poll(*c,ms);
        ms+=40;
        CHECK(client.status()==ClientStatus::Applied);
        CHECK(client.receipt().result==Result::Applied);
        CHECK(client.remoteRevision()==1);
    }
    // The terminal result must permit the next revision, not leave an
    // unexpected Queued/Unknown request blocking the editor.
    CHECK(client.begin(edit(Fixed::fromInt(72),1),kEdit,4));
    drive(client,gateway,*sim,*c,*s,2500,4500);
    CHECK(client.status()==ClientStatus::Applied);
    CHECK(registry(*sim).revision==2);
}

void appliedRecheckNeedsFreshQueryAndManualReconcile() {
    auto sim=make();
    ServerGateway gateway;
    RemoteClient client(99641);
    CHECK(client.begin(edit(Fixed::fromInt(73)),kEdit,4));
    const auto requestId=client.requestId();
    auto [c1,s1]=net::makeLoopbackPair();
    drive(client,gateway,*sim,*c1,*s1,0,2100);
    CHECK(client.status()==ClientStatus::Applied);
    CHECK(registry(*sim).revision==1);
    c1->close();s1->close();
    client.disconnected();

    // Connection may carry an old Queued before/after the new Query.
    auto [c2,s2]=net::makeLoopbackPair();
    Packet stale;
    stale.kind=Message::Receipt;
    stale.clientInstance=client.instance();
    stale.requestId=requestId;
    stale.receipt={Result::Queued,0,0,"stale pre-commit"};
    net::Bytes raw;
    CHECK(encode(stale,raw));
    s2->send(raw);
    client.poll(*c2,2200); // ignore pre-query frame, send Query
    CHECK(client.status()==ClientStatus::Applied);
    s2->send(raw);
    client.poll(*c2,2201); // ignore old Queued while verifying
    CHECK(client.status()==ClientStatus::Applied);

    // Missing receipt after server restart is genuinely uncertain;
    // it must not auto-replay a once-Applied command.
    ServerGateway restarted;
    restarted.poll(*sim,*s2,"trusted-editor",readAcl,writeAcl,2240);
    client.poll(*c2,2241);
    CHECK(client.status()==ClientStatus::Uncertain);
    CHECK(client.receipt().result==Result::Unknown);
    CHECK(!client.exportOutstanding().empty());
    CHECK(client.requestId()==requestId);
    s2->send(raw);
    client.poll(*c2,2300);
    CHECK(client.status()==ClientStatus::Uncertain);
    net::Bytes unwanted;
    CHECK(!s2->recv(unwanted));

    // Only a deliberate same-ID retry may proceed. The authoritative
    // revision prevents a second effect even without the old receipt cache.
    client.retry();
    drive(client,restarted,*sim,*c2,*s2,2400,4000);
    CHECK(client.status()==ClientStatus::Conflict);
    CHECK(registry(*sim).revision==1);
    CHECK(client.requestId()==requestId);
}


void appliedRecheckTimeoutCanRetryOnlyQuery() {
    auto sim=make();
    ServerGateway gateway;
    RemoteClient client(99642);
    CHECK(client.begin(edit(Fixed::fromInt(74)),kEdit,4));
    const auto id=client.requestId();
    auto [c1,s1]=net::makeLoopbackPair();
    drive(client,gateway,*sim,*c1,*s1,0,2100);
    CHECK(client.status()==ClientStatus::Applied);
    CHECK(registry(*sim).revision==1);
    c1->close();s1->close();
    client.disconnected();

    // No server consumes the verification queries; after a bounded number
    // of attempts the client must not silently allow a new edit.
    auto [c2,s2]=net::makeLoopbackPair();
    for(std::uint64_t ms=2200;ms<30000;ms+=200)
        client.poll(*c2,ms);
    CHECK(client.status()==ClientStatus::Uncertain);
    CHECK(client.receipt().result==Result::Applied);
    CHECK(client.requestId()==id);
    CHECK(!client.begin(edit(Fixed::fromInt(75),1),kEdit,4));

    // Manual Retry verifies the SAME ID with Query only, not Submit.
    c2->close();s2->close();
    client.disconnected();
    client.retry();
    auto [c3,s3]=net::makeLoopbackPair();
    client.poll(*c3,31000);
    s3->update(31000);
    net::Bytes wire;
    CHECK(s3->recv(wire));
    Packet sent;
    CHECK(decode(wire,sent));
    CHECK(sent.kind==Message::Query);
    CHECK(sent.requestId==id);
    // The server's original confirmed outcome can now be revalidated.
    Packet reply;
    reply.kind=Message::Receipt;
    reply.clientInstance=client.instance();
    reply.requestId=id;
    reply.receipt={Result::Applied,0,1,"verified after timeout"};
    net::Bytes confirmed;
    CHECK(encode(reply,confirmed));
    s3->send(confirmed);
    client.poll(*c3,31001);
    CHECK(client.status()==ClientStatus::Applied);
    CHECK(registry(*sim).revision==1);
}

void initialRevisionProbe() {
    auto sim=make();
    ServerGateway gateway;
    RemoteClient client(12555);
    CHECK(client.requestRevision());
    auto [c,s]=net::makeLoopbackPair();
    drive(client,gateway,*sim,*c,*s,0,800,false);
    CHECK(client.hasRemoteRevision());
    CHECK(client.remoteRevision()==0);
    CHECK(!client.probingRevision());
    CHECK(registry(*sim).revision==0);
    CHECK(client.begin(edit(Fixed::fromInt(8)),kEdit,4));
    drive(client,gateway,*sim,*c,*s,800,2600);
    CHECK(client.status()==ClientStatus::Applied);
    CHECK(registry(*sim).revision==1);
    CHECK(client.remoteRevision()==1);
}

// A revision probe on an unhealthy journal must never mark the client
// synchronized just because the ECS revision number is available.
// The operator may explicitly probe again after a fresh healthy server.
void revisionProbeStopsOnUnhealthyJournal() {
    auto sim=make();
    ServerGateway unhealthy;
    unhealthy.suspendJournal();
    RemoteClient client(99231);
    CHECK(client.requestRevision());
    auto [c,s]=net::makeLoopbackPair();
    drive(client,unhealthy,*sim,*c,*s,0,800,false);
    CHECK(!client.probingRevision());
    CHECK(!client.hasRemoteRevision());
    CHECK(client.status()==ClientStatus::Uncertain);
    CHECK(client.receipt().result==Result::Unknown);
    CHECK(journalRetrySuspended(client.receipt().detail));
    CHECK(registry(*sim).revision==0);

    // A fresh durable host can be probed explicitly. The previously
    // indeterminate probe must not auto-retry or cause a phantom edit.
    ServerGateway recovered;
    CHECK(client.requestRevision());
    drive(client,recovered,*sim,*c,*s,800,1600,false);
    CHECK(!client.probingRevision());
    CHECK(client.hasRemoteRevision());
    CHECK(client.remoteRevision()==0);
    CHECK(client.status()==ClientStatus::Idle);
    CHECK(client.receipt().detail.empty());
    CHECK(recovered.entries().empty());
    CHECK(registry(*sim).revision==0);
}

void conflictAfterServerRestart() {
    auto sim=make();
    ServerGateway gateway;
    RemoteClient client(4142);
    CHECK(client.begin(edit(Fixed::fromInt(9)),kEdit,4));
    auto saved=client.exportOutstanding();
    auto [c1,s1]=net::makeLoopbackPair();
    client.poll(*c1,0);
    gateway.poll(*sim,*s1,"trusted-editor",readAcl,writeAcl,0);
    for(int i=0;i<3;++i)sim->step();
    gateway.observe(*sim);
    CHECK(registry(*sim).revision==1);
    c1->close();s1->close();
    // Cache lost (new server), but World's revision is authoritative:
    // resend cannot apply twice; server responds with conflict.
    ServerGateway restartedGateway;
    RemoteClient restored(4142);
    CHECK(restored.restoreOutstanding(saved));
    auto [c2,s2]=net::makeLoopbackPair();
    drive(restored,restartedGateway,*sim,*c2,*s2,100,2000);
    CHECK(restored.status()==ClientStatus::Conflict);
    CHECK(registry(*sim).revision==1);
}
}
int main() {
    codec();
    duplicatedPayloadConflict();
    concurrentAdmissions();
    reconnectAndRestore();
    editorBridge();
    latencyAndReorderSimulation();
    delayedReceiptCannotUndoApplied();
    appliedRecheckNeedsFreshQueryAndManualReconcile();
    appliedRecheckTimeoutCanRetryOnlyQuery();
    initialRevisionProbe();
    revisionProbeStopsOnUnhealthyJournal();
    unhealthyJournalStopsAutomaticResubmit();
    manualRetryAfterJournalRecovery();
    ordinaryUnknownStillRetriesOriginal();
    appliedReceiptAfterRollback();
    appliedReceiptSurvivesLaterEdit();
    queuedReceiptInvalidatedByRollback();
    denied();
    conflictAfterServerRestart();
    std::printf("PHYSIS remote edit: %s\n",fails?"FAIL":"PASS");
    return fails?1:0;
}
