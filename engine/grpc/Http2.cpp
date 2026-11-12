#include "Http2.h"
#include <algorithm>

namespace aiverse::grpc::h2 {
namespace {

constexpr char kPreface[] = "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";
constexpr size_t kPrefaceLen = 24;
enum : uint8_t { Data = 0, Headers = 1, Priority = 2, RstStream = 3, Settings = 4, PushPromise = 5, Ping = 6,
                 Goaway = 7, WindowUpdate = 8, Continuation = 9 };
enum : uint8_t { EndStream = 0x1, Ack = 0x1, EndHeaders = 0x4, Padded = 0x8, PriorityFlag = 0x20 };
constexpr uint32_t kMaxRecvFrame = 16384;

uint32_t be32(const uint8_t *p) { return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3]; }
void put32(std::string &s, uint32_t v) {
    s.push_back(char(v >> 24));
    s.push_back(char(v >> 16));
    s.push_back(char(v >> 8));
    s.push_back(char(v));
}

} // namespace

Connection::Connection(bool client, Handler *handler)
    : client_(client), handler_(handler), prefaceSeen_(client), nextSid_(client ? 1 : 2) {}

void Connection::frame(std::string &out, uint8_t type, uint8_t flags, uint32_t sid, std::string_view p) const {
    const uint32_t n = uint32_t(p.size());
    out.push_back(char(n >> 16));
    out.push_back(char(n >> 8));
    out.push_back(char(n));
    out.push_back(char(type));
    out.push_back(char(flags));
    put32(out, sid & 0x7fffffffu);
    out.append(p.data(), p.size());
}

void Connection::windowUpdate(std::string &out, uint32_t sid, uint32_t inc) const {
    std::string p;
    put32(p, inc & 0x7fffffffu);
    frame(out, WindowUpdate, 0, sid, p);
}

void Connection::start(std::string &out) {
    if (client_)
        out.append(kPreface, kPrefaceLen);
    std::string s;
    auto setting = [&](uint16_t id, uint32_t v) {
        s.push_back(char(id >> 8));
        s.push_back(char(id));
        put32(s, v);
    };
    setting(0x2, 0);                 // ENABLE_PUSH
    setting(0x3, 100);               // MAX_CONCURRENT_STREAMS
    setting(0x4, kOurInitialWindow); // INITIAL_WINDOW_SIZE
    setting(0x6, 256 * 1024);        // MAX_HEADER_LIST_SIZE
    frame(out, Settings, 0, 0, s);
    windowUpdate(out, 0, kOurConnWindow - 65535);
}

uint32_t Connection::newStreamId() {
    const uint32_t sid = nextSid_;
    nextSid_ += 2;
    return sid;
}

Connection::Stream *Connection::find(uint32_t sid) {
    auto it = streams_.find(sid);
    return it == streams_.end() ? nullptr : &it->second;
}

void Connection::closeHalf(uint32_t sid, bool local) {
    Stream *s = find(sid);
    if (!s)
        return;
    (local ? s->localClosed : s->remoteClosed) = true;
    if (s->localClosed && s->remoteClosed) {
        streams_.erase(sid);
        --openStreams_;
    }
}

void Connection::sendHeaders(uint32_t sid, const hpack::HeaderList &headers, bool endStream, std::string &out) {
    if (!find(sid)) {
        Stream st;
        st.sendWindow = peerInitialWindow_;
        streams_.emplace(sid, st);
        ++openStreams_;
    }
    std::string block;
    encoder_.encode(headers, block);
    size_t off = 0;
    bool first = true;
    do {
        const size_t n = std::min<size_t>(peerMaxFrame_, block.size() - off);
        const bool last = off + n == block.size();
        const uint8_t flags = uint8_t((last ? EndHeaders : 0) | (first && endStream ? EndStream : 0));
        frame(out, first ? Headers : Continuation, flags, sid, std::string_view(block).substr(off, n));
        off += n;
        first = false;
    } while (off < block.size());
    if (endStream)
        closeHalf(sid, true);
}

void Connection::sendData(uint32_t sid, std::string_view data, bool endStream, std::string &out) {
    Stream *s = find(sid);
    if (!s || s->localClosed || s->endQueued)
        return;
    s->pending.append(data.data(), data.size());
    s->endQueued = endStream;
    flush(out);
}

bool Connection::streamPending(uint32_t sid) const {
    auto it = streams_.find(sid);
    return it != streams_.end() && (!it->second.pending.empty() || it->second.endQueued);
}

void Connection::flush(std::string &out) {
    std::vector<uint32_t> finished;
    for (auto &[sid, s] : streams_) {
        if (s.localClosed)
            continue;
        while (!s.pending.empty() && s.sendWindow > 0 && connSendWindow_ > 0) {
            const size_t n = size_t(std::min<int64_t>({int64_t(s.pending.size()), s.sendWindow, connSendWindow_,
                                                       int64_t(peerMaxFrame_)}));
            const bool end = s.endQueued && n == s.pending.size();
            frame(out, Data, end ? EndStream : 0, sid, std::string_view(s.pending).substr(0, n));
            s.pending.erase(0, n);
            s.sendWindow -= int64_t(n);
            connSendWindow_ -= int64_t(n);
            if (end) {
                s.endQueued = false;
                finished.push_back(sid);
            }
        }
        if (s.pending.empty() && s.endQueued) { // empty DATA with END_STREAM
            frame(out, Data, EndStream, sid, {});
            s.endQueued = false;
            finished.push_back(sid);
        }
    }
    for (uint32_t sid : finished)
        closeHalf(sid, true);
}

void Connection::reset(uint32_t sid, uint32_t code, std::string &out) {
    std::string p;
    put32(p, code);
    frame(out, RstStream, 0, sid, p);
    if (streams_.erase(sid))
        --openStreams_;
}

void Connection::goaway(uint32_t code, const std::string &debug, std::string &out) {
    std::string p;
    put32(p, lastPeerSid_);
    put32(p, code);
    p += debug;
    frame(out, Goaway, 0, 0, p);
}

void Connection::ping(std::string &out) { frame(out, Ping, 0, 0, std::string(8, '\0')); }

bool Connection::fail(uint32_t code, std::string why, std::string &out) {
    error_ = std::move(why);
    goaway(code, error_, out);
    return false;
}

bool Connection::receive(std::string &inbox, std::string &out) {
    size_t pos = 0;
    if (!prefaceSeen_) {
        if (inbox.size() < kPrefaceLen)
            return inbox.compare(0, inbox.size(), kPreface, inbox.size()) == 0 ||
                   fail(ProtocolError, "bad connection preface", out);
        if (inbox.compare(0, kPrefaceLen, kPreface, kPrefaceLen) != 0)
            return fail(ProtocolError, "bad connection preface", out);
        prefaceSeen_ = true;
        pos = kPrefaceLen;
    }
    bool ok = true;
    while (ok && inbox.size() - pos >= 9) {
        const auto *h = reinterpret_cast<const uint8_t *>(inbox.data() + pos);
        const uint32_t len = (uint32_t(h[0]) << 16) | (uint32_t(h[1]) << 8) | h[2];
        if (len > kMaxRecvFrame) {
            ok = fail(FrameSizeError, "frame larger than SETTINGS_MAX_FRAME_SIZE", out);
            break;
        }
        if (inbox.size() - pos - 9 < len)
            break;
        const uint8_t type = h[3], flags = h[4];
        const uint32_t sid = be32(h + 5) & 0x7fffffffu;
        ok = onFrame(type, flags, sid, std::string_view(inbox).substr(pos + 9, len), out);
        pos += 9 + len;
    }
    inbox.erase(0, pos);
    if (ok)
        flush(out);
    return ok;
}

bool Connection::onSettings(uint8_t flags, std::string_view p, std::string &out) {
    if (flags & Ack)
        return p.empty() || fail(FrameSizeError, "SETTINGS ack with payload", out);
    if (p.size() % 6)
        return fail(FrameSizeError, "bad SETTINGS length", out);
    for (size_t i = 0; i < p.size(); i += 6) {
        const auto *b = reinterpret_cast<const uint8_t *>(p.data() + i);
        const uint16_t id = uint16_t((b[0] << 8) | b[1]);
        const uint32_t v = be32(b + 2);
        if (id == 0x3) {
            peerMaxStreams_ = v;
        } else if (id == 0x4) {
            if (v > 0x7fffffffu)
                return fail(FlowControlError, "INITIAL_WINDOW_SIZE too large", out);
            const int64_t delta = int64_t(v) - peerInitialWindow_;
            peerInitialWindow_ = v;
            for (auto &kv : streams_)
                kv.second.sendWindow += delta;
        } else if (id == 0x5) {
            if (v < 16384 || v > 16777215)
                return fail(ProtocolError, "bad MAX_FRAME_SIZE", out);
            peerMaxFrame_ = v;
        } else if (id == 0x2 && v > 1) {
            return fail(ProtocolError, "bad ENABLE_PUSH", out);
        }
        // HEADER_TABLE_SIZE: our encoder never uses the dynamic table, nothing to do.
    }
    frame(out, Settings, Ack, 0, {});
    return true;
}

bool Connection::onFrame(uint8_t type, uint8_t flags, uint32_t sid, std::string_view p, std::string &out) {
    if (headerSid_ && (type != Continuation || sid != headerSid_))
        return fail(ProtocolError, "expected CONTINUATION", out);
    switch (type) {
    case Data: {
        if (!sid)
            return fail(ProtocolError, "DATA on stream 0", out);
        size_t pad = 0;
        if (flags & Padded) {
            if (p.empty() || uint8_t(p[0]) >= p.size())
                return fail(ProtocolError, "bad padding", out);
            pad = uint8_t(p[0]);
            p = p.substr(1, p.size() - 1 - pad);
        }
        // Flow control counts the whole frame payload; replenish generously.
        const uint32_t counted = uint32_t(p.size() + pad + ((flags & Padded) ? 1 : 0));
        connRecvConsumed_ += counted;
        if (connRecvConsumed_ >= kOurConnWindow / 4) {
            windowUpdate(out, 0, connRecvConsumed_);
            connRecvConsumed_ = 0;
        }
        Stream *s = find(sid);
        if (!s || s->remoteClosed) {
            if (sid > (client_ ? nextSid_ : lastPeerSid_))
                return fail(ProtocolError, "DATA on idle stream", out);
            return true; // late frames after a reset: ignore
        }
        s->recvConsumed += counted;
        if (s->recvConsumed >= kOurInitialWindow / 4 && !(flags & EndStream)) {
            windowUpdate(out, sid, s->recvConsumed);
            s->recvConsumed = 0;
        }
        const bool end = flags & EndStream;
        if (end)
            closeHalf(sid, false);
        handler_->onData(sid, p, end);
        return true;
    }
    case Headers: {
        if (!sid)
            return fail(ProtocolError, "HEADERS on stream 0", out);
        size_t pad = 0;
        if (flags & Padded) {
            if (p.empty())
                return fail(ProtocolError, "bad padding", out);
            pad = uint8_t(p[0]);
            p.remove_prefix(1);
        }
        if (flags & PriorityFlag) {
            if (p.size() < 5)
                return fail(FrameSizeError, "short HEADERS", out);
            p.remove_prefix(5);
        }
        if (pad > p.size())
            return fail(ProtocolError, "bad padding", out);
        p.remove_suffix(pad);
        if (!client_ && !find(sid)) {
            if (sid % 2 == 0 || sid <= lastPeerSid_)
                return fail(ProtocolError, "bad client stream id", out);
            lastPeerSid_ = sid;
            Stream st;
            st.sendWindow = peerInitialWindow_;
            streams_.emplace(sid, st);
            ++openStreams_;
        }
        headerBlock_.assign(p.data(), p.size());
        headerEndStream_ = flags & EndStream;
        if (!(flags & EndHeaders)) {
            headerSid_ = sid;
            return true;
        }
        [[fallthrough]];
    }
    case Continuation: {
        if (type == Continuation) {
            if (!headerSid_)
                return fail(ProtocolError, "unexpected CONTINUATION", out);
            headerBlock_.append(p.data(), p.size());
            if (headerBlock_.size() > 512 * 1024)
                return fail(EnhanceYourCalm, "header block too large", out);
            if (!(flags & EndHeaders))
                return true;
            headerSid_ = 0;
        }
        hpack::HeaderList list;
        if (!decoder_.decode(headerBlock_, list))
            return fail(CompressionError, "HPACK decoding failed", out);
        headerBlock_.clear();
        const bool known = find(sid) != nullptr;
        if (headerEndStream_)
            closeHalf(sid, false);
        if (known)
            handler_->onHeaders(sid, std::move(list), headerEndStream_);
        return true;
    }
    case Priority:
        return p.size() == 5 || fail(FrameSizeError, "bad PRIORITY", out);
    case RstStream: {
        if (p.size() != 4 || !sid)
            return fail(FrameSizeError, "bad RST_STREAM", out);
        if (streams_.erase(sid)) {
            --openStreams_;
            handler_->onReset(sid, be32(reinterpret_cast<const uint8_t *>(p.data())));
        }
        return true;
    }
    case Settings:
        return sid == 0 ? onSettings(flags, p, out) : fail(ProtocolError, "SETTINGS on a stream", out);
    case PushPromise:
        return fail(ProtocolError, "PUSH_PROMISE although push is disabled", out);
    case Ping:
        if (p.size() != 8 || sid)
            return fail(FrameSizeError, "bad PING", out);
        if (!(flags & Ack))
            frame(out, Ping, Ack, 0, p);
        return true;
    case Goaway: {
        if (p.size() < 8)
            return fail(FrameSizeError, "bad GOAWAY", out);
        const auto *b = reinterpret_cast<const uint8_t *>(p.data());
        goawayReceived_ = true;
        handler_->onGoaway(be32(b) & 0x7fffffffu, be32(b + 4), std::string(p.substr(8)));
        return true;
    }
    case WindowUpdate: {
        if (p.size() != 4)
            return fail(FrameSizeError, "bad WINDOW_UPDATE", out);
        const uint32_t inc = be32(reinterpret_cast<const uint8_t *>(p.data())) & 0x7fffffffu;
        if (!inc)
            return fail(ProtocolError, "zero WINDOW_UPDATE", out);
        if (!sid) {
            connSendWindow_ += inc;
            if (connSendWindow_ > 0x7fffffff)
                return fail(FlowControlError, "connection window overflow", out);
        } else if (Stream *s = find(sid)) {
            s->sendWindow += inc;
            if (s->sendWindow > 0x7fffffff) {
                reset(sid, FlowControlError, out);
                handler_->onReset(sid, FlowControlError);
            }
        }
        return true;
    }
    default:
        return true; // unknown frame types are ignored (spec)
    }
}

} // namespace aiverse::grpc::h2
