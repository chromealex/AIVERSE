// TLS engines over in-memory buffers, so one non-blocking socket loop serves every platform.
// Backends (createTlsClient picks the platform's):
//   Linux / other POSIX: OpenSSL 3 / 1.1 loaded at runtime (dlopen; no build dependency).
//   macOS / iOS:          Secure Transport (SSLSetIOFuncs over buffers).          [unverified here]
//   Windows:              SChannel (SSPI InitializeSecurityContext over buffers).  [unverified here]
//   Android:              javax.net.ssl.SSLEngine through JNI.                     [unverified here]
//   Web:                  none (the browser owns TLS; see WebTransport.cpp).
#pragma once
#include "Grpc.h"
#include <memory>
#include <string>
#include <vector>

namespace aiverse::grpc::detail {

class TlsEngine {
  public:
    enum class State { Handshaking, Open, Closed, Failed };
    virtual ~TlsEngine() = default;
    // Ciphertext received from the socket.
    virtual void putCipher(const uint8_t *data, size_t n) = 0;
    // Ciphertext to send on the socket (appended).
    virtual void takeCipher(std::string &out) = 0;
    // Plaintext: queued for encryption / decrypted bytes appended to out.
    virtual void writePlain(const uint8_t *data, size_t n) = 0;
    virtual void readPlain(std::string &out) = 0;
    // Advances the handshake / record processing. Call after put/write.
    virtual State step() = 0;
    virtual std::string alpn() const = 0; // negotiated protocol ("h2"), empty if none
    virtual std::string error() const = 0;
};

// host: SNI + certificate name check. alpn: offered protocols in preference order.
std::unique_ptr<TlsEngine> createTlsClient(const TlsConfig &cfg, const std::string &host,
                                           const std::vector<std::string> &alpn, std::string *error);
// Test server side (OpenSSL backend only; null elsewhere).
std::unique_ptr<TlsEngine> createTlsServer(const std::string &certPem, const std::string &keyPem,
                                           const std::vector<std::string> &alpn, std::string *error);
// Name of the backend compiled for this platform ("openssl", "securetransport", ...), and whether it
// is usable right now (e.g. OpenSSL found at runtime).
const char *tlsBackendName();
bool tlsAvailable();

} // namespace aiverse::grpc::detail

namespace aiverse::grpc {
// Android: the TLS backend calls javax.net.ssl.SSLEngine through JNI. Hosts should register the
// process JavaVM once (NativeActivity: app->activity->vm); without it the backend falls back to
// JNI_GetCreatedJavaVMs (public from API 31). No-op on other platforms.
void setAndroidJavaVM(void *javaVm);
} // namespace aiverse::grpc

namespace aiverse::grpc::detail {

} // namespace aiverse::grpc::detail
