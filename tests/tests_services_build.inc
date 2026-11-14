// ---------------- aiversebuild plugin step for iOS (agent-sdk-plugins): Info.plist keys, SKAdNetworkItems, Podfile.
// The Xcode project itself is only generated on a Mac; this checks what aiversebuild writes for it.
#include "tools/aiversebuild/Plugins.h"

namespace {
std::string readText(const std::filesystem::path& p) { std::ifstream f(p, std::ios::binary); std::stringstream ss; ss << f.rdbuf(); return ss.str(); }
size_t countOf(const std::string& s, const std::string& part) {
    size_t n = 0;
    for (size_t at = s.find(part); at != std::string::npos; at = s.find(part, at + 1)) ++n;
    return n;
}
} // namespace

TEST(aiversebuild_ios_sdk_plugins_plist_and_skadnetwork) {
    namespace fs = std::filesystem;
    const fs::path engine = AIVERSE_SOURCE_DIR;
    const fs::path out = fs::temp_directory_path() / "aiverse_sdk_plugins_ios";
    fs::remove_all(out);
    // plugins that pull in an outside SDK must name the notices that ship with the build (Plugins.cpp)
    fs::create_directories(out / "project");
    { std::ofstream f(out / "project" / "NOTICE.txt"); f << "Third-party SDK notices for the test.\n"; }
    const std::string game = R"({
      "plugins": {
        "gameanalytics": { "gameKey": { "ios": "g" }, "secretKey": { "ios": "s" }, "notices": ["NOTICE.txt"] },
        "facebook": { "appId": "1347344110825377", "clientToken": "7f75", "displayName": "Drone Fall & Co", "notices": ["NOTICE.txt"] },
        "att": { "usage": "We use \"your\" data for ads & offers <3", "prompt": "ready",
                 "skAdNetworks": ["V9RVWE8QW5.skadnetwork", "cstr6suwn9", "4fzdc2evr5", "bad id"] },
        "admob": { "appId": { "ios": "ca-app-pub-3940256099942544~1458002511" }, "notices": ["NOTICE.txt"] }
      }
    })";
    build::PluginSet set = build::loadPlugins(engine, out / "project", game, "ios", "");
    CHECK((set.names == std::vector<std::string>{"gameanalytics", "facebook", "att", "admob"}));
    const std::string cm = readText(build::writePluginsCMake(set, out));
    // the ATT usage text is XML-escaped (then CMake-quoted: \" inside the set() string)
    CHECK(cm.find("<key>NSUserTrackingUsageDescription</key><string>We use &quot;your&quot; data for ads &amp; offers &lt;3</string>") != std::string::npos);
    CHECK(cm.find("<key>FacebookAppID</key><string>1347344110825377</string>") != std::string::npos);
    CHECK(cm.find("<key>FacebookDisplayName</key><string>Drone Fall &amp; Co</string>") != std::string::npos);
    CHECK(cm.find("<key>FacebookAutoLogAppEventsEnabled</key><true/>") != std::string::npos);   // plugin.json default
    // one SKAdNetworkItems list: plugin ids + the game's own, lower-cased, suffix stripped, deduplicated
    CHECK(countOf(cm, "SKAdNetworkItems") == 1);
    CHECK(countOf(cm, "<string>v9rvwe8qw5.skadnetwork</string>") == 1 && countOf(cm, "<string>cstr6suwn9.skadnetwork</string>") == 1);
    CHECK(countOf(cm, "<string>4fzdc2evr5.skadnetwork</string>") == 1 && countOf(cm, "<string>n6fk4nfna4.skadnetwork</string>") == 1);
    CHECK(cm.find("bad id") == std::string::npos);
    CHECK(cm.find("\\;") == std::string::npos);   // entities stay intact in the CMake string
    CHECK(cm.find("<key>att</key><string>AiverseAttPlugin</string>") != std::string::npos);
    CHECK(cm.find("plugins/att/ios/AiverseAttPlugin.mm") != std::string::npos);
    CHECK(cm.find("\"AppTrackingTransparency\"") != std::string::npos);
    const std::string pods = build::podfile(set, "15.0");
    CHECK(pods.find("pod 'GA-SDK-IOS', '~> 5.1'") != std::string::npos && pods.find("pod 'FBSDKCoreKit', '~> 18.0'") != std::string::npos);

    // only what is listed: GameAnalytics alone brings no tracking prompt, no ad network ids, no Facebook keys
    build::PluginSet ga = build::loadPlugins(engine, out / "project", game, "ios", "gameanalytics");
    const std::string cm2 = readText(build::writePluginsCMake(ga, out / "ga"));
    CHECK(cm2.find("NSUserTrackingUsageDescription") == std::string::npos && cm2.find("SKAdNetworkItems") == std::string::npos);
    CHECK(cm2.find("Facebook") == std::string::npos && cm2.find("AppTrackingTransparency") == std::string::npos);
    CHECK(build::podfile(ga, "15.0").find("FBSDKCoreKit") == std::string::npos);
    // ATT has no Android part: an Android build never gets it
    build::PluginSet android = build::loadPlugins(engine, out / "project", game, "android", "att,gameanalytics");
    CHECK((android.names == std::vector<std::string>{"gameanalytics"}));
    fs::remove_all(out);
}
