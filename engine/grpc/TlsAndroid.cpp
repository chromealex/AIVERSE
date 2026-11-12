// Android TLS: javax.net.ssl.SSLEngine through JNI (the platform's Conscrypt, system trust store,
// hostname verification, ALPN from API 29). Runs on the owning thread, which is attached to the VM
// on first use. AIVERSE_GRPC_JNI_TLS builds the same backend on a desktop JVM (OpenJDK SunJSSE) so
// the JNI flow can be tested off-device; it has not been run on an Android device.
#if defined(__ANDROID__) || defined(AIVERSE_GRPC_JNI_TLS)
#include "Tls.h"
#include <dlfcn.h>
#include <jni.h>
#include <algorithm>
#include <cstring>
#include <string>
#include <type_traits>

namespace aiverse::grpc {
namespace {
JavaVM *g_vm = nullptr;
}
void setAndroidJavaVM(void *vm) { g_vm = static_cast<JavaVM *>(vm); }
} // namespace aiverse::grpc

namespace aiverse::grpc::detail {
namespace {

JavaVM *javaVm() {
    if (g_vm)
        return g_vm;
    using GetVms = jint (*)(JavaVM **, jsize, jsize *);
    for (const char *lib : {"libnativehelper.so", "libart.so"}) {
        if (void *h = dlopen(lib, RTLD_NOW | RTLD_NOLOAD)) {
            if (auto fn = reinterpret_cast<GetVms>(dlsym(h, "JNI_GetCreatedJavaVMs"))) {
                jsize n = 0;
                if (fn(&g_vm, 1, &n) == JNI_OK && n > 0)
                    return g_vm;
            }
        }
    }
    return nullptr;
}

// The NDK declares AttachCurrentThread(JNIEnv **, void *); desktop JDKs use void **.
template <class VM> jint attachThread(VM *vm, JNIEnv **e) {
    if constexpr (std::is_invocable_v<decltype(&VM::AttachCurrentThread), VM *, JNIEnv **, void *>)
        return vm->AttachCurrentThread(e, nullptr);
    else
        return vm->AttachCurrentThread(reinterpret_cast<void **>(e), nullptr);
}

JNIEnv *env() {
    JavaVM *vm = javaVm();
    if (!vm)
        return nullptr;
    JNIEnv *e = nullptr;
    if (vm->GetEnv(reinterpret_cast<void **>(&e), JNI_VERSION_1_6) == JNI_OK)
        return e;
    return attachThread(vm, &e) == JNI_OK ? e : nullptr;
}

// Pending Java exception -> message (cleared).
bool takeException(JNIEnv *e, std::string &out) {
    if (!e->ExceptionCheck())
        return false;
    jthrowable t = e->ExceptionOccurred();
    e->ExceptionClear();
    jclass obj = e->FindClass("java/lang/Object");
    jmethodID toString = e->GetMethodID(obj, "toString", "()Ljava/lang/String;");
    auto s = static_cast<jstring>(e->CallObjectMethod(t, toString));
    e->ExceptionClear();
    if (s) {
        const char *c = e->GetStringUTFChars(s, nullptr);
        out = c ? c : "java exception";
        e->ReleaseStringUTFChars(s, c);
        e->DeleteLocalRef(s);
    } else {
        out = "java exception";
    }
    e->DeleteLocalRef(t);
    e->DeleteLocalRef(obj);
    return true;
}

class JniEngine final : public TlsEngine {
  public:
    ~JniEngine() override {
        if (JNIEnv *e = env()) {
            for (jobject *o : {&engine_, &netIn_, &netOut_, &appIn_, &appOut_})
                if (*o)
                    e->DeleteGlobalRef(*o);
        }
    }

    bool init(const TlsConfig &cfg, const std::string &host, const std::vector<std::string> &alpn) {
        JNIEnv *e = env();
        if (!e)
            return fail("no JavaVM: call aiverse::grpc::setAndroidJavaVM() at startup");
        if (!cfg.verifyPeer)
            return fail("TlsConfig::verifyPeer=false is not supported on Android");
        e->PushLocalFrame(64);
        const bool ok = build(e, cfg, host, alpn);
        e->PopLocalFrame(nullptr);
        return ok;
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
        JNIEnv *e = env();
        if (!e) {
            fail("JNI unavailable");
            return state_;
        }
        e->PushLocalFrame(64);
        for (int guard = 0; guard < 64 && state_ != State::Failed && state_ != State::Closed; ++guard) {
            const int hs = handshakeStatus(e);
            if (hs == kNeedTask) {
                runTasks(e);
            } else if (hs == kNeedWrap) {
                if (!wrap(e, ""))
                    break;
            } else if (hs == kNeedUnwrap) {
                if (in_.empty() || !unwrap(e))
                    break;
            } else { // FINISHED / NOT_HANDSHAKING
                if (state_ == State::Handshaking) {
                    state_ = State::Open;
                    readAlpn(e);
                }
                if (!pending_.empty()) {
                    std::string chunk = std::move(pending_);
                    pending_.clear();
                    if (!wrap(e, chunk))
                        break;
                    continue;
                }
                if (in_.empty() || !unwrap(e))
                    break;
            }
        }
        e->PopLocalFrame(nullptr);
        return state_;
    }
    std::string alpn() const override { return alpn_; }
    std::string error() const override { return error_; }

  private:
    // SSLEngineResult.HandshakeStatus ordinals: NOT_HANDSHAKING, FINISHED, NEED_TASK, NEED_WRAP, NEED_UNWRAP
    static constexpr int kNeedTask = 2, kNeedWrap = 3, kNeedUnwrap = 4;
    // SSLEngineResult.Status ordinals: BUFFER_UNDERFLOW, BUFFER_OVERFLOW, OK, CLOSED
    static constexpr int kUnderflow = 0, kOverflow = 1, kClosed = 3;

    bool fail(std::string m) {
        error_ = std::move(m);
        state_ = State::Failed;
        return false;
    }
    bool check(JNIEnv *e, const char *what) {
        std::string msg;
        if (takeException(e, msg))
            return fail(std::string(what) + ": " + msg);
        return true;
    }
    static jobject global(JNIEnv *e, jobject o) { return o ? e->NewGlobalRef(o) : nullptr; }

    bool build(JNIEnv *e, const TlsConfig &cfg, const std::string &host, const std::vector<std::string> &alpn) {
        jclass ctxCls = e->FindClass("javax/net/ssl/SSLContext");
        jobject ctx = nullptr;
        if (cfg.caPem.empty()) {
            ctx = e->CallStaticObjectMethod(ctxCls, e->GetStaticMethodID(ctxCls, "getDefault", "()Ljavax/net/ssl/SSLContext;"));
            if (!check(e, "SSLContext.getDefault"))
                return false;
        } else {
            jobject tms = trustManagers(e, cfg.caPem);
            if (!tms)
                return false;
            ctx = e->CallStaticObjectMethod(ctxCls, e->GetStaticMethodID(ctxCls, "getInstance", "(Ljava/lang/String;)Ljavax/net/ssl/SSLContext;"),
                                            e->NewStringUTF("TLS"));
            if (!check(e, "SSLContext.getInstance"))
                return false;
            e->CallVoidMethod(ctx, e->GetMethodID(ctxCls, "init",
                                                  "([Ljavax/net/ssl/KeyManager;[Ljavax/net/ssl/TrustManager;Ljava/security/SecureRandom;)V"),
                              nullptr, tms, nullptr);
            if (!check(e, "SSLContext.init"))
                return false;
        }
        jobject eng = e->CallObjectMethod(ctx, e->GetMethodID(ctxCls, "createSSLEngine", "(Ljava/lang/String;I)Ljavax/net/ssl/SSLEngine;"),
                                          e->NewStringUTF(host.c_str()), jint(443));
        if (!check(e, "createSSLEngine"))
            return false;
        jclass engCls = e->FindClass("javax/net/ssl/SSLEngine");
        e->CallVoidMethod(eng, e->GetMethodID(engCls, "setUseClientMode", "(Z)V"), JNI_TRUE);
        jclass parCls = e->FindClass("javax/net/ssl/SSLParameters");
        jobject par = e->CallObjectMethod(eng, e->GetMethodID(engCls, "getSSLParameters", "()Ljavax/net/ssl/SSLParameters;"));
        e->CallVoidMethod(par, e->GetMethodID(parCls, "setEndpointIdentificationAlgorithm", "(Ljava/lang/String;)V"),
                          e->NewStringUTF("HTTPS"));
        if (!check(e, "hostname verification"))
            return false;
        if (!alpn.empty()) {
            jmethodID setAp = e->GetMethodID(parCls, "setApplicationProtocols", "([Ljava/lang/String;)V");
            if (setAp) {
                jobjectArray arr = e->NewObjectArray(jsize(alpn.size()), e->FindClass("java/lang/String"), nullptr);
                for (size_t i = 0; i < alpn.size(); ++i)
                    e->SetObjectArrayElement(arr, jsize(i), e->NewStringUTF(alpn[i].c_str()));
                e->CallVoidMethod(par, setAp, arr);
            }
            e->ExceptionClear(); // API < 29: no ALPN (gRPC-Web over HTTP/1.1 still works)
        }
        e->CallVoidMethod(eng, e->GetMethodID(engCls, "setSSLParameters", "(Ljavax/net/ssl/SSLParameters;)V"), par);
        e->CallVoidMethod(eng, e->GetMethodID(engCls, "beginHandshake", "()V"));
        if (!check(e, "beginHandshake"))
            return false;
        jclass sesCls = e->FindClass("javax/net/ssl/SSLSession");
        jobject ses = e->CallObjectMethod(eng, e->GetMethodID(engCls, "getSession", "()Ljavax/net/ssl/SSLSession;"));
        const jint packet = e->CallIntMethod(ses, e->GetMethodID(sesCls, "getPacketBufferSize", "()I"));
        const jint app = e->CallIntMethod(ses, e->GetMethodID(sesCls, "getApplicationBufferSize", "()I"));
        if (!check(e, "SSLSession"))
            return false;
        jclass bb = e->FindClass("java/nio/ByteBuffer");
        jmethodID alloc = e->GetStaticMethodID(bb, "allocate", "(I)Ljava/nio/ByteBuffer;");
        engine_ = global(e, eng);
        netIn_ = global(e, e->CallStaticObjectMethod(bb, alloc, packet));
        netOut_ = global(e, e->CallStaticObjectMethod(bb, alloc, packet));
        appIn_ = global(e, e->CallStaticObjectMethod(bb, alloc, app + 1024));
        appOut_ = global(e, e->CallStaticObjectMethod(bb, alloc, app));
        packetSize_ = packet;
        // Method ids are valid for the life of the classes (system classes are never unloaded).
        mWrap_ = e->GetMethodID(engCls, "wrap", "(Ljava/nio/ByteBuffer;Ljava/nio/ByteBuffer;)Ljavax/net/ssl/SSLEngineResult;");
        mUnwrap_ = e->GetMethodID(engCls, "unwrap", "(Ljava/nio/ByteBuffer;Ljava/nio/ByteBuffer;)Ljavax/net/ssl/SSLEngineResult;");
        mHs_ = e->GetMethodID(engCls, "getHandshakeStatus", "()Ljavax/net/ssl/SSLEngineResult$HandshakeStatus;");
        mTask_ = e->GetMethodID(engCls, "getDelegatedTask", "()Ljava/lang/Runnable;");
        mApp_ = e->GetMethodID(engCls, "getApplicationProtocol", "()Ljava/lang/String;");
        e->ExceptionClear();
        jclass resCls = e->FindClass("javax/net/ssl/SSLEngineResult");
        mResStatus_ = e->GetMethodID(resCls, "getStatus", "()Ljavax/net/ssl/SSLEngineResult$Status;");
        mConsumed_ = e->GetMethodID(resCls, "bytesConsumed", "()I");
        mProduced_ = e->GetMethodID(resCls, "bytesProduced", "()I");
        mOrdinal_ = e->GetMethodID(e->FindClass("java/lang/Enum"), "ordinal", "()I");
        mClear_ = e->GetMethodID(bb, "clear", "()Ljava/nio/Buffer;");
        mFlip_ = e->GetMethodID(bb, "flip", "()Ljava/nio/Buffer;");
        mPut_ = e->GetMethodID(bb, "put", "([BII)Ljava/nio/ByteBuffer;");
        mGet_ = e->GetMethodID(bb, "get", "([BII)Ljava/nio/ByteBuffer;");
        mRemaining_ = e->GetMethodID(e->FindClass("java/nio/Buffer"), "remaining", "()I");
        return check(e, "JNI setup") && engine_ && netIn_ && netOut_ && appIn_ && appOut_;
    }

    jobject trustManagers(JNIEnv *e, const std::string &pem) {
        jclass cf = e->FindClass("java/security/cert/CertificateFactory");
        jobject factory = e->CallStaticObjectMethod(cf, e->GetStaticMethodID(cf, "getInstance", "(Ljava/lang/String;)Ljava/security/cert/CertificateFactory;"),
                                                    e->NewStringUTF("X.509"));
        jbyteArray bytes = e->NewByteArray(jsize(pem.size()));
        e->SetByteArrayRegion(bytes, 0, jsize(pem.size()), reinterpret_cast<const jbyte *>(pem.data()));
        jclass bais = e->FindClass("java/io/ByteArrayInputStream");
        jobject in = e->NewObject(bais, e->GetMethodID(bais, "<init>", "([B)V"), bytes);
        jobject certs = e->CallObjectMethod(factory, e->GetMethodID(cf, "generateCertificates", "(Ljava/io/InputStream;)Ljava/util/Collection;"), in);
        if (!check(e, "caPem"))
            return nullptr;
        jclass ksCls = e->FindClass("java/security/KeyStore");
        jstring type = static_cast<jstring>(e->CallStaticObjectMethod(ksCls, e->GetStaticMethodID(ksCls, "getDefaultType", "()Ljava/lang/String;")));
        jobject ks = e->CallStaticObjectMethod(ksCls, e->GetStaticMethodID(ksCls, "getInstance", "(Ljava/lang/String;)Ljava/security/KeyStore;"), type);
        e->CallVoidMethod(ks, e->GetMethodID(ksCls, "load", "(Ljava/io/InputStream;[C)V"), nullptr, nullptr);
        jclass col = e->FindClass("java/util/Collection");
        jobjectArray arr = static_cast<jobjectArray>(e->CallObjectMethod(certs, e->GetMethodID(col, "toArray", "()[Ljava/lang/Object;")));
        const jsize n = arr ? e->GetArrayLength(arr) : 0;
        jmethodID setEntry = e->GetMethodID(ksCls, "setCertificateEntry", "(Ljava/lang/String;Ljava/security/cert/Certificate;)V");
        for (jsize i = 0; i < n; ++i) {
            const std::string alias = "ca" + std::to_string(i);
            e->CallVoidMethod(ks, setEntry, e->NewStringUTF(alias.c_str()), e->GetObjectArrayElement(arr, i));
        }
        if (!check(e, "KeyStore"))
            return nullptr;
        if (n == 0) {
            fail("TlsConfig::caPem holds no certificate");
            return nullptr;
        }
        jclass tmf = e->FindClass("javax/net/ssl/TrustManagerFactory");
        jstring alg = static_cast<jstring>(e->CallStaticObjectMethod(tmf, e->GetStaticMethodID(tmf, "getDefaultAlgorithm", "()Ljava/lang/String;")));
        jobject f = e->CallStaticObjectMethod(tmf, e->GetStaticMethodID(tmf, "getInstance", "(Ljava/lang/String;)Ljavax/net/ssl/TrustManagerFactory;"), alg);
        e->CallVoidMethod(f, e->GetMethodID(tmf, "init", "(Ljava/security/KeyStore;)V"), ks);
        jobject tms = e->CallObjectMethod(f, e->GetMethodID(tmf, "getTrustManagers", "()[Ljavax/net/ssl/TrustManager;"));
        return check(e, "TrustManagerFactory") ? tms : nullptr;
    }

    int ordinal(JNIEnv *e, jobject en) { return en ? e->CallIntMethod(en, mOrdinal_) : -1; }
    int handshakeStatus(JNIEnv *e) {
        jobject hs = e->CallObjectMethod(engine_, mHs_);
        const int o = ordinal(e, hs);
        e->DeleteLocalRef(hs);
        return o;
    }
    void runTasks(JNIEnv *e) {
        jclass run = e->FindClass("java/lang/Runnable");
        jmethodID mRun = e->GetMethodID(run, "run", "()V");
        while (jobject t = e->CallObjectMethod(engine_, mTask_)) {
            e->CallVoidMethod(t, mRun);
            e->DeleteLocalRef(t);
            if (!check(e, "delegated task"))
                return;
        }
        e->DeleteLocalRef(run);
    }
    void putBytes(JNIEnv *e, jobject buf, const std::string &data) {
        jbyteArray a = e->NewByteArray(jsize(data.size()));
        e->SetByteArrayRegion(a, 0, jsize(data.size()), reinterpret_cast<const jbyte *>(data.data()));
        e->DeleteLocalRef(e->CallObjectMethod(buf, mPut_, a, 0, jint(data.size())));
        e->DeleteLocalRef(a);
    }
    void drain(JNIEnv *e, jobject buf, std::string &to) { // buffer in write mode -> append its content
        e->DeleteLocalRef(e->CallObjectMethod(buf, mFlip_));
        const jint n = e->CallIntMethod(buf, mRemaining_);
        if (n > 0) {
            jbyteArray a = e->NewByteArray(n);
            e->DeleteLocalRef(e->CallObjectMethod(buf, mGet_, a, 0, n));
            const size_t at = to.size();
            to.resize(at + size_t(n));
            e->GetByteArrayRegion(a, 0, n, reinterpret_cast<jbyte *>(&to[at]));
            e->DeleteLocalRef(a);
        }
        e->DeleteLocalRef(e->CallObjectMethod(buf, mClear_));
    }
    bool wrap(JNIEnv *e, const std::string &plain) {
        size_t off = 0;
        do {
            const size_t n = std::min<size_t>(plain.size() - off, 16384);
            e->DeleteLocalRef(e->CallObjectMethod(appOut_, mClear_));
            putBytes(e, appOut_, plain.substr(off, n));
            e->DeleteLocalRef(e->CallObjectMethod(appOut_, mFlip_));
            jobject res = e->CallObjectMethod(engine_, mWrap_, appOut_, netOut_);
            if (!check(e, "wrap"))
                return false;
            const int st = ordinal(e, e->CallObjectMethod(res, mResStatus_));
            const jint consumed = e->CallIntMethod(res, mConsumed_);
            e->DeleteLocalRef(res);
            drain(e, netOut_, out_);
            if (st == kClosed) {
                state_ = State::Closed;
                return false;
            }
            if (st == kOverflow)
                return fail("wrap: buffer overflow");
            off += size_t(consumed);
            if (consumed == 0 && n > 0)
                return fail("wrap made no progress");
        } while (off < plain.size());
        return true;
    }
    bool unwrap(JNIEnv *e) {
        const size_t n = std::min<size_t>(in_.size(), size_t(packetSize_));
        e->DeleteLocalRef(e->CallObjectMethod(netIn_, mClear_));
        putBytes(e, netIn_, in_.substr(0, n));
        e->DeleteLocalRef(e->CallObjectMethod(netIn_, mFlip_));
        jobject res = e->CallObjectMethod(engine_, mUnwrap_, netIn_, appIn_);
        if (!check(e, state_ == State::Handshaking ? "certificate verification failed or handshake error" : "unwrap"))
            return false;
        const int st = ordinal(e, e->CallObjectMethod(res, mResStatus_));
        const jint consumed = e->CallIntMethod(res, mConsumed_);
        e->DeleteLocalRef(res);
        drain(e, appIn_, plain_);
        in_.erase(0, size_t(consumed));
        if (st == kClosed) {
            state_ = State::Closed;
            return false;
        }
        if (st == kOverflow)
            return fail("unwrap: buffer overflow");
        return st != kUnderflow && consumed > 0; // underflow: wait for more ciphertext
    }
    void readAlpn(JNIEnv *e) {
        if (!mApp_)
            return;
        auto s = static_cast<jstring>(e->CallObjectMethod(engine_, mApp_));
        e->ExceptionClear();
        if (s) {
            const char *c = e->GetStringUTFChars(s, nullptr);
            alpn_ = c ? c : "";
            e->ReleaseStringUTFChars(s, c);
        }
    }

    jobject engine_ = nullptr, netIn_ = nullptr, netOut_ = nullptr, appIn_ = nullptr, appOut_ = nullptr;
    jint packetSize_ = 16709;
    jmethodID mWrap_{}, mUnwrap_{}, mHs_{}, mTask_{}, mApp_{}, mResStatus_{}, mConsumed_{}, mProduced_{}, mOrdinal_{},
        mClear_{}, mFlip_{}, mPut_{}, mGet_{}, mRemaining_{};
    std::string in_, out_, plain_, pending_, alpn_, error_;
    State state_ = State::Handshaking;
};

} // namespace

const char *tlsBackendName() { return "android-sslengine"; }
bool tlsAvailable() { return javaVm() != nullptr; }

std::unique_ptr<TlsEngine> createTlsClient(const TlsConfig &cfg, const std::string &host,
                                           const std::vector<std::string> &alpn, std::string *error) {
    auto e = std::make_unique<JniEngine>();
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
