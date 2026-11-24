#pragma once
#include "RegionWalFrame.h"
#include <istream>
#include <limits>
namespace aiverse::world {
enum class RegionWalScan { End, Incomplete, Corrupt, IoError, SequenceError, Rejected };
struct RegionWalScanResult {
    RegionWalScan status=RegionWalScan::End;
    uint64_t validBytes=0, lastSequence=0, frames=0;
};
class RegionWalReader {
public:
    // Input begins at a frame boundary. Offsets are relative to that boundary.
    // Consumer returns true only after accepting the complete frame.
    template<class Consumer>
    static RegionWalScanResult scan(std::istream& in,uint64_t previous,Consumer&& accept) {
        RegionWalScanResult r; r.lastSequence=previous;
        std::vector<uint8_t> bytes(RegionWalCodec::headerSize);
        for(;;) {
            bytes.resize(RegionWalCodec::headerSize);
            in.read(reinterpret_cast<char*>(bytes.data()),bytes.size());
            const auto got=in.gcount();
            if(in.bad() || (in.fail()&&!in.eof())) {r.status=RegionWalScan::IoError;return r;}
            if(got==0 && in.eof())return r;
            if(got!=static_cast<std::streamsize>(bytes.size())) {r.status=RegionWalScan::Incomplete;return r;}
            RegionWalFrame frame;size_t consumed=0;
            auto state=RegionWalCodec::decode(bytes,frame,consumed);
            if(state==RegionWalRead::Corrupt){r.status=RegionWalScan::Corrupt;return r;}
            uint32_t n=0;
            for(size_t i=0;i<4;++i)n|=uint32_t(bytes[16+i])<<(8*i);
            if(n>RegionWalCodec::maxPayload){r.status=RegionWalScan::Corrupt;return r;}
            if(n) {
                bytes.resize(RegionWalCodec::headerSize+n);
                in.read(reinterpret_cast<char*>(bytes.data()+RegionWalCodec::headerSize),n);
                if(in.bad() || (in.fail()&&!in.eof())){r.status=RegionWalScan::IoError;return r;}
                if(in.gcount()!=static_cast<std::streamsize>(n)){r.status=RegionWalScan::Incomplete;return r;}
                state=RegionWalCodec::decode(bytes,frame,consumed);
            }
            if(state!=RegionWalRead::Complete){r.status=RegionWalScan::Corrupt;return r;}
            if(r.lastSequence==std::numeric_limits<uint64_t>::max() || frame.sequence!=r.lastSequence+1 ||
               consumed>std::numeric_limits<uint64_t>::max()-r.validBytes) {
                r.status=RegionWalScan::SequenceError;return r;
            }
            if(!accept(static_cast<const RegionWalFrame&>(frame))){r.status=RegionWalScan::Rejected;return r;}
            r.validBytes+=consumed;r.lastSequence=frame.sequence;++r.frames;
        }
    }
};
}
