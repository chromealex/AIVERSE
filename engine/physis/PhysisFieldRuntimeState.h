// PHYSIS canonical authored-field identity, shared by setup and runtime.
// Persistent schema IDs and field-wise Archive layout are unchanged.
#pragma once
#include "PhysisRuntimeFields.h"
#include <cstdint>
#include <map>
#include <string>

namespace aiverse::physis {
struct FieldRuntimeEntry {
    Entity entity = kNullEntity;
    AuthoredFieldKind kind = AuthoredFieldKind::Wind;
    std::string fingerprint;
};
struct FieldRuntimeState {
    std::uint64_t revision = 0;
    std::map<std::string, FieldRuntimeEntry> byId;
};
inline void io(Archive& a, FieldRuntimeEntry& entry) {
    a(entry.entity, entry.kind, entry.fingerprint);
}
inline void io(Archive& a, FieldRuntimeState& state) {
    a(state.revision, state.byId);
}
} // namespace aiverse::physis
