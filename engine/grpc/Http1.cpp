#include "Http1.h"
#include "Transport.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <random>
#ifndef __EMSCRIPTEN__
#include "engine/net/WebSocket.h"
#endif

namespace aiverse::grpc::http1 {

Parser::Result Parser::feed(std::string &inbox, bool headOnly) {
    if (stage_ == Stage::Head) {
        const size_t end = inbox.find("\r\n\r\n");
        if (end == std::string::npos)
            return inbox.size() > 64 * 1024 ? fail("HTTP head too large") : Result::NeedMore;
        std::string_view head(inbox.data(), end);
        const size_t eol = head.find("\r\n");
        std::string_view first = head.substr(0, eol);
        if (response_) {
            if (first.substr(0, 5) != "HTTP/" || first.size() < 12)
                return fail("bad HTTP status line");
            msg_.status = std::atoi(std::string(first.substr(9, 3)).c_str());
            msg_.reason = std::string(first.size() > 13 ? first.substr(13) : std::string_view());
            keepAlive_ = first.substr(5, 3) == "1.1";
        } else {
            const size_t a = first.find(' ');
            const size_t b = first.rfind(' ');
            if (a == std::string_view::npos || b == a)
                return fail("bad HTTP request line");
            msg_.method = std::string(first.substr(0, a));
            msg_.target = std::string(first.substr(a + 1, b - a - 1));
        }
        msg_.headers = detail::parseHeaderBlock(eol == std::string_view::npos ? std::string_view() : head.substr(eol + 2));
        inbox.erase(0, end + 4);
        if (const std::string *c = msg_.header("connection")) {
            std::string v = *c;
            for (char &ch : v)
                ch = char(std::tolower(uint8_t(ch)));
            if (v.find("close") != std::string::npos)
                keepAlive_ = false;
            if (v.find("keep-alive") != std::string::npos)
                keepAlive_ = true;
        }
        if (headOnly || msg_.status == 101) {
            stage_ = Stage::Done;
            return Result::Head;
        }
        const std::string *te = msg_.header("transfer-encoding");
        const std::string *cl = msg_.header("content-length");
        if (te && te->find("chunked") != std::string::npos) {
            stage_ = Stage::Chunked;
        } else if (cl) {
            remaining_ = std::atoll(cl->c_str());
            stage_ = Stage::Body;
        } else if (!response_ || msg_.status == 204 || msg_.status == 304 || msg_.status / 100 == 1) {
            remaining_ = 0;
            stage_ = Stage::Body;
        } else {
            remaining_ = -1;
            keepAlive_ = false;
            stage_ = Stage::Body;
        }
    }
    if (stage_ == Stage::Body) {
        if (remaining_ < 0) {
            msg_.body += inbox;
            inbox.clear();
            return Result::NeedMore;
        }
        const size_t take = size_t(std::min<int64_t>(remaining_, int64_t(inbox.size())));
        msg_.body.append(inbox, 0, take);
        inbox.erase(0, take);
        remaining_ -= int64_t(take);
        if (remaining_ == 0) {
            stage_ = Stage::Done;
            return Result::Complete;
        }
        return Result::NeedMore;
    }
    while (stage_ == Stage::Chunked) {
        if (remaining_ > 0) {
            const size_t take = size_t(std::min<int64_t>(remaining_, int64_t(inbox.size())));
            msg_.body.append(inbox, 0, take);
            inbox.erase(0, take);
            remaining_ -= int64_t(take);
            if (remaining_ > 0)
                return Result::NeedMore;
            remaining_ = -2; // expect CRLF after chunk data
        }
        if (remaining_ == -2) {
            if (inbox.size() < 2)
                return Result::NeedMore;
            if (inbox.compare(0, 2, "\r\n") != 0)
                return fail("bad chunk terminator");
            inbox.erase(0, 2);
            remaining_ = 0;
        }
        const size_t eol = inbox.find("\r\n");
        if (eol == std::string::npos)
            return Result::NeedMore;
        const long long n = std::strtoll(inbox.substr(0, eol).c_str(), nullptr, 16);
        if (n < 0)
            return fail("bad chunk size");
        if (n == 0) {
            // "0\r\n" then optional trailer lines, then an empty line (trailers are ignored).
            const size_t end = inbox.find("\r\n\r\n", eol);
            if (inbox.size() >= eol + 4 && inbox.compare(eol, 4, "\r\n\r\n") == 0)
                inbox.erase(0, eol + 4);
            else if (end != std::string::npos)
                inbox.erase(0, end + 4);
            else
                return Result::NeedMore;
            stage_ = Stage::Done;
            return Result::Complete;
        }
        inbox.erase(0, eol + 2);
        remaining_ = n;
    }
    return stage_ == Stage::Done ? Result::Complete : Result::NeedMore;
}

Parser::Result Parser::finishOnClose() {
    if (stage_ == Stage::Body && remaining_ < 0) {
        stage_ = Stage::Done;
        return Result::Complete;
    }
    return stage_ == Stage::Done ? Result::Complete : fail("connection closed mid-response");
}

} // namespace aiverse::grpc::http1

namespace aiverse::grpc::ws {

namespace {
std::mt19937 &rng() {
    static std::mt19937 r{std::random_device{}()};
    return r;
}
} // namespace

void writeFrame(std::string &out, uint8_t opcode, std::string_view payload, bool masked) {
    out.push_back(char(0x80 | opcode));
    const uint8_t m = masked ? 0x80 : 0;
    const size_t n = payload.size();
    if (n < 126) {
        out.push_back(char(m | uint8_t(n)));
    } else if (n < 65536) {
        out.push_back(char(m | 126));
        out.push_back(char(n >> 8));
        out.push_back(char(n));
    } else {
        out.push_back(char(m | 127));
        for (int i = 7; i >= 0; --i)
            out.push_back(char(uint64_t(n) >> (8 * i)));
    }
    if (!masked) {
        out.append(payload.data(), n);
        return;
    }
    const uint32_t key = rng()();
    const uint8_t k[4] = {uint8_t(key >> 24), uint8_t(key >> 16), uint8_t(key >> 8), uint8_t(key)};
    out.append(reinterpret_cast<const char *>(k), 4);
    for (size_t i = 0; i < n; ++i)
        out.push_back(char(uint8_t(payload[i]) ^ k[i & 3]));
}

int readFrame(std::string &inbox, Frame &f, bool expectMasked, size_t maxPayload) {
    if (inbox.size() < 2)
        return 0;
    const auto *p = reinterpret_cast<const uint8_t *>(inbox.data());
    if (p[0] & 0x70)
        return -1; // no extensions negotiated
    const bool masked = p[1] & 0x80;
    if (masked != expectMasked)
        return -1;
    uint64_t n = p[1] & 0x7f;
    size_t pos = 2;
    if (n == 126) {
        if (inbox.size() < 4)
            return 0;
        n = (uint64_t(p[2]) << 8) | p[3];
        pos = 4;
    } else if (n == 127) {
        if (inbox.size() < 10)
            return 0;
        n = 0;
        for (int i = 0; i < 8; ++i)
            n = (n << 8) | p[2 + i];
        pos = 10;
    }
    if (n > maxPayload)
        return -1;
    const size_t need = pos + (masked ? 4 : 0) + size_t(n);
    if (inbox.size() < need)
        return 0;
    f.fin = p[0] & 0x80;
    f.opcode = p[0] & 0x0f;
    if ((f.opcode & 0x08) && (n > 125 || !f.fin))
        return -1;
    f.payload.assign(inbox, pos + (masked ? 4 : 0), size_t(n));
    if (masked) {
        const uint8_t *k = p + pos;
        for (size_t i = 0; i < f.payload.size(); ++i)
            f.payload[i] = char(uint8_t(f.payload[i]) ^ k[i & 3]);
    }
    inbox.erase(0, need);
    return 1;
}

std::string randomKey() {
    std::string raw(16, '\0');
    for (char &c : raw)
        c = char(rng()() & 0xff);
    return base64Encode(raw);
}

std::string acceptFor(const std::string &key) {
#ifndef __EMSCRIPTEN__
    return net::websocketAccept(key);
#else
    (void)key;
    return {};
#endif
}

} // namespace aiverse::grpc::ws
