#pragma once
#include "engine/net/Conn.h"
#include <algorithm>
#include <functional>
#include <optional>
#include <string>
#include <utility>

namespace aiverse::lib::session {
// Host-only authentication. Times use the host's chosen monotonic millisecond clock.
// Credentials must come from a trusted verifier; never take player/slot from the peer.
struct Principal {std::string player,audience;uint8_t slot=0;uint64_t expiresAt=0;};
struct Access {Principal principal;bool revoked=false;};
namespace detail {
inline net::Bytes packet(uint8_t kind,const std::string& token={}) {
    net::Bytes bytes{'A','I','V','A',1,kind};bytes.insert(bytes.end(),token.begin(),token.end());return bytes;
}
inline bool header(const net::Bytes& b,uint8_t kind){return b.size()>=6&&b[0]=='A'&&b[1]=='I'&&b[2]=='V'&&b[3]=='A'&&b[4]==1&&b[5]==kind;}
inline bool tokenValid(const std::string& token){return !token.empty()&&token.size()<=256&&std::all_of(token.begin(),token.end(),[](unsigned char c){return c>=33&&c<=126;});}
}
// This decorator sends only admission before exposing Connected to the game protocol.
class ClientAdmission final:public net::Conn {
public:
    ClientAdmission(std::unique_ptr<net::Conn> connection,std::string token):connection_(std::move(connection)),token_(std::move(token)){
        if(!connection_||!detail::tokenValid(token_)){closed_=true;if(connection_)connection_->close();}
    }
    ~ClientAdmission()override{close();}
    void send(const net::Bytes& b)override{if(state()==net::ConnState::Connected)connection_->send(b);}
    void sendLatest(const net::Bytes& b)override{if(state()==net::ConnState::Connected)connection_->sendLatest(b);}
    bool recv(net::Bytes& b)override{return state()==net::ConnState::Connected&&connection_->recv(b);}
    void update(uint64_t now)override{
        if(closed_)return;
        connection_->update(now);
        if(!started_){started_=true;born_=now;}
        if(now<born_||(!ready_&&now-born_>=10000)){close();return;}
        if(!sent_&&connection_->state()==net::ConnState::Connected){connection_->send(detail::packet(1,token_));token_.clear();sent_=true;}
        if(!ready_){net::Bytes b;if(connection_->recv(b)){if(b.size()!=6||!detail::header(b,2)){close();return;}ready_=true;}}
    }
    net::ConnState state()const override{
        if(closed_||!connection_||connection_->state()==net::ConnState::Closed)return net::ConnState::Closed;
        return ready_?net::ConnState::Connected:net::ConnState::Connecting;
    }
    void close()override{closed_=true;token_.clear();if(connection_)connection_->close();}
    uint32_t rttMs()const override{return connection_?connection_->rttMs():0;}
    std::string describe()const override{return "admitted client";} // never expose bearer tokens
private:
    std::unique_ptr<net::Conn> connection_;std::string token_;uint64_t born_=0;
    bool started_=false,sent_=false,ready_=false,closed_=false;
};
class AuthorizedConnection final:public net::Conn {
public:
    AuthorizedConnection(std::unique_ptr<net::Conn> connection,std::shared_ptr<Access> access):connection_(std::move(connection)),access_(std::move(access)){}
    ~AuthorizedConnection()override{close();}
    void send(const net::Bytes& b)override{if(state()==net::ConnState::Connected)connection_->send(b);}
    void sendLatest(const net::Bytes& b)override{if(state()==net::ConnState::Connected)connection_->sendLatest(b);}
    bool recv(net::Bytes& b)override{return state()==net::ConnState::Connected&&connection_->recv(b);}
    void update(uint64_t now)override{
        if(!connection_||!access_)return;
        if(access_->revoked||now>=access_->principal.expiresAt){close();return;}
        connection_->update(now);
    }
    net::ConnState state()const override{return !connection_||!access_||access_->revoked?net::ConnState::Closed:connection_->state();}
    void close()override{if(access_)access_->revoked=true;if(connection_)connection_->close();}
    uint32_t rttMs()const override{return connection_?connection_->rttMs():0;}
    std::string describe()const override{return "authorized game connection";}
private:
    std::unique_ptr<net::Conn> connection_;std::shared_ptr<Access> access_;
};
struct Admission {std::shared_ptr<Access> access;std::unique_ptr<net::Conn> connection;};
class AdmissionGate {
public:
    using Verify=std::function<std::optional<Principal>(const std::string& token,const std::string& audience,uint64_t now)>;
    explicit AdmissionGate(std::string audience):audience_(std::move(audience)){}
    bool enqueue(std::unique_ptr<net::Conn> conn,uint64_t now){
        if(!conn)return false;
        if(audience_.empty()||audience_.size()>128||pending_.size()>=64){conn->close();return false;}
        pending_.push_back({std::move(conn),now});return true;
    }
    std::vector<Admission> poll(uint64_t now,const Verify& verify){
        std::vector<Admission> accepted;
        for(auto it=pending_.begin();it!=pending_.end();){
            it->connection->update(now);net::Bytes bytes;bool remove=false;
            if(now<it->born||now-it->born>=10000||it->connection->state()==net::ConnState::Closed)remove=true;
            else if(it->connection->recv(bytes)){
                remove=true;
                if(bytes.size()>6&&bytes.size()<=262&&detail::header(bytes,1)&&verify){
                    std::string token(bytes.begin()+6,bytes.end());
                    if(detail::tokenValid(token)){
                        auto principal=verify(token,audience_,now);
                        if(principal&&!principal->player.empty()&&principal->player.size()<=128&&principal->audience==audience_&&principal->slot>=1&&principal->slot<=200&&principal->expiresAt>now){
                            auto access=std::make_shared<Access>(Access{*principal,false});
                            it->connection->send(detail::packet(2));
                            accepted.push_back({access,std::make_unique<AuthorizedConnection>(std::move(it->connection),access)});
                        }
                    }
                }
            }
            if(remove){if(it->connection)it->connection->close();it=pending_.erase(it);}else ++it;
        }
        return accepted;
    }
    size_t pending()const{return pending_.size();}
private:
    struct Pending {std::unique_ptr<net::Conn> connection;uint64_t born;};
    std::string audience_;std::vector<Pending> pending_;
};
}
