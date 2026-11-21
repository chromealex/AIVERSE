// PHYSIS Stage 26: a REAL, poll-driven admin-only WebSocket listener.
// Intentional security boundary: binds to LOOPBACK ONLY. Off-machine users
// need an external authenticated TLS tunnel/secure host-owned net::Conn.
// Never append PHYSIS admin permissions to normal match InputFrame.
#pragma once
#include "PhysisAdminAuth.h"
#include "PhysisRemoteJournal.h"
#include "PhysisJournalProcessLock.h"
#include "engine/net/WebSocket.h"
#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::physis::remote::admin {
#ifndef __EMSCRIPTEN__
class LoopbackAdminEndpoint {
public:
    // Unauthenticated peers get a small separate quota and a short
    // handshake deadline, so anonymous loopback sockets cannot occupy the
    // slots reserved for already authenticated editors.
    static constexpr std::size_t kMaxPeers=16;
    static constexpr std::size_t kMaxUnauthenticatedPeers=4;
    static constexpr std::uint64_t kHandshakeTimeoutMs=2000;
    using KeyLookup=ServerHandshake::KeyLookup;
    using ReadAcl=std::function<bool(const std::string&)>;
    using WriteAcl=std::function<bool(const std::string&,const Command&)>;

    explicit LoopbackAdminEndpoint(std::filesystem::path journal)
        :journal_(std::move(journal)){}
    // Load the journal BEFORE binding the listener; corrupted history
    // must disable admission rather than silently forgetting request IDs.
    bool open(std::uint16_t port,std::uint16_t reservedFieldCommandType,
              KeyLookup keys,ReadAcl read,WriteAcl write,
              std::string* error=nullptr) {
        if(!reservedFieldCommandType || !keys || !read || !write) {
            if(error)*error="PHYSIS admin requires credential lookup and ACL";
            return false;
        }
        if(!journalLock_.acquire(journal_.path(),error))return false;
        gateway_.setReservedCommandType(reservedFieldCommandType);
#ifndef _WIN32
        const int journalDir=journalLock_.directoryFd();
        if(journalDir<0 || !journal_.loadPinned(journalDir,gateway_,error)) {
            if(journalDir<0 && error)
                *error="PHYSIS admin journal directory ownership lost";
            journalLock_.release();
            return false;
        }
#else
        if(!journal_.attach(gateway_,error)) {
            journalLock_.release();
            return false;
        }
#endif
        // Loading may take time or traverse a large (bounded) journal.
        // A same-user process could rename/unlink the live lock or parent
        // during that read. Do not bind a privileged listener unless the
        // verified directory/lock identity is still intact afterwards.
        if(!journalLock_.valid()) {
            gateway_.suspendJournal();
            journalLock_.release();
            if(error)*error="PHYSIS journal lock ownership changed during startup";
            return false;
        }
        // A previous lock inode can be unlinked while flock is still
        // held. Validate path ownership before EVERY journal write.
        gateway_.setJournalWriter([this](
            const std::map<ServerGateway::Key,ServerGateway::Entry>& entries,
            std::string* why) {
            if(!journalLock_.valid()) {
                if(why)*why="PHYSIS admin journal lock path was replaced";
                return false;
            }
#ifndef _WIN32
            const int parentFd=journalLock_.directoryFd();
            if(parentFd<0) {
                if(why)*why="PHYSIS admin journal directory ownership lost";
                return false;
            }
            const bool saved=journal_.savePinned(parentFd,entries,why);
#else
            const bool saved=journal_.save(entries,why);
#endif
            // Journal bytes are not safe to acknowledge if ownership was
            // lost during I/O, even if the atomic rename already succeeded.
            if(!journalLock_.valid()) {
                if(why)*why="PHYSIS admin journal ownership changed during write";
                return false;
            }
            return saved;
        });
        if(!listener_.open(port,true)) {
            journalLock_.release();
            if(error)*error="PHYSIS loopback admin WebSocket listener failed";
            return false;
        }
        keys_=std::move(keys);
        read_=std::move(read);
        write_=std::move(write);
        if(error)error->clear();
        return true;
    }
    std::uint16_t port()const{return listener_.port();}
    ServerGateway& gateway(){return gateway_;}
    const ServerGateway& gateway()const{return gateway_;}
    std::size_t connections()const{return peers_.size();}
    bool journalHealthy()const {
        return journalLock_.valid() && gateway_.journalHealthy();
    }

    void poll(Simulation& sim,std::uint64_t nowMs) {
        // Fail closed BEFORE ingress, including requests on existing peers.
        // Cached receipts become Unknown when the journal is unsafe.
        if(!journalLock_.valid())gateway_.suspendJournal();
        auto newlyConnected=listener_.poll(nowMs);
        std::size_t pending=0;
        for(const auto& peer:peers_)
            if(peer && !peer->handshake.authenticated() &&
               peer->conn->state()!=net::ConnState::Closed)++pending;
        for(auto& conn:newlyConnected) {
            if(peers_.size()>=kMaxPeers ||
               pending>=kMaxUnauthenticatedPeers) {conn->close();continue;}
            peers_.push_back(std::make_unique<Peer>(
                std::move(conn),keys_,nowMs));
            ++pending;
        }
        for(auto& peer:peers_) {
            if(!peer || peer->conn->state()==net::ConnState::Closed)continue;
            peer->conn->update(nowMs);
            if(!peer->handshake.authenticated()) {
                if(nowMs>=peer->createdAt &&
                   nowMs-peer->createdAt>kHandshakeTimeoutMs) {
                    peer->conn->close();continue;
                }
                net::Bytes message;
                int attempts=0;
                while(attempts++<4 && peer->conn->recv(message)) {
                    if(!peer->handshake.consume(*peer->conn,message)) {
                        peer->conn->close();break;
                    }
                    if(peer->handshake.authenticated())break;
                }
            }
            if(peer->handshake.authenticated() &&
               peer->conn->state()==net::ConnState::Connected)
                gateway_.poll(sim,*peer->conn,peer->handshake.principal(),
                              read_,write_,nowMs);
        }
        peers_.erase(std::remove_if(peers_.begin(),peers_.end(),
            [](const auto& peer) {
                return !peer || peer->conn->state()==net::ConnState::Closed;
            }),peers_.end());
    }
    // Host calls after committed Simulation::step, not from editor/rendering.
    void afterTick(const Simulation& sim) {
        if(!journalLock_.valid())gateway_.suspendJournal();
        gateway_.observe(sim);
    }

private:
    struct Peer {
        std::unique_ptr<net::Conn> conn;
        ServerHandshake handshake;
        std::uint64_t createdAt;
        Peer(std::unique_ptr<net::Conn> c,const KeyLookup& lookup,
             std::uint64_t now):conn(std::move(c)),handshake(lookup),
                                  createdAt(now){}
    };
    // Member destruction is reverse declaration order. Keep ownership until
    // AFTER peers, gateway and listener have been destroyed during shutdown.
    JournalProcessLock journalLock_;
    net::WsListener listener_;
    FileJournal journal_;
    ServerGateway gateway_;
    KeyLookup keys_;
    ReadAcl read_;
    WriteAcl write_;
    std::vector<std::unique_ptr<Peer>> peers_;
};

// Native connectWebSocket currently has NO WSS implementation. This connector
// thus enforces an explicitly loopback-only ws URL; off-host access must be
// via a host-owned authenticated encrypted tunnel landing on loopback.
class LoopbackAdminConnector {
public:
    bool connect(const std::string& url,std::string principal,net::Key key,
                 std::string* error=nullptr) {
        const bool local=url.rfind("ws://localhost:",0)==0 ||
            url.rfind("ws://127.0.0.1:",0)==0 ||
            url.rfind("ws://[::1]:",0)==0;
        if(!local || !validPrincipal(principal) || net::isZero(key)) {
            if(error)*error="PHYSIS admin client requires a loopback ws URL and credentials";
            net::wipe(key.data(),key.size());
            return false;
        }
        auto conn=net::connectWebSocket(url);
        if(!conn) {
            if(error)*error="PHYSIS local admin connection failed";
            net::wipe(key.data(),key.size());
            return false;
        }
        handshake_=std::make_unique<ClientHandshake>(std::move(principal),key);
        net::wipe(key.data(),key.size());
        conn_=std::move(conn);
        began_=false;
        started_=0;
        if(error)error->clear();
        return true;
    }
    void poll(std::uint64_t nowMs) {
        if(!conn_ || !handshake_)return;
        if(!started_)started_=nowMs?nowMs:1;
        conn_->update(nowMs);
        if(conn_->state()==net::ConnState::Closed)return;
        if(nowMs>started_ && nowMs-started_>10000 &&
           !handshake_->authenticated()) {
            conn_->close();
            return;
        }
        if(conn_->state()==net::ConnState::Connected && !began_) {
            began_=handshake_->begin(*conn_);
            if(!began_)conn_->close();
        }
        net::Bytes msg;
        int count=0;
        while(began_ && count++<4 && conn_->recv(msg)) {
            if(!handshake_->consume(*conn_,msg)) {
                conn_->close();
                break;
            }
            if(handshake_->authenticated())break;
        }
    }
    bool ready()const {
        return conn_ && handshake_ && handshake_->authenticated() &&
               conn_->state()==net::ConnState::Connected;
    }
    bool rejected()const {
        return (handshake_ && handshake_->rejected()) ||
               (conn_ && conn_->state()==net::ConnState::Closed);
    }
    std::unique_ptr<net::Conn> takeAuthenticated() {
        if(!ready())return {};
        handshake_.reset(); // wipe credential copy
        return std::move(conn_);
    }
private:
    std::unique_ptr<net::Conn> conn_;
    std::unique_ptr<ClientHandshake> handshake_;
    bool began_=false;
    std::uint64_t started_=0;
};
#endif // native
} // namespace aiverse::physis::remote::admin
