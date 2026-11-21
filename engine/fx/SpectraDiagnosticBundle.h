// SpectraFX Stage 43 — privacy-safe offline diagnostic ZIP (STORE method).
// No zlib, platform Device, ECS, filesystem or raw GPU source buffers.
// Only fixed aggregate JSON structures are admitted; no arbitrary filenames,
// original file paths, source VM values or particle positions in the bundle.
#pragma once

#include "SpectraDiagnosticReport.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <locale>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace aiverse::spectra {

struct GpuSafeTimelineFrame {
    uint32_t sample=0;
    uint32_t estimatedParticles=0;
    uint32_t freshGroups=0,staleGroups=0,pendingGroups=0;
    uint32_t killsAwaitingCompletion=0;
    bool stepAccepted=false,deviceHealthy=false;
};

inline constexpr size_t kGpuSafeBundleMaxFrames=180;
inline constexpr size_t kGpuSafeBundleMaxBytes=128u*1024u;

namespace diagnostic_zip {

inline void push16(std::vector<uint8_t>& b,uint16_t v) {
    b.push_back(uint8_t(v));b.push_back(uint8_t(v>>8));
}
inline void push32(std::vector<uint8_t>& b,uint32_t v) {
    push16(b,uint16_t(v));push16(b,uint16_t(v>>16));
}
struct Entry {std::string_view name;std::string body;};
inline bool append(const Entry& entry,std::vector<uint8_t>& dst,
                   std::vector<uint8_t>& directory) {
    if(entry.name.empty() || entry.name.size()>255 ||
       entry.body.size()>kGpuSafeBundleMaxBytes ||
       dst.size()>kGpuSafeBundleMaxBytes ||
       dst.size()>std::numeric_limits<uint32_t>::max() ||
       entry.body.size()>std::numeric_limits<uint32_t>::max())
        return false;
    // All names are compile-time fixed, ASCII, without separators.
    const uint32_t offset=uint32_t(dst.size());
    const uint32_t length=uint32_t(entry.body.size());
    const uint32_t crc=gpuCheckpointPayloadCrc32(
        reinterpret_cast<const uint8_t*>(entry.body.data()),length);
    push32(dst,0x04034b50u);
    push16(dst,20);push16(dst,0);push16(dst,0); // version, flags, STORE
    push16(dst,0);push16(dst,0); // deterministic DOS time/date
    push32(dst,crc);push32(dst,length);push32(dst,length);
    push16(dst,uint16_t(entry.name.size()));push16(dst,0);
    dst.insert(dst.end(),entry.name.begin(),entry.name.end());
    dst.insert(dst.end(),entry.body.begin(),entry.body.end());

    push32(directory,0x02014b50u);
    push16(directory,20);push16(directory,20);
    push16(directory,0);push16(directory,0); // flags, STORE
    push16(directory,0);push16(directory,0);
    push32(directory,crc);push32(directory,length);push32(directory,length);
    push16(directory,uint16_t(entry.name.size()));push16(directory,0);
    push16(directory,0);push16(directory,0); // extra/comment length, disk
    push16(directory,0);push32(directory,0); // internal/external attr
    push32(directory,offset);
    directory.insert(directory.end(),entry.name.begin(),entry.name.end());
    return dst.size()+directory.size()+22<=kGpuSafeBundleMaxBytes;
}
} // namespace diagnostic_zip

// Fail-closed: invalid checkpoint, excessive frames/bytes -> false, output
// unchanged. No opt-in to RAW .spfx here: this API can *only* create a
// privacy-redacted support bundle, not a sensitive archive by accident.
inline bool makeSafeGpuDiagnosticZip(
    const GpuDiagnosticInspection& inspected,
    const std::vector<GpuSafeTimelineFrame>& frames,
    std::vector<uint8_t>& output) {
    if(!inspected.structurallyValid() ||
       frames.size()>kGpuSafeBundleMaxFrames)return false;

    const std::string summary=gpuDiagnosticReportJson(
        inspected,GpuDiagnosticReportPrivacy{});
    std::ostringstream log;
    log.imbue(std::locale::classic());
    log<<"{\n  \"kind\": \"spectrafx_redacted_preview_timeline\",\n"
       <<"  \"frames\": [\n";
    for(size_t i=0;i<frames.size();++i) {
        const auto& f=frames[i];
        log<<"    {\"sample\":"<<f.sample
           <<",\"estimatedParticles\":"<<f.estimatedParticles
           <<",\"freshGroups\":"<<f.freshGroups
           <<",\"staleGroups\":"<<f.staleGroups
           <<",\"pendingGroups\":"<<f.pendingGroups
           <<",\"killsAwaitingCompletion\":"<<f.killsAwaitingCompletion
           <<",\"stepAccepted\":"<<(f.stepAccepted?"true":"false")
           <<",\"deviceHealthy\":"<<(f.deviceHealthy?"true":"false")
           <<"}"<<(i+1<frames.size()?",":"")<<"\n";
    }
    log<<"  ]\n}\n";
    const std::string timeline=log.str();
    // Fixed, literal, audited manifest. No original source path or host
    // name is included; raw GPU captures are NEVER automatically packaged.
    const std::string manifest=
        "{\n"
        "  \"kind\": \"spectrafx_support_bundle\",\n"
        "  \"bundleVersion\": 1,\n"
        "  \"sourceFormat\": \"SPFX-V2\",\n"
        "  \"redacted\": true,\n"
        "  \"containsRawCheckpoint\": false,\n"
        "  \"containsWorldCoordinates\": false,\n"
        "  \"containsVmScalars\": false,\n"
        "  \"containsGpuIdentifiers\": false,\n"
        "  \"uploadedAutomatically\": false,\n"
        "  \"entries\": [\"manifest.json\",\"checkpoint_summary.json\","
        "\"preview_timeline.json\"]\n"
        "}\n";

    const diagnostic_zip::Entry entries[]{
        {"manifest.json",manifest},
        {"checkpoint_summary.json",summary},
        {"preview_timeline.json",timeline}
    };
    std::vector<uint8_t> data,directory;
    data.reserve(std::min<size_t>(
        kGpuSafeBundleMaxBytes,manifest.size()+summary.size()+
        timeline.size()+512u));
    for(const auto& entry:entries)
        if(!diagnostic_zip::append(entry,data,directory))return false;
    if(data.size()+directory.size()+22>kGpuSafeBundleMaxBytes)return false;
    const uint32_t centralOffset=uint32_t(data.size());
    const uint32_t centralSize=uint32_t(directory.size());
    data.insert(data.end(),directory.begin(),directory.end());
    diagnostic_zip::push32(data,0x06054b50u); // end of central directory
    diagnostic_zip::push16(data,0);diagnostic_zip::push16(data,0);
    diagnostic_zip::push16(data,3);diagnostic_zip::push16(data,3);
    diagnostic_zip::push32(data,centralSize);
    diagnostic_zip::push32(data,centralOffset);
    diagnostic_zip::push16(data,0);
    output.swap(data);
    return true;
}
} // namespace aiverse::spectra
