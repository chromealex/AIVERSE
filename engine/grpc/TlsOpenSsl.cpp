// OpenSSL backend loaded at runtime: no headers or import libraries are needed to build, and a
// machine without libssl simply reports TLS as unavailable (plain http:// keeps working).
// Used on Linux and other POSIX systems that are not Apple / Android.
#if !defined(__EMSCRIPTEN__) && !defined(__APPLE__) && !defined(__ANDROID__) && !defined(_WIN32) && \
    !defined(AIVERSE_GRPC_JNI_TLS)
#include "Tls.h"
#include <dlfcn.h>
#include <cstring>

namespace aiverse::grpc::detail {
namespace {

// Opaque OpenSSL types.
struct SSL;
struct SSL_CTX;
struct SSL_METHOD;
struct BIO;
struct BIO_METHOD;
struct X509;
struct X509_STORE;
struct EVP_PKEY;
using AlpnSelectCb = int (*)(SSL *, const unsigned char **, unsigned char *, const unsigned char *, unsigned int,
                             void *);

struct Api {
    bool ok = false;
    std::string error;
    const SSL_METHOD *(*TLS_client_method)();
    const SSL_METHOD *(*TLS_server_method)();
    SSL_CTX *(*SSL_CTX_new)(const SSL_METHOD *);
    void (*SSL_CTX_free)(SSL_CTX *);
    long (*SSL_CTX_ctrl)(SSL_CTX *, int, long, void *);
    void (*SSL_CTX_set_verify)(SSL_CTX *, int, void *);
    int (*SSL_CTX_set_default_verify_paths)(SSL_CTX *);
    X509_STORE *(*SSL_CTX_get_cert_store)(const SSL_CTX *);
    int (*SSL_CTX_use_certificate)(SSL_CTX *, X509 *);
    int (*SSL_CTX_use_PrivateKey)(SSL_CTX *, EVP_PKEY *);
    void (*SSL_CTX_set_alpn_select_cb)(SSL_CTX *, AlpnSelectCb, void *);
    SSL *(*SSL_new)(SSL_CTX *);
    void (*SSL_free)(SSL *);
    void (*SSL_set_bio)(SSL *, BIO *, BIO *);
    void (*SSL_set_connect_state)(SSL *);
    void (*SSL_set_accept_state)(SSL *);
    int (*SSL_do_handshake)(SSL *);
    int (*SSL_read)(SSL *, void *, int);
    int (*SSL_write)(SSL *, const void *, int);
    int (*SSL_get_error)(const SSL *, int);
    long (*SSL_ctrl)(SSL *, int, long, void *);
    int (*SSL_set1_host)(SSL *, const char *);
    int (*SSL_set_alpn_protos)(SSL *, const unsigned char *, unsigned int);
    void (*SSL_get0_alpn_selected)(const SSL *, const unsigned char **, unsigned int *);
    int (*SSL_select_next_proto)(unsigned char **, unsigned char *, const unsigned char *, unsigned int,
                                 const unsigned char *, unsigned int);
    long (*SSL_get_verify_result)(const SSL *);
    BIO *(*BIO_new)(const BIO_METHOD *);
    const BIO_METHOD *(*BIO_s_mem)();
    BIO *(*BIO_new_mem_buf)(const void *, int);
    int (*BIO_free)(BIO *);
    int (*BIO_read)(BIO *, void *, int);
    int (*BIO_write)(BIO *, const void *, int);
    long (*BIO_ctrl)(BIO *, int, long, void *);
    X509 *(*PEM_read_bio_X509)(BIO *, X509 **, void *, void *);
    EVP_PKEY *(*PEM_read_bio_PrivateKey)(BIO *, EVP_PKEY **, void *, void *);
    void (*X509_free)(X509 *);
    void (*EVP_PKEY_free)(EVP_PKEY *);
    int (*X509_STORE_add_cert)(X509_STORE *, X509 *);
    const char *(*X509_verify_cert_error_string)(long);
    unsigned long (*ERR_get_error)();
    void (*ERR_error_string_n)(unsigned long, char *, size_t);
    void (*ERR_clear_error)();
};

constexpr int kSslErrorWantRead = 2, kSslErrorWantWrite = 3, kSslErrorZeroReturn = 6;
constexpr int kCtrlSetTlsextHostname = 55, kCtrlSetMinProto = 123, kTls12 = 0x0303;
constexpr int kBioCtrlPending = 10, kBioCSetBufMemEofReturn = 130;
constexpr int kVerifyPeer = 1;

const Api &api() {
    static Api a = [] {
        Api r{};
        void *crypto = nullptr, *ssl = nullptr;
        for (const char *n : {"libcrypto.so.3", "libcrypto.so.1.1", "libcrypto.so"})
            if ((crypto = dlopen(n, RTLD_NOW | RTLD_GLOBAL)))
                break;
        for (const char *n : {"libssl.so.3", "libssl.so.1.1", "libssl.so"})
            if ((ssl = dlopen(n, RTLD_NOW | RTLD_GLOBAL)))
                break;
        if (!crypto || !ssl) {
            r.error = "OpenSSL (libssl.so.3 / libssl.so.1.1) not found";
            return r;
        }
        bool all = true;
        auto load = [&](auto &fn, void *lib, const char *name) {
            void *p = dlsym(lib, name);
            if (!p) {
                all = false;
                if (r.error.empty())
                    r.error = std::string("OpenSSL symbol missing: ") + name;
            }
            std::memcpy(&fn, &p, sizeof p);
        };
#define S(name) load(r.name, ssl, #name)
#define C(name) load(r.name, crypto, #name)
        S(TLS_client_method); S(TLS_server_method); S(SSL_CTX_new); S(SSL_CTX_free); S(SSL_CTX_ctrl);
        S(SSL_CTX_set_verify); S(SSL_CTX_set_default_verify_paths); S(SSL_CTX_get_cert_store);
        S(SSL_CTX_use_certificate); S(SSL_CTX_use_PrivateKey); S(SSL_CTX_set_alpn_select_cb); S(SSL_new);
        S(SSL_free); S(SSL_set_bio); S(SSL_set_connect_state); S(SSL_set_accept_state); S(SSL_do_handshake);
        S(SSL_read); S(SSL_write); S(SSL_get_error); S(SSL_ctrl); S(SSL_set1_host); S(SSL_set_alpn_protos);
        S(SSL_get0_alpn_selected); S(SSL_select_next_proto); S(SSL_get_verify_result);
        C(BIO_new); C(BIO_s_mem); C(BIO_new_mem_buf); C(BIO_free); C(BIO_read); C(BIO_write); C(BIO_ctrl);
        C(PEM_read_bio_X509); C(PEM_read_bio_PrivateKey); C(X509_free); C(EVP_PKEY_free);
        C(X509_STORE_add_cert); C(X509_verify_cert_error_string); C(ERR_get_error); C(ERR_error_string_n);
        C(ERR_clear_error);
#undef S
#undef C
        r.ok = all;
        return r;
    }();
    return a;
}

std::string lastError(const Api &a) {
    std::string out;
    while (unsigned long e = a.ERR_get_error()) {
        char buf[256];
        a.ERR_error_string_n(e, buf, sizeof buf);
        if (!out.empty())
            out += "; ";
        out += buf;
    }
    return out.empty() ? "unknown TLS error" : out;
}

std::string alpnWire(const std::vector<std::string> &protos) {
    std::string w;
    for (const auto &p : protos) {
        w.push_back(char(p.size()));
        w += p;
    }
    return w;
}

int selectAlpn(SSL *, const unsigned char **out, unsigned char *outlen, const unsigned char *in, unsigned int inlen,
               void *arg) {
    const auto *ours = static_cast<const std::string *>(arg);
    unsigned char *sel = nullptr;
    // OPENSSL_NPN_NEGOTIATED == 1
    if (api().SSL_select_next_proto(&sel, outlen, reinterpret_cast<const unsigned char *>(ours->data()),
                                    unsigned(ours->size()), in, inlen) != 1)
        return 3; // SSL_TLSEXT_ERR_NOACK
    *out = sel;
    return 0;
}

class OpenSslEngine final : public TlsEngine {
  public:
    ~OpenSslEngine() override {
        const Api &a = api();
        if (ssl_)
            a.SSL_free(ssl_); // frees both BIOs
        if (ctx_)
            a.SSL_CTX_free(ctx_);
    }

    bool initClient(const TlsConfig &cfg, const std::string &host, const std::vector<std::string> &alpn) {
        const Api &a = api();
        ctx_ = a.SSL_CTX_new(a.TLS_client_method());
        if (!ctx_)
            return fail(lastError(a));
        a.SSL_CTX_ctrl(ctx_, kCtrlSetMinProto, kTls12, nullptr);
        if (cfg.verifyPeer) {
            a.SSL_CTX_set_verify(ctx_, kVerifyPeer, nullptr);
            a.SSL_CTX_set_default_verify_paths(ctx_);
            if (!cfg.caPem.empty() && !addRoots(cfg.caPem))
                return false;
        }
        if (!makeSsl())
            return false;
        a.SSL_set_connect_state(ssl_);
        const bool ipLiteral = host.find_first_not_of("0123456789.") == std::string::npos || host.find(':') != std::string::npos;
        if (!ipLiteral)
            a.SSL_ctrl(ssl_, kCtrlSetTlsextHostname, 0, const_cast<char *>(host.c_str()));
        if (cfg.verifyPeer && a.SSL_set1_host(ssl_, host.c_str()) != 1)
            return fail("cannot set expected host name");
        if (!alpn.empty()) {
            const std::string w = alpnWire(alpn);
            a.SSL_set_alpn_protos(ssl_, reinterpret_cast<const unsigned char *>(w.data()), unsigned(w.size()));
        }
        return true;
    }

    bool initServer(const std::string &certPem, const std::string &keyPem, const std::vector<std::string> &alpn) {
        const Api &a = api();
        ctx_ = a.SSL_CTX_new(a.TLS_server_method());
        if (!ctx_)
            return fail(lastError(a));
        BIO *cb = a.BIO_new_mem_buf(certPem.data(), int(certPem.size()));
        X509 *cert = a.PEM_read_bio_X509(cb, nullptr, nullptr, nullptr);
        a.BIO_free(cb);
        BIO *kb = a.BIO_new_mem_buf(keyPem.data(), int(keyPem.size()));
        EVP_PKEY *key = a.PEM_read_bio_PrivateKey(kb, nullptr, nullptr, nullptr);
        a.BIO_free(kb);
        const bool ok = cert && key && a.SSL_CTX_use_certificate(ctx_, cert) == 1 &&
                        a.SSL_CTX_use_PrivateKey(ctx_, key) == 1;
        if (cert)
            a.X509_free(cert);
        if (key)
            a.EVP_PKEY_free(key);
        if (!ok)
            return fail("bad server certificate/key: " + lastError(a));
        serverAlpn_ = alpnWire(alpn);
        if (!serverAlpn_.empty())
            a.SSL_CTX_set_alpn_select_cb(ctx_, selectAlpn, &serverAlpn_);
        if (!makeSsl())
            return false;
        a.SSL_set_accept_state(ssl_);
        return true;
    }

    void putCipher(const uint8_t *d, size_t n) override {
        if (n)
            api().BIO_write(rbio_, d, int(n));
    }
    void takeCipher(std::string &out) override {
        const Api &a = api();
        char buf[16384];
        while (a.BIO_ctrl(wbio_, kBioCtrlPending, 0, nullptr) > 0) {
            const int n = a.BIO_read(wbio_, buf, sizeof buf);
            if (n <= 0)
                break;
            out.append(buf, size_t(n));
        }
    }
    void writePlain(const uint8_t *d, size_t n) override { pendingPlain_.append(reinterpret_cast<const char *>(d), n); }
    void readPlain(std::string &out) override {
        out += plainIn_;
        plainIn_.clear();
    }

    State step() override {
        const Api &a = api();
        if (state_ == State::Failed || state_ == State::Closed)
            return state_;
        if (state_ == State::Handshaking) {
            a.ERR_clear_error();
            const int r = a.SSL_do_handshake(ssl_);
            if (r == 1) {
                state_ = State::Open;
                const unsigned char *p = nullptr;
                unsigned int len = 0;
                a.SSL_get0_alpn_selected(ssl_, &p, &len);
                alpn_.assign(reinterpret_cast<const char *>(p), p ? len : 0);
            } else {
                const int e = a.SSL_get_error(ssl_, r);
                if (e != kSslErrorWantRead && e != kSslErrorWantWrite) {
                    const long v = a.SSL_get_verify_result(ssl_);
                    fail(v != 0 ? std::string("certificate verification failed: ") + a.X509_verify_cert_error_string(v)
                                : "handshake failed: " + lastError(a));
                    return state_;
                }
                return state_;
            }
        }
        while (!pendingPlain_.empty()) {
            a.ERR_clear_error();
            const int n = a.SSL_write(ssl_, pendingPlain_.data(), int(pendingPlain_.size()));
            if (n <= 0) {
                const int e = a.SSL_get_error(ssl_, n);
                if (e != kSslErrorWantRead && e != kSslErrorWantWrite)
                    fail("write failed: " + lastError(a));
                break;
            }
            pendingPlain_.erase(0, size_t(n));
        }
        char buf[16384];
        for (;;) {
            a.ERR_clear_error();
            const int n = a.SSL_read(ssl_, buf, sizeof buf);
            if (n > 0) {
                plainIn_.append(buf, size_t(n));
                continue;
            }
            const int e = a.SSL_get_error(ssl_, n);
            if (e == kSslErrorZeroReturn)
                state_ = State::Closed;
            else if (e != kSslErrorWantRead && e != kSslErrorWantWrite)
                fail("read failed: " + lastError(a));
            break;
        }
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
    bool addRoots(const std::string &pem) {
        const Api &a = api();
        BIO *b = a.BIO_new_mem_buf(pem.data(), int(pem.size()));
        X509_STORE *store = a.SSL_CTX_get_cert_store(ctx_);
        int added = 0;
        while (X509 *c = a.PEM_read_bio_X509(b, nullptr, nullptr, nullptr)) {
            a.X509_STORE_add_cert(store, c);
            a.X509_free(c);
            ++added;
        }
        a.BIO_free(b);
        a.ERR_clear_error(); // the end-of-PEM read leaves an error behind
        return added > 0 || fail("TlsConfig::caPem holds no certificate");
    }
    bool makeSsl() {
        const Api &a = api();
        ssl_ = a.SSL_new(ctx_);
        rbio_ = a.BIO_new(a.BIO_s_mem());
        wbio_ = a.BIO_new(a.BIO_s_mem());
        if (!ssl_ || !rbio_ || !wbio_)
            return fail(lastError(a));
        // An empty read BIO means "no data yet", not EOF.
        a.BIO_ctrl(rbio_, kBioCSetBufMemEofReturn, -1, nullptr);
        a.SSL_set_bio(ssl_, rbio_, wbio_);
        return true;
    }

    SSL_CTX *ctx_ = nullptr;
    SSL *ssl_ = nullptr;
    BIO *rbio_ = nullptr;
    BIO *wbio_ = nullptr;
    State state_ = State::Handshaking;
    std::string pendingPlain_, plainIn_, alpn_, error_, serverAlpn_;
};

} // namespace

const char *tlsBackendName() { return "openssl"; }
bool tlsAvailable() { return api().ok; }

std::unique_ptr<TlsEngine> createTlsClient(const TlsConfig &cfg, const std::string &host,
                                           const std::vector<std::string> &alpn, std::string *error) {
    if (!api().ok) {
        if (error)
            *error = api().error;
        return nullptr;
    }
    auto e = std::make_unique<OpenSslEngine>();
    if (!e->initClient(cfg, host, alpn)) {
        if (error)
            *error = e->error();
        return nullptr;
    }
    return e;
}

std::unique_ptr<TlsEngine> createTlsServer(const std::string &certPem, const std::string &keyPem,
                                           const std::vector<std::string> &alpn, std::string *error) {
    if (!api().ok) {
        if (error)
            *error = api().error;
        return nullptr;
    }
    auto e = std::make_unique<OpenSslEngine>();
    if (!e->initServer(certPem, keyPem, alpn)) {
        if (error)
            *error = e->error();
        return nullptr;
    }
    return e;
}

} // namespace aiverse::grpc::detail
#endif
