#pragma once
#include <cstddef>
namespace aiverse::world {
// Last successful reconciliation; derived, not persisted or hashed.
struct RegionOwnershipDiagnostics {
    std::size_t trackedEntities=0, scannedCars=0, candidateCars=0, pendingGroups=0;
    std::size_t uncoveredSolo=0, uncoveredGroups=0, uncoveredLifecycle=0;
    bool journalOverflow=false, soloCandidateMode=false;
};
}
