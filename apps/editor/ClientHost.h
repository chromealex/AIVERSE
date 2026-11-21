#pragma once
// Compatibility name for the shared native game host; no editor UI ownership.
#include "apps/client/HostedClient.h"
namespace aiverse::editor { using ClientHost = client::HostedClient; }
