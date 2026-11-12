// Deep links: the game was opened from a URL (custom scheme "bunkerwars://invite?c=..", an
// https universal / app link, an attribution link of AppsFlyer OneLink, a payment return of a web
// shop) — at launch or while running. Protocol "links" (docs/plugins.md): plugin "deeplinks"
// (Android intent filters, iOS URL schemes + associated domains, web URL), every attribution
// plugin that resolves deferred links (appsflyer), and "mock-links" on desktop.
//
//   "plugins": { "deeplinks": { "schemes": ["bunkerwars"], "hosts": ["bunkerwars.onelink.me"] } }
//   "links": { "providers": ["deeplinks", "appsflyer"] }    // optional: default = every one built in
//
// Unlike store / ads, every provider reports: a link comes from whichever saw it first, and the
// service drops a repeat of the same URL within a few seconds (OS + attribution SDK both report it).
#pragma once
#include <functional>
#include <string>
#include <vector>

namespace aiverse::links {

struct Link {
    std::string url;
    std::string source;      // provider ("deeplinks", "appsflyer", ...)
    bool deferred = false;   // first launch after install through the link
    std::string dataJson = "{}";   // provider extras (campaign, media source, deep_link_value ...)
};

bool available();
std::vector<std::string> providers();
// The link the game was launched with (asks every provider once; "" url when there is none).
void initial(std::function<void(const Link&)> cb);
// Every link while running (and the launch link, once).
int onLink(std::function<void(const Link&)> cb);
void removeListener(int id);

// "bunkerwars://invite?c=42&x=y" -> path "invite", query {c: 42, x: y} (percent-decoded).
struct Parsed { std::string scheme, host, path; std::vector<std::pair<std::string, std::string>> query; };
Parsed parse(const std::string& url);
std::string query(const Parsed& p, const std::string& key, const std::string& def = "");

void reset();   // tests

} // namespace aiverse::links
