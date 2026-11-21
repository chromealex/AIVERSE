// SpectraFX Stage 42 — privacy-safe *summary*, never raw GPU particle data.
// Explicit opt-in is required for disclosing world-space bounds or GPU frame
// identifiers; no scalar VM/program inputs or per-slot data are ever emitted.
#pragma once

#include "SpectraCheckpointInspector.h"
#include <cstdint>
#include <iomanip>
#include <locale>
#include <sstream>
#include <string>

namespace aiverse::spectra {

struct GpuDiagnosticReportPrivacy {
    bool includeWorldBounds=false; // may reveal private scene geometry
    bool includeGpuIdentifiers=false; // exact RHI frame/revision/CRC fingerprints
};

// Fixed-schema JSON with no external strings (even source file paths are
// excluded), so no untrusted text escaping is needed. The default report is
// suitable for an internal bug ticket without disclosing world coordinates,
// VM scalar values or the raw checkpoint itself.
inline std::string gpuDiagnosticReportJson(
    const GpuDiagnosticInspection& summary,
    GpuDiagnosticReportPrivacy privacy={}) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out<<std::setprecision(9);
    out<<"{\n"
       <<"  \"format\": \"SPFX-V2\",\n"
       <<"  \"kind\": \"spectrafx_sanitized_diagnostic\",\n"
       <<"  \"containsRawGpuState\": false,\n"
       <<"  \"containsVmScalarValues\": false,\n"
       <<"  \"containsWorldCoordinates\": "
       <<(privacy.includeWorldBounds?"true":"false")<<",\n"
       <<"  \"valid\": "<<(summary.structurallyValid()?"true":"false")<<",\n"
       <<"  \"error\": \""<<gpuDiagnosticErrorString(summary.error)<<"\"";
    if(summary.structurallyValid()) {
        out<<",\n"
           <<"  \"physicalSlots\": "<<summary.header.physicalSlots<<",\n"
           <<"  \"liveSlots\": "<<summary.liveSlots<<",\n"
           <<"  \"deadSlots\": "<<summary.deadSlots<<",\n"
           <<"  \"nonFiniteLiveSlots\": "<<summary.nonFiniteLiveSlots<<",\n"
           <<"  \"invalidAgeSlots\": "<<summary.invalidAgeSlots<<",\n"
           <<"  \"invalidCpuLifetimeSlots\": "<<summary.invalidCpuLifetimeSlots<<",\n"
           <<"  \"mismatchedLifeSlots\": "<<summary.mismatchedLifeSlots<<",\n"
           <<"  \"semanticWarnings\": "
           <<(summary.hasSemanticWarnings()?"true":"false");
        if(privacy.includeGpuIdentifiers) {
            out<<",\n"
               <<"  \"gpuFrameSerial\": "<<summary.header.gpuFrameSerial<<",\n"
               <<"  \"capturedRevision\": "<<summary.header.capturedRevision<<",\n"
               <<"  \"payloadCRC32\": "<<summary.header.payloadCrc32;
        }
        if(privacy.includeWorldBounds && summary.containsLivePositions) {
            out<<",\n"
               <<"  \"worldBoundsMin\": ["
               <<summary.minPosition[0]<<", "<<summary.minPosition[1]<<", "
               <<summary.minPosition[2]<<"],\n"
               <<"  \"worldBoundsMax\": ["
               <<summary.maxPosition[0]<<", "<<summary.maxPosition[1]<<", "
               <<summary.maxPosition[2]<<"]";
        }
    }
    out<<"\n}\n";
    return out.str();
}

} // namespace aiverse::spectra
