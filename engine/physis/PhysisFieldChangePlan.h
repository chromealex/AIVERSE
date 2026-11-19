// PHYSIS Stage 22: deterministic authored-field change planning.
// Pure planning only: the authoritative Simulation/World owner must apply changes
// at a tick boundary, with rollback and entity identity handled by the host.
#pragma once
#include "PhysisFieldAssets.h"
#include <algorithm>
#include <string>
#include <vector>

namespace aiverse::physis {

enum class FieldChangeKind : std::uint8_t { Remove, Update, Add };

struct FieldChange {
    FieldChangeKind operation{};
    std::string id;
    AuthoredFieldKind kind{};
};

// Compare explicit serialized properties, not memory bytes (padding/ABI differ).
// The caller supplies the canonical asset JSON fingerprints from its validated
// MapDoc; a changed fingerprint means a component must be replaced.
struct FieldRevision {
    std::string id;
    AuthoredFieldKind kind{};
    std::string canonicalJson;
};

inline std::vector<FieldChange> planFieldChanges(
    std::vector<FieldRevision> before, std::vector<FieldRevision> after) {
    const auto less = [](const FieldRevision& a, const FieldRevision& b) {
        return a.id < b.id;
    };
    const auto validate = [&](std::vector<FieldRevision>& fields) {
        std::sort(fields.begin(), fields.end(), less);
        for (std::size_t i = 0; i < fields.size(); ++i) {
            if (fields[i].id.empty())
                throw std::invalid_argument("PHYSIS field revision has empty ID");
            if (i && fields[i-1].id == fields[i].id)
                throw std::invalid_argument("PHYSIS duplicate field revision ID");
        }
    };
    validate(before);
    validate(after);
    std::vector<FieldChange> changes;
    std::size_t i = 0, j = 0;
    while (i < before.size() || j < after.size()) {
        if (j == after.size() ||
            (i < before.size() && before[i].id < after[j].id)) {
            changes.push_back({FieldChangeKind::Remove, before[i].id, before[i].kind});
            ++i;
        } else if (i == before.size() || after[j].id < before[i].id) {
            changes.push_back({FieldChangeKind::Add, after[j].id, after[j].kind});
            ++j;
        } else {
            if (before[i].kind != after[j].kind) {
                changes.push_back({FieldChangeKind::Remove, before[i].id, before[i].kind});
                changes.push_back({FieldChangeKind::Add, after[j].id, after[j].kind});
            } else if (before[i].canonicalJson != after[j].canonicalJson) {
                changes.push_back({FieldChangeKind::Update, after[j].id, after[j].kind});
            }
            ++i;
            ++j;
        }
    }
    return changes;
}
} // namespace aiverse::physis
