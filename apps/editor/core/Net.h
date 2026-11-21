// Minimal localhost TCP with newline-delimited JSON messages: the running editor listens, the MCP
// server connects. Non-blocking server side so the editor polls it once per frame on the UI thread.
#pragma once
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace aiverse::editor::net {

constexpr int kDefaultPort = 47321;
constexpr size_t kMaxLineBytes = size_t(4) << 20;   // a longer line (or unterminated buffer) drops the client

// Agent-link session token (protocol 2, docs/mcp-agent-link.md): the editor writes a random
// token to <home>/.aiverse/agent-link/<port>.token (owner-only), a client proves it can read
// that file by sending {"method":"hello","token":...} first. AIVERSE_AGENT_LINK_DIR overrides
// the folder (tests).
std::filesystem::path agentTokenPath(int port);
bool readAgentToken(int port, std::string& token);
std::string randomToken();                          // 64 hex characters
bool tokenEquals(const std::string& a, const std::string& b);   // constant time for equal lengths

class LineServer {
public:
    enum class Token { None, Publish };
    explicit LineServer(Token token = Token::None) : publishToken_(token == Token::Publish) {}
    LineServer(const LineServer&) = delete;
    LineServer& operator=(const LineServer&) = delete;
    ~LineServer();
    bool listen(int port);                 // 127.0.0.1 only; 0 asks the OS for a free port
    bool listenAvailable(int preferredPort = kDefaultPort);
    // Accepts connections and returns complete lines received since the last poll, tagged by client.
    // A client is dropped (its lines of this poll discarded) when a line exceeds kMaxLineBytes or
    // looks like an HTTP request line (a browser's no-cors POST must never reach the JSON handler).
    struct Msg { int client; std::string line; };
    std::vector<Msg> poll();
    void send(int client, const std::string& line);   // appends '\n'
    bool listening() const { return fd_ >= 0; }
    void close();                          // stop listening, drop clients (frees the port for another editor)
    int port() const { return port_; }
    int clients() const { return int(conns_.size()); }   // connected MCP servers (status pill)
    void drop(int client);                          // close one client connection now
    void setAuthenticated(int client);
    bool authenticated(int client) const;
    // Session token of this listener; with Token::Publish it is written to agentTokenPath(port())
    // when listening starts and removed by close(). Empty while not listening.
    const std::string& token() const { return token_; }
    bool tokenPublished() const { return tokenPublished_; }

private:
    struct Conn { intptr_t fd; std::string in; bool authenticated = false; };
    intptr_t fd_ = -1;
    int port_ = 0;
    std::vector<Conn> conns_;
    bool publishToken_ = false, tokenPublished_ = false;
    std::string token_;
};

class LineClient {
public:
    ~LineClient();
    bool connect(int port, int timeoutMs = 300);
    bool send(const std::string& line);
    bool recv(std::string& line, int timeoutMs);      // waits for one full line
    void close();
    bool connected() const { return fd_ >= 0; }

private:
    intptr_t fd_ = -1;
    std::string buf_;
};

} // namespace aiverse::editor::net
