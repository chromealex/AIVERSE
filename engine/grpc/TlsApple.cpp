// Secure Transport TLS over memory buffers (macOS / iOS). Deprecated by Apple in favour of
// Network.framework, which owns its own socket; Secure Transport is still shipped in the macOS 15 /
// iOS 18 SDKs and is the only Apple API that fits a buffer-driven engine. TLS 1.2 maximum.
// [unverified: not compiled in the Linux VM]
#if defined(__APPLE__) && !defined(__EMSCRIPTEN__)
#include "Tls.h"
#include <Security/SecureTransport.h>
#include <Security/Security.h>
#include <algorithm>
#include <cstring>
#include <string>

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"

namespace aiverse::grpc::detail {
namespace {

std::string osError(const char *what, OSStatus s) { return std::string(what) + " failed (OSStatus " + std::to_string(int(s)) + ")"; }

class SecureTransportEngine final : public TlsEngine {
  public:
    ~SecureTransportEngine() override {
        if (ctx_)
            CFRelease(ctx_);
        if (anchors_)
            CFRelease(anchors_);
    }

    bool init(const TlsConfig &cfg, const std::string &host, const std::vector<std::string> &alpn) {
        verify_ = cfg.verifyPeer;
        ctx_ = SSLCreateContext(kCFAllocatorDefault, kSSLClientSide, kSSLStreamType);
        if (!ctx_)
            return fail("SSLCreateContext failed");
        SSLSetIOFuncs(ctx_, &SecureTransportEngine::readFn, &SecureTransportEngine::writeFn);
        SSLSetConnection(ctx_, this);
        SSLSetPeerDomainName(ctx_, host.c_str(), host.size());
        SSLSetProtocolVersionMin(ctx_, kTLSProtocol12);
        if (!alpn.empty()) {
            CFMutableArrayRef arr = CFArrayCreateMutable(kCFAllocatorDefault, 0, &kCFTypeArrayCallBacks);
            for (const auto &p : alpn) {
                CFStringRef s = CFStringCreateWithCString(kCFAllocatorDefault, p.c_str(), kCFStringEncodingASCII);
                CFArrayAppendValue(arr, s);
                CFRelease(s);
            }
            SSLSetALPNProtocols(ctx_, arr);
            CFRelease(arr);
        }
        if (verify_ && !cfg.caPem.empty() && !loadAnchors(cfg.caPem))
            return false;
        if (!verify_ || anchors_)
            SSLSetSessionOption(ctx_, kSSLSessionOptionBreakOnServerAuth, true);
        return true;
    }

    void putCipher(const uint8_t *d, size_t n) override { in_.append(reinterpret_cast<const char *>(d), n); }
    void takeCipher(std::string &out) override {
        out += out_;
        out_.clear();
    }
    void writePlain(const uint8_t *d, size_t n) override { pending_.append(reinterpret_cast<const char *>(d), n); }
    void readPlain(std::string &out) override {
        out += plain_;
        plain_.clear();
    }

    State step() override {
        while (state_ == State::Handshaking) {
            const OSStatus s = SSLHandshake(ctx_);
            if (s == errSSLWouldBlock)
                return state_;
            if (s == errSSLPeerAuthCompleted) {
                if (!checkTrust())
                    return state_;
                continue;
            }
            if (s != noErr) {
                fail(s == errSSLXCertChainInvalid || s == errSSLBadCert || s == errSSLHostNameMismatch
                         ? osError("certificate verification", s)
                         : osError("handshake", s));
                return state_;
            }
            CFArrayRef protos = nullptr;
            if (SSLCopyALPNProtocols(ctx_, &protos) == noErr && protos) {
                if (CFArrayGetCount(protos) > 0) {
                    char buf[64] = {};
                    CFStringGetCString(static_cast<CFStringRef>(CFArrayGetValueAtIndex(protos, 0)), buf, sizeof buf,
                                       kCFStringEncodingASCII);
                    alpn_ = buf;
                }
                CFRelease(protos);
            }
            state_ = State::Open;
        }
        if (state_ != State::Open)
            return state_;
        while (!pending_.empty()) {
            size_t done = 0;
            const OSStatus s = SSLWrite(ctx_, pending_.data(), pending_.size(), &done);
            pending_.erase(0, done);
            if (s != noErr && s != errSSLWouldBlock) {
                fail(osError("SSLWrite", s));
                return state_;
            }
            if (done == 0)
                break;
        }
        char buf[16384];
        for (;;) {
            size_t got = 0;
            const OSStatus s = SSLRead(ctx_, buf, sizeof buf, &got);
            plain_.append(buf, got);
            if (s == errSSLClosedGraceful || s == errSSLClosedNoNotify) {
                state_ = State::Closed;
                break;
            }
            if (s == errSSLWouldBlock || (s == noErr && got == 0))
                break;
            if (s != noErr) {
                fail(osError("SSLRead", s));
                break;
            }
        }
        return state_;
    }
    std::string alpn() const override { return alpn_; }
    std::string error() const override { return error_; }

  private:
    static OSStatus readFn(SSLConnectionRef conn, void *data, size_t *len) {
        auto *self = static_cast<SecureTransportEngine *>(const_cast<void *>(conn));
        const size_t want = *len;
        const size_t n = std::min(want, self->in_.size());
        std::memcpy(data, self->in_.data(), n);
        self->in_.erase(0, n);
        *len = n;
        return n < want ? errSSLWouldBlock : noErr;
    }
    static OSStatus writeFn(SSLConnectionRef conn, const void *data, size_t *len) {
        auto *self = static_cast<SecureTransportEngine *>(const_cast<void *>(conn));
        self->out_.append(static_cast<const char *>(data), *len);
        return noErr;
    }
    bool fail(std::string e) {
        error_ = std::move(e);
        state_ = State::Failed;
        return false;
    }
    bool loadAnchors(const std::string &pem) {
        CFMutableArrayRef arr = CFArrayCreateMutable(kCFAllocatorDefault, 0, &kCFTypeArrayCallBacks);
        size_t pos = 0;
        while ((pos = pem.find("-----BEGIN CERTIFICATE-----", pos)) != std::string::npos) {
            const size_t start = pos + 27;
            const size_t end = pem.find("-----END CERTIFICATE-----", start);
            if (end == std::string::npos)
                break;
            std::string b64;
            for (size_t i = start; i < end; ++i)
                if (pem[i] != '\n' && pem[i] != '\r' && pem[i] != ' ')
                    b64.push_back(pem[i]);
            std::string der;
            if (base64Decode(b64, der)) {
                CFDataRef data = CFDataCreate(kCFAllocatorDefault, reinterpret_cast<const UInt8 *>(der.data()), CFIndex(der.size()));
                if (SecCertificateRef c = SecCertificateCreateWithData(kCFAllocatorDefault, data)) {
                    CFArrayAppendValue(arr, c);
                    CFRelease(c);
                }
                CFRelease(data);
            }
            pos = end;
        }
        if (CFArrayGetCount(arr) == 0) {
            CFRelease(arr);
            return fail("TlsConfig::caPem holds no certificate");
        }
        anchors_ = arr;
        return true;
    }
    bool checkTrust() {
        if (!verify_)
            return true;
        SecTrustRef trust = nullptr;
        if (SSLCopyPeerTrust(ctx_, &trust) != noErr || !trust)
            return fail("certificate verification failed: no server certificate");
        SecTrustSetAnchorCertificates(trust, anchors_);
        SecTrustSetAnchorCertificatesOnly(trust, false); // custom roots in addition to the system ones
        CFErrorRef err = nullptr;
        const bool ok = SecTrustEvaluateWithError(trust, &err);
        if (err)
            CFRelease(err);
        CFRelease(trust);
        return ok || fail("certificate verification failed: untrusted chain or wrong host name");
    }

    SSLContextRef ctx_ = nullptr;
    CFArrayRef anchors_ = nullptr;
    bool verify_ = true;
    std::string in_, out_, plain_, pending_, alpn_, error_;
    State state_ = State::Handshaking;
};

} // namespace

const char *tlsBackendName() { return "securetransport"; }
bool tlsAvailable() { return true; }

std::unique_ptr<TlsEngine> createTlsClient(const TlsConfig &cfg, const std::string &host,
                                           const std::vector<std::string> &alpn, std::string *error) {
    auto e = std::make_unique<SecureTransportEngine>();
    if (!e->init(cfg, host, alpn)) {
        if (error)
            *error = e->error();
        return nullptr;
    }
    return e;
}

std::unique_ptr<TlsEngine> createTlsServer(const std::string &, const std::string &, const std::vector<std::string> &,
                                           std::string *error) {
    if (error)
        *error = "test TLS server is only implemented with OpenSSL";
    return nullptr;
}

} // namespace aiverse::grpc::detail

#pragma clang diagnostic pop
#endif
