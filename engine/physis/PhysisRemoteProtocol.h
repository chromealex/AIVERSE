// PHYSIS Stage 25: versioned remote editor envelopes over net::Conn.
// Independent of FPS/RTS gameplay input, editor GUI and platform transport.
#pragma once
#include "PhysisFieldRuntime.h"
#include "engine/net/Conn.h"
#include <cstdint>
#include <string>
#include <utility>

namespace aiverse::physis::remote {
inline constexpr std::uint32_t kMagic=0x53594850u; // PHYS, little endian
inline constexpr std::uint16_t kVersion=1;
inline constexpr std::size_t kMaxFrame=40u*1024u;
inline constexpr std::size_t kMaxText=240;
// Stable V1 receipt diagnostics: when the durable journal is unavailable,
// Unknown must NOT trigger automatic Submit retries. Keeping this as V1
// receipt detail preserves existing binary layouts and journal schemas.
inline constexpr const char* kJournalDurabilityUnavailable =
    "PHYSIS journal unavailable; receipt durability unconfirmed";
inline constexpr const char* kJournalRetrySuspended =
    "PHYSIS journal unavailable; new edits and retries suspended";
inline bool journalRetrySuspended(const std::string& detail) {
    return detail==kJournalDurabilityUnavailable ||
           detail==kJournalRetrySuspended;
}

enum class Message : std::uint8_t { Submit=1, Query=2, Receipt=3 };
enum class Result : std::uint8_t {
    Unknown=0, Queued=1, Applied=2, Rejected=3, Conflict=4, Denied=5
};
struct Receipt {
    Result result=Result::Unknown;
    std::uint32_t scheduledTick=0;
    std::uint64_t revision=0;
    std::string detail;
};
struct Packet {
    Message kind=Message::Query;
    // Client instance ID comes from secure host randomness, persisted over
    // reconnect; never a permission/identity claim.
    std::uint64_t clientInstance=0, requestId=0;
    Command command{};
    Receipt receipt{};
};
inline bool terminal(Result r) {
    return r==Result::Applied || r==Result::Rejected ||
           r==Result::Conflict || r==Result::Denied;
}
inline bool validResult(Result r) {
    switch(r) {
        case Result::Unknown: case Result::Queued: case Result::Applied:
        case Result::Rejected: case Result::Conflict: case Result::Denied: return true;
    }
    return false;
}
inline bool protocolError(std::string* e,const char* what) {
    if(e)*e=what;
    return false;
}
inline bool encode(const Packet& p,net::Bytes& result,std::string* error=nullptr) {
    if(!p.clientInstance || !p.requestId)
        return protocolError(error,"PHYSIS invalid session/request ID");
    ByteWriter w;
    w.u32(kMagic);w.u16(kVersion);w.u8(std::uint8_t(p.kind));
    w.u64(p.clientInstance);w.u64(p.requestId);
    if(p.kind==Message::Submit) {
        if(p.command.args.empty() || p.command.args.size()>4096)
            return protocolError(error,"PHYSIS command argument limit");
        ByteWriter command;
        writeCommand(command,p.command);
        if(command.buf.size()>kMaxFrame-32)
            return protocolError(error,"PHYSIS oversize nested command");
        w.u32(std::uint32_t(command.buf.size()));
        w.buf.insert(w.buf.end(),command.buf.begin(),command.buf.end());
    } else if(p.kind==Message::Receipt) {
        if(!validResult(p.receipt.result)||p.receipt.detail.size()>kMaxText)
            return protocolError(error,"PHYSIS invalid receipt");
        w.u8(std::uint8_t(p.receipt.result));
        w.u32(p.receipt.scheduledTick);w.u64(p.receipt.revision);
        w.str(p.receipt.detail);
    } else if(p.kind!=Message::Query)
        return protocolError(error,"PHYSIS unknown message kind");
    if(w.buf.size()>kMaxFrame)
        return protocolError(error,"PHYSIS packet exceeds frame bound");
    result=std::move(w.buf);
    if(error)error->clear();
    return true;
}
inline bool decode(const net::Bytes& bytes,Packet& output,std::string* error=nullptr) {
    if(bytes.size()<23 || bytes.size()>kMaxFrame)
        return protocolError(error,"PHYSIS packet length invalid");
    ByteReader r(bytes.data(),bytes.size());
    const std::uint32_t magic=r.u32();
    const std::uint16_t version=r.u16();
    const std::uint8_t kind=r.u8();
    Packet candidate;
    candidate.clientInstance=r.u64();candidate.requestId=r.u64();
    if(!r.ok()||magic!=kMagic||version!=kVersion||
       !candidate.clientInstance||!candidate.requestId)
        return protocolError(error,"PHYSIS packet header invalid");
    if(kind==1) {
        candidate.kind=Message::Submit;
        const auto length=r.u32();
        if(!r.ok()||!length||length>kMaxFrame-32)
            return protocolError(error,"PHYSIS command length invalid");
        net::Bytes raw;raw.reserve(length);
        for(std::uint32_t i=0;i<length&&r.ok();++i)raw.push_back(r.u8());
        if(!r.ok())return protocolError(error,"PHYSIS command truncated");
        ByteReader nested(raw.data(),raw.size());
        candidate.command=readCommand(nested);
        if(!nested.ok()||!nested.atEnd()||candidate.command.args.empty()||
           candidate.command.args.size()>4096)
            return protocolError(error,"PHYSIS malformed nested command");
    } else if(kind==2) candidate.kind=Message::Query;
    else if(kind==3) {
        candidate.kind=Message::Receipt;
        candidate.receipt.result=Result(r.u8());
        candidate.receipt.scheduledTick=r.u32();
        candidate.receipt.revision=r.u64();
        const auto len=r.u32();
        if(!r.ok()||len>kMaxText)
            return protocolError(error,"PHYSIS receipt length invalid");
        candidate.receipt.detail.reserve(len);
        for(std::uint32_t i=0;i<len&&r.ok();++i)
            candidate.receipt.detail.push_back(char(r.u8()));
        if(!validResult(candidate.receipt.result))
            return protocolError(error,"PHYSIS invalid receipt status");
    } else return protocolError(error,"PHYSIS packet type unknown");
    if(!r.ok()||!r.atEnd())
        return protocolError(error,"PHYSIS trailing/truncated packet");
    output=std::move(candidate);
    if(error)error->clear();
    return true;
}
} // namespace aiverse::physis::remote
