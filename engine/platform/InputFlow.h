// AIVerse InputFlow — canonical platform host facade.
//
// This API is presentation-only; authoritative ECS consumes validated Commands.
// Existing EnhancedInput* headers and names remain available for older projects.
#pragma once

#include <aiverselib/gameplay/InputFlow.h>
#include "EnhancedInputAdapter.h"
#include "EnhancedInputConfig.h"
#include "EnhancedInputFileIO.h"
#include "EnhancedInputProfileIO.h"
#include "EnhancedInputPresets.h"
#include "EnhancedInputUserProfiles.h"
#include "EnhancedInputUsers.h"

namespace aiverse::platform {
// InputFlowAdapter and InputFlowUsers are the concrete implementations.
// Their EnhancedInput* aliases are defined in the legacy headers.
using InputFlowUserProfiles = enhanced_config::UserProfiles;
using InputFlowAsset = enhanced_config::Asset;
using InputFlowProfile = enhanced_config::Profile;
namespace inputflow_config = enhanced_config;
namespace inputflow_presets = enhanced_presets;
} // namespace aiverse::platform
