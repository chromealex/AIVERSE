#pragma once
#include <functional>
#include <string>

namespace aiverse::platform {
// Independent project reader for worker jobs: no global root, hosted view, CWD or pack mutation.
// Files and local content.rpak (+ local split chunks) share the same checked content path.
using ContentReader=std::function<bool(const std::string&,std::string&)>;
ContentReader makeContentReader(const std::string& projectRoot,size_t maxFileBytes=32*1024*1024);
}
