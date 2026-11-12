// Desktop mocks of the service plugins for development and tests: "mock-store", "mock-ads",
// "mock-analytics" (records every call) and "mock-att" (App Tracking Transparency prompt).
#pragma once
#include <string>
#include <vector>

namespace aiverse::services {
inline constexpr const char* kMockPlugins[] = {"mock-store", "mock-ads", "mock-analytics", "mock-att", "mock-social", "mock-push", "mock-links", "mock-consent"};
// Registers every mock / one of them. The bridge does it for mocks listed in game.json "plugins" or
// with AIVERSE_MOCK_PLUGINS=1; call it yourself in tests or tools.
void registerMockPlugins();
void registerMockPlugin(const std::string& name);
// The calls mock-analytics received ("method {args}"), oldest first; clear = forget them.
std::vector<std::string> mockAnalyticsLog(bool clear = false);
// Client service mocks "mock-social", "mock-push", "mock-links", "mock-consent" (MockClientPlugins.cpp); false: not one of them.
bool registerClientMockPlugin(const std::string& name);
} // namespace aiverse::services
