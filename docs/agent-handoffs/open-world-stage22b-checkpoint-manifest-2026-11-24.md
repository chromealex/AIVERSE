# W1.22b — checkpoint manifest

RegionCheckpointManifest.h adds fixed 92-byte versioned manifest with checkpoint identity, nonzero generation, checkpoint byte length and CRC32. File names derived from numeric generation, no arbitrary paths. Decode validates size/version/CRC and leaves output unchanged on failure. Codec only: atomic file publication, digest verification, generation locking and WAL identity enforcement remain outstanding. Preserve old generations until new manifest is durable. No native tests run. Next durable generation publication. PR #116 Draft; no main merge.
