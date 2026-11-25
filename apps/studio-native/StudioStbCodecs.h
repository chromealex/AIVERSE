// Studio-private stb codecs. Include in exactly one translation unit per
// executable: these macros emit internal-linkage implementations, not just
// declarations. Other engine modules also embed stb and must not share an
// exported global stb implementation with Studio.
//
// Mark the optional public stb entry points as intentionally unused in this
// TU. Native first-party warnings stay enabled (no -w/-Wno-* or global
// diagnostic pragmas), while every callable codec stays available.
#pragma once
#include <cstdio>

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#define STBIDEF [[maybe_unused]] static
#include "third_party/stb/stb_image.h"
#undef STBIDEF
#undef STB_IMAGE_STATIC
#undef STB_IMAGE_IMPLEMENTATION

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC
#define STBIWDEF [[maybe_unused]] static
#include "third_party/stb_image_write/stb_image_write.h"
#undef STBIWDEF
#undef STB_IMAGE_WRITE_STATIC
#undef STB_IMAGE_WRITE_IMPLEMENTATION
