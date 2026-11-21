#include "Net.h"
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <random>
#include <system_error>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#if defined(_MSC_VER)
#pragma comment(lib, "ws2_32.lib")   // MinGW links ws2_32 through CMake
#endif
using socklen_t = int;
#define AIVERSE_CLOSE closesocket
static void netInit() { static bool done = false; if (!done) { WSADATA d; WSAStartup(MAKEWORD(2, 2), &d); done = true; } }
static void setNonBlocking(intptr_t fd) { u_long m = 1; ioctlsocket(SOCKET(fd), FIONBIO, &m); }
static bool wouldBlock() { return WSAGetLastError() == WSAEWOULDBLOCK; }
#else
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#define AIVERSE_CLOSE ::close
static void netInit() {}
static void setNonBlocking(intptr_t fd) { fcntl(int(fd), F_SETFL, fcntl(int(fd), F_GETFL, 0) | O_NONBLOCK); }
static bool wouldBlock() { return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINPROGRESS; }
#endif

#if !defined(MSG_NOSIGNAL)
#define MSG_NOSIGNAL 0
#endif

namespace aiverse::editor::net {

namespace {
bool sendAll(intptr_t fd, const std::string& data) {
    size_t off = 0;
    auto start = std::chrono::steady_clock::now();
    while (off < data.size()) {
        auto n = ::send(fd, data.data() + off, int(data.size() - off), MSG_NOSIGNAL);
        if (n > 0) { off += size_t(n); continue; }
        if (n < 0 && wouldBlock()) {
            if (std::chrono::steady_clock::now() - start > std::chrono::seconds(5)) return false;
            continue;
        }
        return false;
    }
    return true;
}
sockaddr_in loopback(int port) {
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(uint16_t(port));
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    return a;
}
// "GET / HTTP/1.1", "POST /x HTTP/1.1", ...: never valid agent-link JSON.
bool httpRequestLine(const std::string& line) {
    static const char* const methods[] = {"GET ", "POST ", "PUT ", "OPTIONS ", "HEAD ", "DELETE ",
                                          "PATCH ", "CONNECT ", "TRACE ", "PRI "};
    for (const char* m : methods)
        if (line.compare(0, std::strlen(m), m) == 0) return true;
    return false;
}
std::filesystem::path agentLinkDir() {
    if (const char* d = std::getenv("AIVERSE_AGENT_LINK_DIR"); d && *d) return d;
    const char* home = std::getenv("HOME");
#if defined(_WIN32)
    if (!home || !*home) home = std::getenv("USERPROFILE");
#endif
    if (!home || !*home) return {};
    return std::filesystem::path(home) / ".aiverse" / "agent-link";
}
bool writeOwnerOnly(const std::filesystem::path& path, const std::string& text) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
#if defined(_WIN32)
    // %USERPROFILE% is private to the user by its default ACL
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out << text;
    out.close();
    return bool(out);
#else
    ::chmod(path.parent_path().c_str(), 0700);
    const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW, 0600);
    if (fd < 0) return false;
    ::fchmod(fd, 0600);   // an existing file keeps its mode on O_CREAT
    size_t off = 0;
    while (off < text.size()) {
        const auto n = ::write(fd, text.data() + off, text.size() - off);
        if (n <= 0) { ::close(fd); return false; }
        off += size_t(n);
    }
    return ::close(fd) == 0;
#endif
}
} // namespace

std::filesystem::path agentTokenPath(int port) {
    const auto dir = agentLinkDir();
    return dir.empty() ? std::filesystem::path() : dir / (std::to_string(port) + ".token");
}

bool readAgentToken(int port, std::string& token) {
    const auto path = agentTokenPath(port);
    if (path.empty()) return false;
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ')) text.pop_back();
    if (text.empty() || text.size() > 256) return false;
    token = text;
    return true;
}

std::string randomToken() {
    std::random_device rd;   // OS CSPRNG on the supported desktop toolchains
    static const char* hex = "0123456789abcdef";
    std::string out;
    out.reserve(64);
    for (int i = 0; i < 8; ++i) {
        uint32_t v = rd();
        for (int k = 0; k < 8; ++k) { out += hex[v & 15u]; v >>= 4; }
    }
    return out;
}

bool tokenEquals(const std::string& a, const std::string& b) {
    if (a.empty() || a.size() != b.size()) return false;
    unsigned char diff = 0;
    for (size_t i = 0; i < a.size(); ++i) diff |= static_cast<unsigned char>(a[i] ^ b[i]);
    return diff == 0;
}

LineServer::~LineServer() { close(); }

void LineServer::close() {
    for (auto& c : conns_) AIVERSE_CLOSE(c.fd);
    conns_.clear();
    if (fd_ >= 0) AIVERSE_CLOSE(fd_);
    if (tokenPublished_) {
        // remove only our own file (another editor may have taken the port since)
        std::string onDisk;
        if (readAgentToken(port_, onDisk) && tokenEquals(onDisk, token_)) {
            std::error_code ec;
            std::filesystem::remove(agentTokenPath(port_), ec);
        }
    }
    tokenPublished_ = false;
    token_.clear();
    fd_ = -1;
    port_ = 0;
}

void LineServer::drop(int client) {
    for (size_t i = 0; i < conns_.size(); ++i)
        if (int(conns_[i].fd) == client) { AIVERSE_CLOSE(conns_[i].fd); conns_.erase(conns_.begin() + long(i)); return; }
}

void LineServer::setAuthenticated(int client) {
    for (auto& c : conns_) if (int(c.fd) == client) c.authenticated = true;
}

bool LineServer::authenticated(int client) const {
    for (const auto& c : conns_) if (int(c.fd) == client) return c.authenticated;
    return false;
}

bool LineServer::listen(int port) {
    if (fd_ >= 0 || port < 0 || port > 65535) return false;
    netInit();
    intptr_t fd = intptr_t(::socket(AF_INET, SOCK_STREAM, 0));
    if (fd < 0) return false;
    int yes = 1;
#if defined(_WIN32)
    setsockopt(fd, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&yes), sizeof yes);
#else
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&yes), sizeof yes);
#endif
    sockaddr_in a = loopback(port);
    if (::bind(fd, reinterpret_cast<sockaddr*>(&a), sizeof a) != 0 || ::listen(fd, 4) != 0) { AIVERSE_CLOSE(fd); return false; }
    socklen_t size = sizeof a;
    if (::getsockname(fd, reinterpret_cast<sockaddr*>(&a), &size) != 0) { AIVERSE_CLOSE(fd); return false; }
    setNonBlocking(fd);
    fd_ = fd;
    port_ = ntohs(a.sin_port);
    token_ = randomToken();
    if (publishToken_) {
        const auto path = agentTokenPath(port_);
        tokenPublished_ = !path.empty() && writeOwnerOnly(path, token_ + "\n");
    }
    return true;
}

// Keep the conventional endpoint for the first editor; bind atomically to an OS-assigned
// port for additional editors, without a probe/close/rebind race.
bool LineServer::listenAvailable(int preferredPort) {
    return listen(preferredPort) || listen(0);
}

std::vector<LineServer::Msg> LineServer::poll() {
    std::vector<Msg> out;
    if (fd_ < 0) return out;
    for (;;) {
        intptr_t c = intptr_t(::accept(fd_, nullptr, nullptr));
        if (c < 0) break;
        setNonBlocking(c);
        conns_.push_back({c, {}});
    }
    char buf[65536];
    for (size_t i = 0; i < conns_.size();) {
        bool closed = false, overflow = false;
        for (;;) {
            auto n = ::recv(conns_[i].fd, buf, sizeof buf, 0);
            if (n > 0) {
                conns_[i].in.append(buf, size_t(n));
                // bound the buffer: an unterminated line cannot grow memory without limit
                if ((conns_[i].in.size() > kMaxLineBytes && conns_[i].in.find('\n') == std::string::npos) ||
                    conns_[i].in.size() > 4 * kMaxLineBytes) { overflow = true; break; }
                continue;
            }
            if (n == 0 || !wouldBlock()) closed = true;
            break;
        }
        const size_t first = out.size();
        bool bad = overflow;
        size_t nl;
        while (!bad && (nl = conns_[i].in.find('\n')) != std::string::npos) {
            std::string line = conns_[i].in.substr(0, nl);
            conns_[i].in.erase(0, nl + 1);
            if (line.size() > kMaxLineBytes || httpRequestLine(line)) { bad = true; break; }
            out.push_back({int(conns_[i].fd), std::move(line)});
        }
        if (!bad && conns_[i].in.size() > kMaxLineBytes) bad = true;
        if (bad) { out.resize(first); closed = true; }   // nothing from a rejected client is handled
        if (closed) { AIVERSE_CLOSE(conns_[i].fd); conns_.erase(conns_.begin() + long(i)); }
        else ++i;
    }
    return out;
}

void LineServer::send(int client, const std::string& line) {
    for (auto& c : conns_) if (int(c.fd) == client) { sendAll(c.fd, line + "\n"); return; }
}

LineClient::~LineClient() { close(); }

void LineClient::close() {
    if (fd_ >= 0) AIVERSE_CLOSE(fd_);
    fd_ = -1;
    buf_.clear();
}

bool LineClient::connect(int port, int timeoutMs) {
    netInit();
    close();
    intptr_t fd = intptr_t(::socket(AF_INET, SOCK_STREAM, 0));
    if (fd < 0) return false;
    sockaddr_in a = loopback(port);
    setNonBlocking(fd);
    int r = ::connect(fd, reinterpret_cast<sockaddr*>(&a), sizeof a);
    if (r != 0 && !wouldBlock()) { AIVERSE_CLOSE(fd); return false; }
    if (r != 0) {
        fd_set w; FD_ZERO(&w); FD_SET(fd, &w);
        timeval tv{timeoutMs / 1000, (timeoutMs % 1000) * 1000};
        if (::select(int(fd + 1), nullptr, &w, nullptr, &tv) <= 0) { AIVERSE_CLOSE(fd); return false; }
        int err = 0;
        socklen_t len = sizeof err;
        getsockopt(fd, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&err), &len);
        if (err != 0) { AIVERSE_CLOSE(fd); return false; }
    }
    fd_ = fd;
    return true;
}

bool LineClient::send(const std::string& line) {
    if (fd_ < 0) return false;
    if (!sendAll(fd_, line + "\n")) { close(); return false; }
    return true;
}

bool LineClient::recv(std::string& line, int timeoutMs) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    char buf[65536];
    for (;;) {
        size_t nl = buf_.find('\n');
        if (nl != std::string::npos) { line = buf_.substr(0, nl); buf_.erase(0, nl + 1); return true; }
        if (fd_ < 0) return false;
        auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
        if (left <= 0) return false;
        fd_set rset; FD_ZERO(&rset); FD_SET(fd_, &rset);
        timeval tv{}; tv.tv_sec = decltype(tv.tv_sec)(left / 1000); tv.tv_usec = decltype(tv.tv_usec)((left % 1000) * 1000);
        if (::select(int(fd_ + 1), &rset, nullptr, nullptr, &tv) <= 0) continue;
        auto n = ::recv(fd_, buf, sizeof buf, 0);
        if (n > 0) buf_.append(buf, size_t(n));
        else if (n == 0 || !wouldBlock()) { close(); return false; }
    }
}

} // namespace aiverse::editor::net
