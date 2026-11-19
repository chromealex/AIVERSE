// PHYSIS installation-time guard for mutually exclusive authored-field modes.
// This is an ephemeral setup marker, NOT an authoritative ECS save resource.
// Persisted runtime identity remains FieldRuntimeState or RuntimeFieldRegistry.
#pragma once
#include <cstdint>
namespace aiverse::physis {
enum class AuthoredFieldMode : std::uint8_t {
    Static = 1,
    Tracked = 2,
    Editable = 3
};
struct AuthoredFieldInstallMode {
    AuthoredFieldMode mode = AuthoredFieldMode::Static;
};
} // namespace aiverse::physis
