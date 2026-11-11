#pragma once
#include <cstdint>
#include <limits>
#include <string>
namespace aiverse::lib::app {
enum class SessionPhase { Idle, Connecting, Lobby, Playing, Reconnecting, Closed };
struct SessionConfig { std::uint32_t maxRetries=3; std::uint64_t retryDelay=1000; };
struct Session {
    SessionPhase phase=SessionPhase::Idle, resumePhase=SessionPhase::Lobby;
    SessionConfig config;
    std::uint32_t retries=0;
    std::uint64_t nextRetry=0, epoch=0, lastSequence=0;
    bool hasSequence=false;
    std::string contentHash;
    bool connect(std::string hash) {
        if(hash.empty()||(phase!=SessionPhase::Idle&&phase!=SessionPhase::Closed)) return false;
        contentHash=std::move(hash);phase=SessionPhase::Connecting;retries=0;++epoch;hasSequence=false;return true;
    }
    bool connected(std::uint64_t ticket, const std::string& hash) {
        if(ticket!=epoch||(phase!=SessionPhase::Connecting&&phase!=SessionPhase::Reconnecting)) return false;
        if(hash!=contentHash){phase=SessionPhase::Closed;return false;}
        phase=phase==SessionPhase::Reconnecting?resumePhase:SessionPhase::Lobby;retries=0;return true;
    }
    bool start() { if(phase!=SessionPhase::Lobby)return false;phase=SessionPhase::Playing;return true; }
    void disconnected(std::uint64_t now) {
        if(phase==SessionPhase::Idle||phase==SessionPhase::Closed)return;
        if(phase!=SessionPhase::Reconnecting) resumePhase=phase==SessionPhase::Playing?SessionPhase::Playing:SessionPhase::Lobby;
        phase=SessionPhase::Reconnecting;++epoch;
        nextRetry=config.retryDelay>std::numeric_limits<std::uint64_t>::max()-now?std::numeric_limits<std::uint64_t>::max():now+config.retryDelay;
    }
    bool retry(std::uint64_t now) {
        if(phase!=SessionPhase::Reconnecting||now<nextRetry)return false;
        if(retries>=config.maxRetries){phase=SessionPhase::Closed;return false;}
        ++retries;++epoch;
        nextRetry=config.retryDelay>std::numeric_limits<std::uint64_t>::max()-now?std::numeric_limits<std::uint64_t>::max():now+config.retryDelay;
        return true;
    }
    bool accept(std::uint64_t ticket,std::uint64_t sequence) {
        if(ticket!=epoch||phase!=SessionPhase::Playing||(hasSequence&&sequence<=lastSequence))return false;
        lastSequence=sequence;hasSequence=true;return true;
    }
    void close(){phase=SessionPhase::Closed;++epoch;}
};
}
