// Agent link transport hardening (review fix 2026-11-21, docs/mcp-agent-link.md):
// line size cap, HTTP request lines rejected, owner-only session token file.
// Only apps/editor/core/Net.cpp is needed.
#include "apps/editor/core/Net.h"
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>
#if !defined(_WIN32)
#include <sys/stat.h>
#endif

using namespace aiverse::editor::net;

#define CHECK(x) do { if (!(x)) { std::cerr << "agent_link failed line " << __LINE__ << ": " #x "\n"; std::exit(1); } } while (false)

static std::vector<LineServer::Msg> pollFor(LineServer& server, int ms = 1000) {
    std::vector<LineServer::Msg> all;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < deadline) {
        auto m = server.poll();
        all.insert(all.end(), m.begin(), m.end());
        if (!all.empty()) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return all;
}
static void settle(LineServer& server, int ms = 200) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < deadline) { server.poll(); std::this_thread::sleep_for(std::chrono::milliseconds(5)); }
}

int main() {
    const auto dir = std::filesystem::temp_directory_path() / "aiverse_agent_link_test";
    std::filesystem::remove_all(dir);
#if defined(_WIN32)
    _putenv_s("AIVERSE_AGENT_LINK_DIR", dir.string().c_str());
#else
    setenv("AIVERSE_AGENT_LINK_DIR", dir.c_str(), 1);
#endif

    {   // plain servers (tests, tools) publish nothing
        LineServer plain;
        CHECK(plain.listen(0));
        CHECK(!plain.tokenPublished());
        CHECK(plain.token().size() == 64);
        CHECK(!std::filesystem::exists(agentTokenPath(plain.port())));
    }

    LineServer server(LineServer::Token::Publish);
    CHECK(server.listen(0));
    const int port = server.port();
    CHECK(server.tokenPublished());
    std::string token;
    CHECK(readAgentToken(port, token));
    CHECK(tokenEquals(token, server.token()));
    CHECK(!tokenEquals(token, std::string(token.size(), '0')));
    CHECK(!tokenEquals("", ""));
#if !defined(_WIN32)
    struct stat st{};
    CHECK(::stat(agentTokenPath(port).c_str(), &st) == 0);
    CHECK((st.st_mode & 0077) == 0);   // owner-only
#endif

    {   // JSON lines pass, authentication is per connection
        LineClient client;
        CHECK(client.connect(port));
        CHECK(client.send(R"({"id":1,"method":"hello"})"));
        auto m = pollFor(server);
        CHECK(m.size() == 1 && m[0].line == R"({"id":1,"method":"hello"})");
        CHECK(!server.authenticated(m[0].client));
        server.setAuthenticated(m[0].client);
        CHECK(server.authenticated(m[0].client));
        server.drop(m[0].client);
        CHECK(server.clients() == 0);
    }
    {   // a browser's request line drops the client and nothing of it is delivered
        LineClient browser;
        CHECK(browser.connect(port));
        CHECK(browser.send("POST / HTTP/1.1\r\nHost: 127.0.0.1\r\nContent-Type: text/plain\r\n\r\n"
                           R"({"id":1,"method":"call","params":{"name":"map_save"}})"));
        settle(server);
        CHECK(server.clients() == 0);
        std::string line;
        CHECK(!browser.recv(line, 300));
    }
    {   // a line beyond kMaxLineBytes drops the client and is never delivered
        LineClient ok;
        CHECK(ok.connect(port));
        // Send from another thread while the server polls: a 1 MiB line does
        // not fit loopback socket buffers on every OS (macOS keeps them small),
        // so a single-threaded send would block until sendAll gives up.
        bool okSent = false;
        std::thread okWriter([&] { okSent = ok.send(std::string(size_t(1) << 20, 'x')); });   // 1 MiB lines are fine
        bool gotOk = false;
        const auto okDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (!gotOk && std::chrono::steady_clock::now() < okDeadline)
            for (auto& m : server.poll()) if (m.line.size() == (size_t(1) << 20)) gotOk = true;
        okWriter.join();
        CHECK(okSent);
        CHECK(gotOk);
        CHECK(server.clients() == 1);
        LineClient big;
        CHECK(big.connect(port));
        // The send may fail once the server drops the connection; only the
        // server side matters here.
        std::thread bigWriter([&] { big.send(std::string(kMaxLineBytes + 1024, 'z')); });
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        bool gotHuge = false;
        while (std::chrono::steady_clock::now() < deadline && server.clients() != 1) {
            for (auto& m : server.poll()) if (m.line.size() > kMaxLineBytes) gotHuge = true;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        bigWriter.join();
        settle(server, 100);
        CHECK(!gotHuge);
        CHECK(server.clients() == 1);   // only `ok` is left
    }
    server.close();
    CHECK(!std::filesystem::exists(agentTokenPath(port)));   // removed with the listener
    std::filesystem::remove_all(dir);
    std::cout << "agent link transport passed\n";
    return 0;
}
