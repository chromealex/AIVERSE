// SChannel TLS over memory buffers (Windows). Compiled with mingw-w64; not run on Windows here.
#if defined(_WIN32) && !defined(__EMSCRIPTEN__)
#ifndef SECURITY_WIN32
#define SECURITY_WIN32
#endif
#include "Tls.h"
#include <winsock2.h>
#include <windows.h>
#include <wincrypt.h>
#include <schannel.h>
#include <security.h>
#include <sspi.h>
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace aiverse::grpc::detail {
namespace {

std::string winError(SECURITY_STATUS s) {
    char buf[256] = {};
    FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, DWORD(s), 0, buf, sizeof buf, nullptr);
    std::string m(buf);
    while (!m.empty() && (m.back() == '\n' || m.back() == '\r' || m.back() == ' '))
        m.pop_back();
    char code[32];
    std::snprintf(code, sizeof code, "0x%08lx", static_cast<unsigned long>(s));
    return m.empty() ? std::string(code) : m + " (" + code + ")";
}

class SchannelEngine final : public TlsEngine {
  public:
    ~SchannelEngine() override {
        if (haveCtx_)
            DeleteSecurityContext(&ctx_);
        if (haveCred_)
            FreeCredentialsHandle(&cred_);
        if (roots_)
            CertCloseStore(roots_, 0);
    }

    bool init(const TlsConfig &cfg, const std::string &host, const std::vector<std::string> &alpn) {
        host_ = host;
        verify_ = cfg.verifyPeer;
        for (const auto &p : alpn) {
            alpnWire_.push_back(char(p.size()));
            alpnWire_ += p;
        }
        if (verify_ && !cfg.caPem.empty() && !loadRoots(cfg.caPem))
            return false;
        // Custom roots or no verification: SChannel must not validate; we check the chain ourselves.
        manual_ = !verify_ || roots_ != nullptr;
        const DWORD flags = SCH_CRED_NO_DEFAULT_CREDS | SCH_USE_STRONG_CRYPTO |
                            (manual_ ? SCH_CRED_MANUAL_CRED_VALIDATION : SCH_CRED_AUTO_CRED_VALIDATION);
        SCH_CREDENTIALS sc{};
        sc.dwVersion = SCH_CREDENTIALS_VERSION;
        sc.dwFlags = flags;
        SECURITY_STATUS st = AcquireCredentialsHandleA(nullptr, const_cast<char *>(UNISP_NAME_A), SECPKG_CRED_OUTBOUND,
                                                       nullptr, &sc, nullptr, nullptr, &cred_, nullptr);
        if (st != SEC_E_OK) { // Windows before 10 1809: legacy structure (TLS 1.2)
            SCHANNEL_CRED old{};
            old.dwVersion = SCHANNEL_CRED_VERSION;
            old.dwFlags = flags;
            old.grbitEnabledProtocols = SP_PROT_TLS1_2_CLIENT;
            st = AcquireCredentialsHandleA(nullptr, const_cast<char *>(UNISP_NAME_A), SECPKG_CRED_OUTBOUND, nullptr,
                                           &old, nullptr, nullptr, &cred_, nullptr);
        }
        if (st != SEC_E_OK)
            return fail("AcquireCredentialsHandle: " + winError(st));
        haveCred_ = true;
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
        if (state_ == State::Handshaking && !handshake())
            return state_;
        if (state_ != State::Open)
            return state_;
        encrypt();
        decrypt();
        return state_;
    }
    std::string alpn() const override { return alpn_; }
    std::string error() const override { return error_; }

  private:
    bool fail(std::string e) {
        error_ = std::move(e);
        state_ = State::Failed;
        return false;
    }

    bool loadRoots(const std::string &pem) {
        roots_ = CertOpenStore(CERT_STORE_PROV_MEMORY, 0, 0, 0, nullptr);
        size_t pos = 0;
        int added = 0;
        while ((pos = pem.find("-----BEGIN CERTIFICATE-----", pos)) != std::string::npos) {
            const size_t end = pem.find("-----END CERTIFICATE-----", pos);
            if (end == std::string::npos)
                break;
            const std::string block = pem.substr(pos, end + 25 - pos);
            DWORD len = 0;
            if (CryptStringToBinaryA(block.c_str(), DWORD(block.size()), CRYPT_STRING_BASE64HEADER, nullptr, &len, nullptr,
                                     nullptr)) {
                std::string der(len, '\0');
                if (CryptStringToBinaryA(block.c_str(), DWORD(block.size()), CRYPT_STRING_BASE64HEADER,
                                         reinterpret_cast<BYTE *>(der.data()), &len, nullptr, nullptr) &&
                    CertAddEncodedCertificateToStore(roots_, X509_ASN_ENCODING, reinterpret_cast<const BYTE *>(der.data()),
                                                     len, CERT_STORE_ADD_ALWAYS, nullptr))
                    ++added;
            }
            pos = end;
        }
        return added > 0 || fail("TlsConfig::caPem holds no certificate");
    }

    bool handshake() {
        for (;;) {
            if (haveCtx_ && in_.empty())
                return true; // waiting for the server
            SecBuffer inBufs[2] = {{ULONG(in_.size()), SECBUFFER_TOKEN, in_.data()}, {0, SECBUFFER_EMPTY, nullptr}};
            SecBufferDesc inDesc{SECBUFFER_VERSION, 2, inBufs};
            // First call: offer ALPN.
            std::string alpnBuf;
            SecBuffer alpnSec{};
            SecBufferDesc alpnDesc{SECBUFFER_VERSION, 1, &alpnSec};
            if (!haveCtx_ && !alpnWire_.empty()) {
                // SEC_APPLICATION_PROTOCOLS { ULONG size; SEC_APPLICATION_PROTOCOL_LIST { ext; USHORT size; list } }
                const ULONG listSize = ULONG(alpnWire_.size());
                alpnBuf.resize(sizeof(ULONG) + sizeof(int) + sizeof(USHORT) + listSize);
                char *p = alpnBuf.data();
                const ULONG total = ULONG(sizeof(int) + sizeof(USHORT) + listSize);
                const int ext = SecApplicationProtocolNegotiationExt_ALPN;
                const USHORT ls = USHORT(listSize);
                std::memcpy(p, &total, sizeof total);
                std::memcpy(p + sizeof(ULONG), &ext, sizeof ext);
                std::memcpy(p + sizeof(ULONG) + sizeof(int), &ls, sizeof ls);
                std::memcpy(p + sizeof(ULONG) + sizeof(int) + sizeof(USHORT), alpnWire_.data(), listSize);
                alpnSec = {ULONG(alpnBuf.size()), SECBUFFER_APPLICATION_PROTOCOLS, alpnBuf.data()};
            }
            SecBuffer outBuf{0, SECBUFFER_TOKEN, nullptr};
            SecBufferDesc outDesc{SECBUFFER_VERSION, 1, &outBuf};
            ULONG attrs = 0;
            const ULONG flags = ISC_REQ_SEQUENCE_DETECT | ISC_REQ_REPLAY_DETECT | ISC_REQ_CONFIDENTIALITY |
                                ISC_REQ_ALLOCATE_MEMORY | ISC_REQ_STREAM | ISC_REQ_EXTENDED_ERROR |
                                (manual_ ? ISC_REQ_MANUAL_CRED_VALIDATION : 0);
            SECURITY_STATUS st = InitializeSecurityContextA(
                &cred_, haveCtx_ ? &ctx_ : nullptr, const_cast<char *>(host_.c_str()), flags, 0, 0,
                haveCtx_ ? &inDesc : (alpnBuf.empty() ? nullptr : &alpnDesc), 0, &ctx_, &outDesc, &attrs, nullptr);
            haveCtx_ = true;
            if (outBuf.cbBuffer && outBuf.pvBuffer) {
                out_.append(static_cast<const char *>(outBuf.pvBuffer), outBuf.cbBuffer);
                FreeContextBuffer(outBuf.pvBuffer);
            }
            if (st == SEC_E_INCOMPLETE_MESSAGE)
                return true;
            // Unconsumed bytes stay for the next round.
            if (inBufs[1].BufferType == SECBUFFER_EXTRA && inBufs[1].cbBuffer > 0)
                in_.erase(0, in_.size() - inBufs[1].cbBuffer);
            else if (st == SEC_E_OK || st == SEC_I_CONTINUE_NEEDED)
                in_.clear();
            if (st == SEC_I_CONTINUE_NEEDED || st == SEC_I_INCOMPLETE_CREDENTIALS)
                continue;
            if (st != SEC_E_OK)
                return fail("handshake: " + winError(st));
            if (manual_ && verify_ && !checkChain())
                return false;
            SecPkgContext_ApplicationProtocol ap{};
            if (QueryContextAttributesA(&ctx_, SECPKG_ATTR_APPLICATION_PROTOCOL, &ap) == SEC_E_OK &&
                ap.ProtoNegoStatus == SecApplicationProtocolNegotiationStatus_Success)
                alpn_.assign(reinterpret_cast<const char *>(ap.ProtocolId), ap.ProtocolIdSize);
            if (QueryContextAttributesA(&ctx_, SECPKG_ATTR_STREAM_SIZES, &sizes_) != SEC_E_OK)
                return fail("cannot query stream sizes");
            state_ = State::Open;
            return true;
        }
    }

    bool checkChain() {
        PCCERT_CONTEXT cert = nullptr;
        if (QueryContextAttributesA(&ctx_, SECPKG_ATTR_REMOTE_CERT_CONTEXT, &cert) != SEC_E_OK || !cert)
            return fail("certificate verification failed: no server certificate");
        CERT_CHAIN_ENGINE_CONFIG ec{};
        ec.cbSize = sizeof ec;
        ec.hExclusiveRoot = roots_;
        HCERTCHAINENGINE engine = nullptr;
        bool ok = CertCreateCertificateChainEngine(&ec, &engine);
        PCCERT_CHAIN_CONTEXT chain = nullptr;
        CERT_CHAIN_PARA para{};
        para.cbSize = sizeof para;
        ok = ok && CertGetCertificateChain(engine, cert, nullptr, cert->hCertStore, &para, 0, nullptr, &chain);
        std::wstring wname(host_.begin(), host_.end());
        SSL_EXTRA_CERT_CHAIN_POLICY_PARA ssl{};
        ssl.cbSize = sizeof ssl;
        ssl.dwAuthType = AUTHTYPE_SERVER;
        ssl.pwszServerName = wname.data();
        CERT_CHAIN_POLICY_PARA pp{};
        pp.cbSize = sizeof pp;
        pp.pvExtraPolicyPara = &ssl;
        CERT_CHAIN_POLICY_STATUS ps{};
        ps.cbSize = sizeof ps;
        ok = ok && CertVerifyCertificateChainPolicy(CERT_CHAIN_POLICY_SSL, chain, &pp, &ps) && ps.dwError == 0;
        const DWORD err = ps.dwError;
        if (chain)
            CertFreeCertificateChain(chain);
        if (engine)
            CertFreeCertificateChainEngine(engine);
        CertFreeCertificateContext(cert);
        return ok || fail("certificate verification failed: " + winError(SECURITY_STATUS(err)));
    }

    void encrypt() {
        while (!pending_.empty() && state_ == State::Open) {
            const size_t n = std::min<size_t>(pending_.size(), sizes_.cbMaximumMessage);
            std::string rec(sizes_.cbHeader + n + sizes_.cbTrailer, '\0');
            std::memcpy(rec.data() + sizes_.cbHeader, pending_.data(), n);
            SecBuffer b[4] = {{sizes_.cbHeader, SECBUFFER_STREAM_HEADER, rec.data()},
                              {ULONG(n), SECBUFFER_DATA, rec.data() + sizes_.cbHeader},
                              {sizes_.cbTrailer, SECBUFFER_STREAM_TRAILER, rec.data() + sizes_.cbHeader + n},
                              {0, SECBUFFER_EMPTY, nullptr}};
            SecBufferDesc d{SECBUFFER_VERSION, 4, b};
            const SECURITY_STATUS st = EncryptMessage(&ctx_, 0, &d, 0);
            if (st != SEC_E_OK) {
                fail("EncryptMessage: " + winError(st));
                return;
            }
            out_.append(rec.data(), b[0].cbBuffer + b[1].cbBuffer + b[2].cbBuffer);
            pending_.erase(0, n);
        }
    }

    void decrypt() {
        while (!in_.empty() && state_ == State::Open) {
            SecBuffer b[4] = {{ULONG(in_.size()), SECBUFFER_DATA, in_.data()},
                              {0, SECBUFFER_EMPTY, nullptr},
                              {0, SECBUFFER_EMPTY, nullptr},
                              {0, SECBUFFER_EMPTY, nullptr}};
            SecBufferDesc d{SECBUFFER_VERSION, 4, b};
            const SECURITY_STATUS st = DecryptMessage(&ctx_, &d, 0, nullptr);
            if (st == SEC_E_INCOMPLETE_MESSAGE)
                return;
            if (st == SEC_I_CONTEXT_EXPIRED) {
                state_ = State::Closed;
                return;
            }
            if (st != SEC_E_OK && st != SEC_I_RENEGOTIATE) {
                fail("DecryptMessage: " + winError(st));
                return;
            }
            std::string extra;
            for (auto &x : b) {
                if (x.BufferType == SECBUFFER_DATA && x.cbBuffer)
                    plain_.append(static_cast<const char *>(x.pvBuffer), x.cbBuffer);
                else if (x.BufferType == SECBUFFER_EXTRA && x.cbBuffer)
                    extra.assign(static_cast<const char *>(x.pvBuffer), x.cbBuffer);
            }
            in_ = std::move(extra);
            if (st == SEC_I_RENEGOTIATE) {
                // TLS 1.3 post-handshake message (e.g. key update): feed it back into the handshake.
                state_ = State::Handshaking;
                if (!handshake() || state_ != State::Open)
                    return;
            }
        }
    }

    CredHandle cred_{};
    CtxtHandle ctx_{};
    bool haveCred_ = false, haveCtx_ = false, verify_ = true, manual_ = false;
    HCERTSTORE roots_ = nullptr;
    SecPkgContext_StreamSizes sizes_{};
    std::string host_, alpnWire_, alpn_, error_;
    std::string in_, out_, plain_, pending_;
    State state_ = State::Handshaking;
};

} // namespace

const char *tlsBackendName() { return "schannel"; }
bool tlsAvailable() { return true; }

std::unique_ptr<TlsEngine> createTlsClient(const TlsConfig &cfg, const std::string &host,
                                           const std::vector<std::string> &alpn, std::string *error) {
    auto e = std::make_unique<SchannelEngine>();
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
#endif
