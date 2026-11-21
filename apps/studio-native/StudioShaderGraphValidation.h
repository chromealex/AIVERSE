// Studio Shader Graph bridge: read-only allowlist.
// Source paths and arbitrary generated shader strings NEVER cross browser IPC.
#pragma once
#include <string_view>
namespace aiverse::studio {
inline bool validStudioShaderGraphName(std::string_view name) {
    if(name.empty() || name.size()>64) return false;
    for(const char c:name) {
        if(!((c>='a'&&c<='z') || (c>='A'&&c<='Z') ||
             (c>='0'&&c<='9') || c=='_' || c=='-'))
            return false;
    }
    return true;
}
} // namespace aiverse::studio
