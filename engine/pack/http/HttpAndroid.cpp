// HTTP(S) fetcher for Android: java.net.HttpURLConnection over JNI (system TLS, no extra deps).
// The download thread attaches itself to the JavaVM the platform layer hands over.
#include <jni.h>

#include <cstdio>

#include <thread>

#include "HttpRequest.h"
#include "ThreadDownload.h"

namespace aiverse::pack {

namespace {
JavaVM* g_vm = nullptr;

bool check(JNIEnv* env, ThreadDownload& d, const char* what) {
    if (!env->ExceptionCheck()) return true;
    env->ExceptionClear();
    d.fail(std::string(what) + " failed");
    return false;
}
} // namespace

void setAndroidJavaVm(void* vm) { g_vm = static_cast<JavaVM*>(vm); }

std::unique_ptr<Download> fetchHttp(const std::string& url, const std::string& destPath, std::string& error) {
    if (!g_vm) { error = "HTTP downloads: JavaVM not set"; return nullptr; }
    auto dl = std::make_unique<ThreadDownload>();
    dl->run([url, destPath](ThreadDownload& d) {
        JNIEnv* env = nullptr;
        if (g_vm->AttachCurrentThread(&env, nullptr) != JNI_OK || !env) { d.fail("cannot attach to the JavaVM"); return; }
        uint64_t have = 0;
        if (std::FILE* f = std::fopen(destPath.c_str(), "rb")) { std::fseek(f, 0, SEEK_END); have = uint64_t(std::ftell(f)); std::fclose(f); }
        env->PushLocalFrame(32);
        [&] {
            jclass urlCls = env->FindClass("java/net/URL");
            jobject u = env->NewObject(urlCls, env->GetMethodID(urlCls, "<init>", "(Ljava/lang/String;)V"), env->NewStringUTF(url.c_str()));
            if (!check(env, d, "URL")) return;
            jobject conn = env->CallObjectMethod(u, env->GetMethodID(urlCls, "openConnection", "()Ljava/net/URLConnection;"));
            if (!check(env, d, "openConnection")) return;
            jclass hc = env->FindClass("java/net/HttpURLConnection");
            env->CallVoidMethod(conn, env->GetMethodID(hc, "setConnectTimeout", "(I)V"), jint(20000));
            env->CallVoidMethod(conn, env->GetMethodID(hc, "setReadTimeout", "(I)V"), jint(60000));
            if (have) {
                std::string range = "bytes=" + std::to_string(have) + "-";
                env->CallVoidMethod(conn, env->GetMethodID(hc, "setRequestProperty", "(Ljava/lang/String;Ljava/lang/String;)V"),
                                    env->NewStringUTF("Range"), env->NewStringUTF(range.c_str()));
            }
            jint code = env->CallIntMethod(conn, env->GetMethodID(hc, "getResponseCode", "()I"));
            if (!check(env, d, "HTTP request")) return;
            if (code == 416) { d.succeed(); return; }
            if (code != 200 && code != 206) { d.fail("HTTP " + std::to_string(int(code))); return; }
            if (code == 200) have = 0;
            jobject in = env->CallObjectMethod(conn, env->GetMethodID(hc, "getInputStream", "()Ljava/io/InputStream;"));
            if (!check(env, d, "getInputStream")) return;
            jclass isCls = env->FindClass("java/io/InputStream");
            jmethodID readM = env->GetMethodID(isCls, "read", "([B)I");
            jbyteArray buf = env->NewByteArray(1 << 16);
            std::FILE* f = std::fopen(destPath.c_str(), have ? "ab" : "wb");
            if (!f) { d.fail("cannot write " + destPath); return; }
            d.bytes_ = have;
            for (;;) {
                jint n = env->CallIntMethod(in, readM, buf);
                if (!check(env, d, "read")) break;
                if (n < 0) { d.succeed(); break; }
                jbyte* p = env->GetByteArrayElements(buf, nullptr);
                size_t w = std::fwrite(p, 1, size_t(n), f);
                env->ReleaseByteArrayElements(buf, p, JNI_ABORT);
                if (w != size_t(n)) { d.fail("cannot write " + destPath); break; }
                d.bytes_ += uint64_t(n);
            }
            std::fclose(f);
            env->CallVoidMethod(in, env->GetMethodID(isCls, "close", "()V"));
            env->ExceptionClear();
        }();
        env->PopLocalFrame(nullptr);
        g_vm->DetachCurrentThread();
    });
    return dl;
}

} // namespace aiverse::pack

namespace aiverse::pack {

namespace {
// Reads a java.io.InputStream (may be null) to the end.
std::string readAll(JNIEnv* env, jobject in) {
    std::string out;
    if (!in) return out;
    jclass isCls = env->FindClass("java/io/InputStream");
    jmethodID readM = env->GetMethodID(isCls, "read", "([B)I");
    jbyteArray buf = env->NewByteArray(1 << 16);
    for (;;) {
        jint n = env->CallIntMethod(in, readM, buf);
        if (env->ExceptionCheck() || n < 0) { env->ExceptionClear(); break; }
        jbyte* p = env->GetByteArrayElements(buf, nullptr);
        out.append(reinterpret_cast<const char*>(p), size_t(n));
        env->ReleaseByteArrayElements(buf, p, JNI_ABORT);
    }
    env->CallVoidMethod(in, env->GetMethodID(isCls, "close", "()V"));
    env->ExceptionClear();
    return out;
}
std::string jstr(JNIEnv* env, jstring s) {
    if (!s) return {};
    const char* c = env->GetStringUTFChars(s, nullptr);
    std::string out = c ? c : "";
    if (c) env->ReleaseStringUTFChars(s, c);
    return out;
}
} // namespace

bool httpRequest(const HttpCall& call, std::function<void(HttpReply)> done) {
    if (!g_vm) return false;
    std::thread([call, done = std::move(done)]() {
        HttpReply r;
        JNIEnv* env = nullptr;
        if (g_vm->AttachCurrentThread(&env, nullptr) != JNI_OK || !env) { r.error = "cannot attach to the JavaVM"; done(std::move(r)); return; }
        env->PushLocalFrame(64);
        [&] {
            auto failed = [&](const char* what) {
                if (!env->ExceptionCheck()) return false;
                jthrowable ex = env->ExceptionOccurred();
                env->ExceptionClear();
                r = HttpReply{};
                r.timeout = env->IsInstanceOf(ex, env->FindClass("java/net/SocketTimeoutException"));
                r.error = std::string(what) + " failed";
                return true;
            };
            jclass urlCls = env->FindClass("java/net/URL");
            jobject u = env->NewObject(urlCls, env->GetMethodID(urlCls, "<init>", "(Ljava/lang/String;)V"), env->NewStringUTF(call.url.c_str()));
            if (failed("URL")) return;
            jobject conn = env->CallObjectMethod(u, env->GetMethodID(urlCls, "openConnection", "()Ljava/net/URLConnection;"));
            if (failed("openConnection")) return;
            jclass hc = env->FindClass("java/net/HttpURLConnection");
            env->CallVoidMethod(conn, env->GetMethodID(hc, "setConnectTimeout", "(I)V"), jint(call.timeoutMs));
            env->CallVoidMethod(conn, env->GetMethodID(hc, "setReadTimeout", "(I)V"), jint(call.timeoutMs));
            env->CallVoidMethod(conn, env->GetMethodID(hc, "setRequestMethod", "(Ljava/lang/String;)V"), env->NewStringUTF(call.method.c_str()));
            if (failed("setRequestMethod")) return;
            jmethodID setProp = env->GetMethodID(hc, "setRequestProperty", "(Ljava/lang/String;Ljava/lang/String;)V");
            for (auto& [k, v] : call.headers) env->CallVoidMethod(conn, setProp, env->NewStringUTF(k.c_str()), env->NewStringUTF(v.c_str()));
            if (!call.body.empty()) {
                env->CallVoidMethod(conn, env->GetMethodID(hc, "setDoOutput", "(Z)V"), jboolean(true));
                jobject os = env->CallObjectMethod(conn, env->GetMethodID(hc, "getOutputStream", "()Ljava/io/OutputStream;"));
                if (failed("connect")) return;
                jclass osCls = env->FindClass("java/io/OutputStream");
                jbyteArray b = env->NewByteArray(jsize(call.body.size()));
                env->SetByteArrayRegion(b, 0, jsize(call.body.size()), reinterpret_cast<const jbyte*>(call.body.data()));
                env->CallVoidMethod(os, env->GetMethodID(osCls, "write", "([B)V"), b);
                env->CallVoidMethod(os, env->GetMethodID(osCls, "close", "()V"));
                if (failed("send")) return;
            }
            jint code = env->CallIntMethod(conn, env->GetMethodID(hc, "getResponseCode", "()I"));
            if (failed("HTTP request")) return;
            r.status = int(code);
            jmethodID keyM = env->GetMethodID(hc, "getHeaderFieldKey", "(I)Ljava/lang/String;");
            jmethodID valM = env->GetMethodID(hc, "getHeaderField", "(I)Ljava/lang/String;");
            for (jint i = 1; i < 200; ++i) {   // index 0 is the status line
                jstring k = jstring(env->CallObjectMethod(conn, keyM, i));
                if (env->ExceptionCheck() || !k) { env->ExceptionClear(); break; }
                r.headers.push_back({jstr(env, k), jstr(env, jstring(env->CallObjectMethod(conn, valM, i)))});
            }
            jobject in = env->CallObjectMethod(conn, env->GetMethodID(hc, code >= 400 ? "getErrorStream" : "getInputStream", "()Ljava/io/InputStream;"));
            if (env->ExceptionCheck()) { env->ExceptionClear(); in = nullptr; }
            r.body = readAll(env, in);
        }();
        env->PopLocalFrame(nullptr);
        g_vm->DetachCurrentThread();
        done(std::move(r));
    }).detach();
    return true;
}

} // namespace aiverse::pack
