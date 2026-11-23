# Stage 69 — restore checkpoint byte codec

Branch feature/spectrafx-html5-data-interfaces-20261123. Commits efe160ad764605a857e27ffeff1aeabd98c4be08, cb97330699b922c9592ad66f0ce5cb641b0d7630, 58fe33d1f275db576ac3bed76c69baec1cafa912.

New SpectraCheckpointFile.h bounded SPFR V1 codec, CRC, identity, allocator and RNG state. ComputeParticles import/export bytes, checked upload, reject nonzero source tokens. No builds/tests. Identity currently caller-supplied; no actual disk IO or Studio clock serialization. Next derive trusted graph/build identity, add GraphRuntime and Studio file IO and replay preset persistence. Not merged.
