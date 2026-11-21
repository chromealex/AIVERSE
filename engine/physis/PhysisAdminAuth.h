// PHYSIS Stage 26: challenge/response authentication on a DEDICATED net::Conn.
// Uses net::Crypto keyed BLAKE2b and OS random. No plaintext key transmission.
// Use ONLY loopback WS behind TLS proxy or an authenticated encrypted transport.
// These authenticated identities are separate from ordinary player InputFrame.
#pragma once
#include "PhysisRemoteServer.h"
#include "engine/net/Crypto.h"
#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace aiverse::physis::remote::admin {

inline constexpr std::uint32_t kAuthMagic=0x41485950u; // "PHYA"
inline constexpr std::uint16_t kAuthVersion=1;
inline constexpr std::size_t kAuthMaxBytes=320;
enum class Kind:std::uint8_t { Hello=1,Challenge=2,Proof=3,Granted=4,Denied=5 };

inline net::Bytes encode(Kind kind,const net::Bytes& payload={}) {
    ByteWriter writer;
    writer.u32(kAuthMagic);writer.u16(kAuthVersion);writer.u8(std::uint8_t(kind));
    writer.u16(std::uint16_t(payload.size()));
    writer.buf.insert(writer.buf.end(),payload.begin(),payload.end());
    return writer.buf;
}
inline bool decode(const net::Bytes& raw,Kind& kind,net::Bytes& payload) {
    if(raw.size()<9 || raw.size()>kAuthMaxBytes)return false;
    ByteReader r(raw.data(),raw.size());
    if(r.u32()!=kAuthMagic || r.u16()!=kAuthVersion)return false;
    const auto n=r.u8();
    const auto length=r.u16();
    if(!r.ok() || length>kAuthMaxBytes-9 ||
       (n<1 || n>5))return false;
    net::Bytes temp;
    for(std::uint16_t i=0;i<length&&r.ok();++i)temp.push_back(r.u8());
    if(!r.ok()||!r.atEnd())return false;
    kind=Kind(n);
    payload=std::move(temp);
    return true;
}
inline net::Bytes authenticator(const net::Key& secret,
                                const std::string& principal,
                                const net::Bytes& nonce) {
    net::Bytes input;
    constexpr const char label[]="aiverse-physis-admin-v1";
    input.insert(input.end(),label,label+sizeof(label)-1);
    input.push_back(0);
    input.insert(input.end(),principal.begin(),principal.end());
    input.push_back(0);
    input.insert(input.end(),nonce.begin(),nonce.end());
    return net::keyedHash(secret,input.data(),input.size(),32);
}
inline bool validPrincipal(const std::string& principal) {
    if(principal.empty()||principal.size()>64)return false;
    for(unsigned char c:principal)
        if(!(c>='a'&&c<='z')&&!(c>='A'&&c<='Z')&&
           !(c>='0'&&c<='9')&&c!='-'&&c!='_'&&c!='.')
            return false;
    return true;
}

class ServerHandshake {
public:
    using KeyLookup=std::function<std::optional<net::Key>(const std::string&)>;
    explicit ServerHandshake(KeyLookup lookup):lookup_(std::move(lookup)) {}
    ServerHandshake(const ServerHandshake&)=delete;
    ServerHandshake& operator=(const ServerHandshake&)=delete;
    ~ServerHandshake(){net::wipe(secret_.data(),secret_.size());}
    bool authenticated()const{return state_==State::Granted;}
    bool rejected()const{return state_==State::Denied;}
    const std::string& principal()const{return principal_;}

    bool consume(net::Conn& conn,const net::Bytes& frame) {
        Kind kind{};
        net::Bytes data;
        if(!decode(frame,kind,data))return deny(conn);
        if(state_==State::AwaitHello) {
            if(kind!=Kind::Hello)return deny(conn);
            const std::string candidate(data.begin(),data.end());
            if(!validPrincipal(candidate) || !lookup_)return deny(conn);
            auto key=lookup_(candidate);
            principal_=candidate;
            // Unknown principals receive the same random Challenge and are
            // denied only at Proof, so Hello cannot enumerate valid accounts.
            // A throwaway random key keeps the Proof work identical.
            knownPrincipal_=key && !net::isZero(*key);
            if(knownPrincipal_)secret_=*key;
            else net::secureRandom(secret_.data(),secret_.size());
            if(key)net::wipe(key->data(),key->size());
            nonce_.resize(32);
            net::secureRandom(nonce_.data(),nonce_.size());
            conn.send(encode(Kind::Challenge,nonce_));
            state_=State::AwaitProof;
            return true;
        }
        if(state_==State::AwaitProof) {
            if(kind!=Kind::Proof || data.size()!=32)return deny(conn);
            const auto expected=authenticator(secret_,principal_,nonce_);
            const bool matched=net::equalBytes(expected.data(),data.data(),32) &&
                               knownPrincipal_;
            net::wipe(secret_.data(),secret_.size());
            nonce_.clear();
            if(!matched)return deny(conn);
            state_=State::Granted;
            conn.send(encode(Kind::Granted));
            return true;
        }
        return deny(conn);
    }
private:
    enum class State {AwaitHello,AwaitProof,Granted,Denied};
    bool deny(net::Conn& conn) {
        state_=State::Denied;
        net::wipe(secret_.data(),secret_.size());
        nonce_.clear();
        conn.send(encode(Kind::Denied));
        return false;
    }
    State state_=State::AwaitHello;
    KeyLookup lookup_;
    std::string principal_;
    net::Key secret_{};
    net::Bytes nonce_;
    bool knownPrincipal_=false;
};

class ClientHandshake {
public:
    ClientHandshake(std::string principal,net::Key secret)
        :principal_(std::move(principal)),secret_(secret) {}
    ClientHandshake(const ClientHandshake&)=delete;
    ClientHandshake& operator=(const ClientHandshake&)=delete;
    ~ClientHandshake(){net::wipe(secret_.data(),secret_.size());}
    bool authenticated() const{return state_==State::Granted;}
    bool rejected()const{return state_==State::Denied;}
    bool begin(net::Conn& conn) {
        if(state_!=State::Fresh || !validPrincipal(principal_) ||
           net::isZero(secret_))return false;
        net::Bytes payload(principal_.begin(),principal_.end());
        conn.send(encode(Kind::Hello,payload));
        state_=State::AwaitChallenge;
        return true;
    }
    bool consume(net::Conn& conn,const net::Bytes& frame) {
        Kind kind{};
        net::Bytes payload;
        if(!decode(frame,kind,payload))return deny();
        if(kind==Kind::Denied)return deny();
        if(state_==State::AwaitChallenge && kind==Kind::Challenge &&
           payload.size()==32) {
            conn.send(encode(Kind::Proof,authenticator(secret_,principal_,payload)));
            net::wipe(secret_.data(),secret_.size());
            state_=State::AwaitGrant;
            return true;
        }
        if(state_==State::AwaitGrant && kind==Kind::Granted &&
           payload.empty()) {
            state_=State::Granted;
            return true;
        }
        return deny();
    }
private:
    enum class State {Fresh,AwaitChallenge,AwaitGrant,Granted,Denied};
    bool deny() {
        state_=State::Denied;
        net::wipe(secret_.data(),secret_.size());
        return false;
    }
    State state_=State::Fresh;
    std::string principal_;
    net::Key secret_;
};

} // namespace aiverse::physis::remote::admin
