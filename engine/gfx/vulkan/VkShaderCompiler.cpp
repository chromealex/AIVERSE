#include <algorithm>
#include <cstdlib>
#include "VkShaderCompiler.h"
#ifndef ENABLE_OPT
#define ENABLE_OPT 0
#endif
#include "../ColorSpace.h"

#include <glslang/Public/ResourceLimits.h>
#include <glslang/Public/ShaderLang.h>
#include <SPIRV/GlslangToSpv.h>

#include <cctype>
#include <filesystem>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace aiverse::gfx::vk {
namespace {

bool identChar(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

// Comments become spaces (newlines kept, so compiler messages keep their line numbers).
std::string stripComments(const std::string& s) {
    std::string out = s;
    for (size_t i = 0; i + 1 < out.size(); ++i) {
        if (out[i] == '/' && out[i + 1] == '/') {
            while (i < out.size() && out[i] != '\n') out[i++] = ' ';
        } else if (out[i] == '/' && out[i + 1] == '*') {
            out[i] = out[i + 1] = ' ';
            for (i += 2; i + 1 < out.size() && !(out[i] == '*' && out[i + 1] == '/'); ++i)
                if (out[i] != '\n') out[i] = ' ';
            if (i + 1 < out.size()) out[i] = out[i + 1] = ' ';
        }
    }
    return out;
}

void replaceWord(std::string& s, const char* from, const char* to) {
    const size_t n = std::strlen(from), m = std::strlen(to);
    for (size_t p = s.find(from); p != std::string::npos; p = s.find(from, p)) {
        const bool l = p == 0 || !identChar(s[p - 1]), r = p + n >= s.size() || !identChar(s[p + n]);
        if (l && r) { s.replace(p, n, to); p += m; }
        else p += n;
    }
}

size_t skipSpace(const std::string& s, size_t p) { while (p < s.size() && std::isspace(static_cast<unsigned char>(s[p]))) ++p; return p; }
std::string identAt(const std::string& s, size_t p) { size_t e = p; while (e < s.size() && identChar(s[e])) ++e; return s.substr(p, e - p); }

// "tex7" -> 7; samplers with other names were never assigned a unit in GL (unit 0).
int slotOf(const std::string& name, const char* prefix) {
    const size_t n = std::strlen(prefix);
    if (name.size() <= n || name.compare(0, n, prefix) != 0) return -1;
    int v = 0;
    for (size_t i = n; i < name.size(); ++i) { if (!std::isdigit(static_cast<unsigned char>(name[i]))) return -1; v = v * 10 + (name[i] - '0'); }
    return v;
}

// layout(...) contents without set / binding entries.
std::string layoutWithoutBinding(const std::string& in) {
    std::string out, item;
    auto flush = [&] {
        size_t a = item.find_first_not_of(" \t\r\n"), b = item.find_last_not_of(" \t\r\n");
        std::string t = a == std::string::npos ? std::string() : item.substr(a, b - a + 1);
        if (!t.empty() && t.compare(0, 7, "binding") != 0 && t.compare(0, 3, "set") != 0) { if (!out.empty()) out += ", "; out += t; }
        item.clear();
    };
    for (char c : in) { if (c == ',') flush(); else item += c; }
    flush();
    return out;
}

// Gives every uniform block and sampler an explicit set 0 binding.
std::string bindResources(const std::string& src, uint32_t* shadowSlots) {
    std::string s = src, out;
    out.reserve(s.size() + 512);
    size_t copied = 0;
    for (size_t p = s.find("uniform"); p != std::string::npos; p = s.find("uniform", p + 7)) {
        if ((p > 0 && identChar(s[p - 1])) || (p + 7 < s.size() && identChar(s[p + 7]))) continue;
        size_t q = skipSpace(s, p + 7);
        std::string type = identAt(s, q);
        if (type == "lowp" || type == "mediump" || type == "highp") { q = skipSpace(s, q + type.size()); type = identAt(s, q); }
        if (type.empty()) continue;
        const size_t afterType = skipSpace(s, q + type.size());
        int binding = -1;
        bool block = false;
        if (type.find("sampler") != std::string::npos) {
            const int slot = slotOf(identAt(s, afterType), "tex");
            binding = kTextureBinding0 + (slot >= 0 && slot < kMaxTextureSlots ? slot : 0);
            if (shadowSlots && type.find("Shadow") != std::string::npos) *shadowSlots |= 1u << (binding - kTextureBinding0);
        } else if (afterType < s.size() && s[afterType] == '{') {
            const int slot = slotOf(type, "U");
            binding = slot >= 0 && slot < kUniformSlots ? slot : kUniformSlots - 1;   // other names were never bound in GL
            block = true;
        } else continue;   // loose uniforms / images: left to the compiler
        // an existing layout(...) right before "uniform"
        size_t start = p;
        std::string quals;
        size_t b = p;
        while (b > 0 && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
        if (b > 0 && s[b - 1] == ')') {
            size_t open = s.rfind('(', b - 1);
            size_t kw = open == std::string::npos ? std::string::npos : open;
            while (kw != std::string::npos && kw > 0 && std::isspace(static_cast<unsigned char>(s[kw - 1]))) --kw;
            if (open != std::string::npos && kw >= 6 && s.compare(kw - 6, 6, "layout") == 0 && (kw == 6 || !identChar(s[kw - 7]))) {
                quals = layoutWithoutBinding(s.substr(open + 1, b - 1 - open - 1));
                start = kw - 6;
            }
        }
        if (block && quals.find("std140") == std::string::npos && quals.find("std430") == std::string::npos)
            quals = quals.empty() ? "std140" : "std140, " + quals;
        out.append(s, copied, start - copied);
        out += "layout(" + (quals.empty() ? std::string() : quals + ", ") + "set = 0, binding = " + std::to_string(binding) + ") ";
        copied = p;
    }
    out.append(s, copied, std::string::npos);
    return out;
}

// void main() -> void aiverse_user_main(); a new main() calls it and remaps clip z.
std::string wrapVertexMain(const std::string& s) {
    for (size_t p = s.find("main"); p != std::string::npos; p = s.find("main", p + 4)) {
        if ((p > 0 && identChar(s[p - 1])) || (p + 4 < s.size() && identChar(s[p + 4]))) continue;
        size_t v = p;
        while (v > 0 && std::isspace(static_cast<unsigned char>(s[v - 1]))) --v;
        if (v < 4 || s.compare(v - 4, 4, "void") != 0) continue;
        if (skipSpace(s, p + 4) >= s.size() || s[skipSpace(s, p + 4)] != '(') continue;
        std::string out = s;
        out.replace(p, 4, "aiverse_user_main");
        out += "\nvoid main() {\n    aiverse_user_main();\n    gl_Position.z = (gl_Position.z + gl_Position.w) * 0.5;\n}\n";
        return out;
    }
    return s;
}

std::once_flag g_init;
std::mutex g_mutex;
std::unordered_map<uint64_t, std::shared_ptr<SpirvProgram>> g_cache;

uint64_t fnv(const std::string& a, const std::string& b, bool linear) {
    uint64_t h = 1469598103934665603ull;
    auto mix = [&](const std::string& s) { for (unsigned char c : s) { h ^= c; h *= 1099511628211ull; } h ^= 0xff; h *= 1099511628211ull; };
    mix(a); mix(b); h ^= linear ? 1 : 2;
    return h;
}

} // namespace

std::string scaleUByte4Inputs(const std::string& body, uint32_t locations) {
    if (!locations) return body;
    std::string s = stripComments(body), scale;
    // layout(location = L) in vec4 NAME;  ->  an UNORM raw input plus a global
    // NAME the new main() fills (x 255) before the shader's own main runs.
    for (size_t p = s.find("layout"); p != std::string::npos; p = s.find("layout", p + 6)) {
        if ((p > 0 && identChar(s[p - 1])) || identChar(s[p + 6])) continue;
        size_t q = skipSpace(s, p + 6);
        if (q >= s.size() || s[q] != '(') continue;
        const size_t close = s.find(')', q);
        if (close == std::string::npos) continue;
        std::string inside = s.substr(q + 1, close - q - 1);
        inside.erase(std::remove_if(inside.begin(), inside.end(), [](char c) { return std::isspace(static_cast<unsigned char>(c)); }), inside.end());
        if (inside.rfind("location=", 0) != 0) continue;
        const int loc = std::atoi(inside.c_str() + 9);
        if (loc < 0 || loc > 31 || !(locations & (1u << loc))) continue;
        size_t r = skipSpace(s, close + 1);
        if (identAt(s, r) != "in") continue;
        r = skipSpace(s, r + 2);
        if (identAt(s, r) != "vec4") continue;
        r = skipSpace(s, r + 4);
        const std::string name = identAt(s, r);
        size_t end = skipSpace(s, r + name.size());
        if (name.empty() || end >= s.size() || s[end] != ';') continue;
        const std::string tag = "AIVERSE_UB4_SCALE_" + std::to_string(loc);
        const std::string decl = "layout(location = " + std::to_string(loc) + ") in vec4 " + name +
            "_aiverse_unorm;\nvec4 " + name + ";\n#define " + tag + " " + name + " = " + name +
            "_aiverse_unorm * 255.0;\n";
        s.replace(p, end + 1 - p, decl);
        p += decl.size() - 6;
        if (scale.find(tag) == std::string::npos)
            scale += "#ifdef " + tag + "\n    " + tag + "\n#endif\n";
    }
    if (scale.empty()) return s;
    for (size_t p = s.find("main"); p != std::string::npos; p = s.find("main", p + 4)) {
        if ((p > 0 && identChar(s[p - 1])) || (p + 4 < s.size() && identChar(s[p + 4]))) continue;
        size_t v = p;
        while (v > 0 && std::isspace(static_cast<unsigned char>(s[v - 1]))) --v;
        if (v < 4 || s.compare(v - 4, 4, "void") != 0) continue;
        if (skipSpace(s, p + 4) >= s.size() || s[skipSpace(s, p + 4)] != '(') continue;
        s.replace(p, 4, "aiverse_ub4_main");
        s += "\nvoid main() {\n" + scale + "    aiverse_ub4_main();\n}\n";
        break;
    }
    return s;
}

std::string translateGlsl(const std::string& body, bool vertex, bool linearTarget, uint32_t* shadowSlots) {
    std::string s = stripComments(body);
    replaceWord(s, "gl_VertexID", "gl_VertexIndex");
    replaceWord(s, "gl_InstanceID", "gl_InstanceIndex");
    s = bindResources(s, shadowSlots);
    if (vertex) s = wrapVertexMain(s);
    return "#version 450\n#define AIVERSE_VULKAN 1\n" + colorShaderPrefix(false, linearTarget) + s;
}

// AIVERSE_VK_SPV_DIR=<dir> (experiment): every program's SPIR-V is written there as
// <key>.vert.spv / <key>.frag.spv; when <key>.vert.opt.spv and <key>.frag.opt.spv exist
// (e.g. made offline with spirv-opt -O) they are used instead.
void exchangeSpirv(uint64_t key, SpirvProgram& p) {
    static const char* dir = std::getenv("AIVERSE_VK_SPV_DIR");
    if (!dir || !*dir) return;
    char base[1024];
    std::snprintf(base, sizeof base, "%s/%016llx", dir, static_cast<unsigned long long>(key));
    auto read = [](const std::string& path, std::vector<uint32_t>& out) {
        std::FILE* f = std::fopen(path.c_str(), "rb");
        if (!f) return false;
        std::vector<uint32_t> words;
        uint32_t w;
        while (std::fread(&w, 4, 1, f) == 1) words.push_back(w);
        std::fclose(f);
        if (words.size() < 5 || words[0] != 0x07230203u) return false;
        out = std::move(words);
        return true;
    };
    auto write = [](const std::string& path, const std::vector<uint32_t>& words) {
        if (std::FILE* f = std::fopen(path.c_str(), "wb")) { std::fwrite(words.data(), 4, words.size(), f); std::fclose(f); }
    };
    std::vector<uint32_t> v, f;
    if (read(std::string(base) + ".vert.opt.spv", v) && read(std::string(base) + ".frag.opt.spv", f)) {
        std::fprintf(stderr, "[vk-spv] %016llx: optimized SPIR-V (%zu+%zu words, was %zu+%zu)\n",
                     static_cast<unsigned long long>(key), v.size(), f.size(), p.vertex.size(), p.fragment.size());
        p.vertex = std::move(v);
        p.fragment = std::move(f);
        return;
    }
    write(std::string(base) + ".vert.spv", p.vertex);
    write(std::string(base) + ".frag.spv", p.fragment);
}

// ---- on-disk program cache ----
constexpr uint32_t kSpirvCacheVersion = 1;
std::string g_cacheDir;
void setShaderCacheDir(const std::string& dir) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_cacheDir = dir;
    if (!dir.empty()) { std::error_code ec; std::filesystem::create_directories(dir, ec); }
}
namespace {
std::string cachePath(uint64_t key, bool optimized) {
    if (g_cacheDir.empty()) return {};
    char name[64];
    std::snprintf(name, sizeof name, "/%016llx-%u%s.spvc", static_cast<unsigned long long>(key), kSpirvCacheVersion, optimized ? "o" : "");
    return g_cacheDir + name;
}
// Layout: magic, vertex words, fragment words, shadowSlots, then the words.
bool loadCached(const std::string& path, SpirvProgram& p) {
    if (path.empty()) return false;
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    uint32_t h[4] = {};
    bool ok = std::fread(h, 4, 4, f) == 4 && h[0] == 0x41565350u && h[1] > 4 && h[2] > 4 && h[1] < (1u << 24) && h[2] < (1u << 24);
    if (ok) {
        p.vertex.resize(h[1]); p.fragment.resize(h[2]); p.shadowSlots = h[3];
        ok = std::fread(p.vertex.data(), 4, h[1], f) == h[1] && std::fread(p.fragment.data(), 4, h[2], f) == h[2] &&
             p.vertex[0] == 0x07230203u && p.fragment[0] == 0x07230203u;
    }
    std::fclose(f);
    if (!ok) { p = SpirvProgram{}; }
    return ok;
}
void storeCached(const std::string& path, const SpirvProgram& p) {
    if (path.empty() || !p.error.empty()) return;
    const std::string tmp = path + ".tmp";
    std::FILE* f = std::fopen(tmp.c_str(), "wb");
    if (!f) return;
    const uint32_t h[4] = {0x41565350u, uint32_t(p.vertex.size()), uint32_t(p.fragment.size()), p.shadowSlots};
    bool ok = std::fwrite(h, 4, 4, f) == 4 && std::fwrite(p.vertex.data(), 4, p.vertex.size(), f) == p.vertex.size() &&
              std::fwrite(p.fragment.data(), 4, p.fragment.size(), f) == p.fragment.size();
    ok = std::fclose(f) == 0 && ok;
    std::error_code ec;
    if (ok) std::filesystem::rename(tmp, path, ec);
    if (!ok || ec) std::filesystem::remove(tmp, ec);
}
}

bool compileProgram(const std::string& vertexBody, const std::string& fragmentBody, bool linearTarget, SpirvProgram& out) {
    const uint64_t key = fnv(vertexBody, fragmentBody, linearTarget);
    std::lock_guard<std::mutex> lock(g_mutex);
    if (auto it = g_cache.find(key); it != g_cache.end()) { out = *it->second; return out.error.empty(); }
    static const bool optimize = [] { const char* e = std::getenv("AIVERSE_VK_SPV_OPT"); return ENABLE_OPT && !(e && e[0] == '0'); }();
    const std::string cached = std::getenv("AIVERSE_VK_SPV_DIR") ? std::string() : cachePath(key, optimize);
    if (SpirvProgram hit; loadCached(cached, hit)) {
        g_cache.emplace(key, std::make_shared<SpirvProgram>(hit));
        out = std::move(hit);
        return true;
    }
    std::call_once(g_init, [] { glslang::InitializeProcess(); });

    uint32_t shadow = 0;
    const std::string vs = translateGlsl(vertexBody, true, linearTarget, &shadow), fs = translateGlsl(fragmentBody, false, linearTarget, &shadow);
    const EShMessages msgs = EShMessages(EShMsgSpvRules | EShMsgVulkanRules);
    glslang::TShader vsh(EShLangVertex), fsh(EShLangFragment);
    auto setup = [&](glslang::TShader& sh, const std::string& src, EShLanguage stage) {
        const char* p = src.c_str();
        sh.setStrings(&p, 1);
        sh.setEnvInput(glslang::EShSourceGlsl, stage, glslang::EShClientVulkan, 100);
        sh.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_1);
        sh.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_3);
        sh.setAutoMapLocations(true);
        return sh.parse(GetDefaultResources(), 450, false, msgs);
    };
    SpirvProgram result;
    result.shadowSlots = shadow;
    if (!setup(vsh, vs, EShLangVertex)) result.error = std::string("vertex shader: ") + vsh.getInfoLog();
    else if (!setup(fsh, fs, EShLangFragment)) result.error = std::string("fragment shader: ") + fsh.getInfoLog();
    else {
        glslang::TProgram program;
        program.addShader(&vsh);
        program.addShader(&fsh);
        if (!program.link(msgs)) result.error = std::string("link: ") + program.getInfoLog();
        else {
            // varyings get matching locations by name across the two stages (the default mapper numbers each stage alone)
            std::unique_ptr<glslang::TIoMapper> mapper(glslang::GetGlslIoMapper());
            std::unique_ptr<glslang::TIoMapResolver> resolver(program.getGlslIoResolver(EShLangVertex));
            if (!program.mapIO(resolver.get(), mapper.get())) result.error = std::string("io mapping: ") + program.getInfoLog();
        }
        if (result.error.empty()) {
            glslang::SpvOptions opt;
            // SPIRV-Tools performance passes when built in (ENABLE_OPT, AIVERSE_SPIRV_OPT);
            // AIVERSE_VK_SPV_OPT=0 turns them off at run time for comparisons.
            opt.disableOptimizer = !optimize;
            opt.optimizePerformance = !opt.disableOptimizer;
            opt.validate = false;
            glslang::GlslangToSpv(*program.getIntermediate(EShLangVertex), result.vertex, &opt);
            glslang::GlslangToSpv(*program.getIntermediate(EShLangFragment), result.fragment, &opt);
            exchangeSpirv(key, result);
        }
    }
    storeCached(cached, result);
    g_cache.emplace(key, std::make_shared<SpirvProgram>(result));
    out = std::move(result);
    return out.error.empty();
}

bool compileComputeProgram(const std::string& glslBody,
                           std::vector<uint32_t>& spirv,std::string& error) {
    spirv.clear();error.clear();
    if (glslBody.empty() || glslBody.size() > (size_t(1)<<20)) {
        error="compute shader source empty or exceeds 1MiB limit";
        return false;
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    std::call_once(g_init, [] { glslang::InitializeProcess(); });
    std::string source="#version 450\n#define AIVERSE_VULKAN 1\n";
    if (glslBody.find("local_size_x") == std::string::npos)
        source+="layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;\n";
    source+=glslBody;
    glslang::TShader shader(EShLangCompute);
    const char* ptr=source.c_str();
    shader.setStrings(&ptr,1);
    shader.setEnvInput(glslang::EShSourceGlsl,EShLangCompute,
                       glslang::EShClientVulkan,100);
    shader.setEnvClient(glslang::EShClientVulkan,glslang::EShTargetVulkan_1_1);
    shader.setEnvTarget(glslang::EShTargetSpv,glslang::EShTargetSpv_1_3);
    const EShMessages messages=EShMessages(EShMsgSpvRules|EShMsgVulkanRules);
    if (!shader.parse(GetDefaultResources(),450,false,messages)) {
        error=std::string("compute GLSL: ")+shader.getInfoLog();
        return false;
    }
    glslang::TProgram program;
    program.addShader(&shader);
    if (!program.link(messages) ||
        !program.getIntermediate(EShLangCompute)) {
        error=std::string("compute link: ")+program.getInfoLog();
        return false;
    }
    glslang::SpvOptions options;
    options.disableOptimizer=true;
    options.validate=false;
    glslang::GlslangToSpv(*program.getIntermediate(EShLangCompute),spirv,&options);
    if (spirv.empty()) {
        error="compute SPIR-V compilation returned no words";
        return false;
    }
    return true;
}

bool compileComputeProgram(const std::string& source,
                           std::vector<uint32_t>& spirv,
                           std::string* error) {
    if (error) error->clear();
    if (source.empty() || source.size()>256u*1024u) {
        if (error) *error="Vulkan compute: empty or oversized GLSL";
        return false;
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    std::call_once(g_init, [] { glslang::InitializeProcess(); });
    const char* raw=source.c_str();
    glslang::TShader shader(EShLangCompute);
    shader.setStrings(&raw,1);
    shader.setEnvInput(glslang::EShSourceGlsl, EShLangCompute,
                       glslang::EShClientVulkan, 100);
    shader.setEnvClient(glslang::EShClientVulkan,
                        glslang::EShTargetVulkan_1_1);
    shader.setEnvTarget(glslang::EShTargetSpv,
                        glslang::EShTargetSpv_1_3);
    const auto messages=EShMessages(EShMsgSpvRules|EShMsgVulkanRules);
    if (!shader.parse(GetDefaultResources(),450,false,messages)) {
        if (error) *error=std::string("Vulkan compute: ")+shader.getInfoLog();
        return false;
    }
    glslang::TProgram program;
    program.addShader(&shader);
    if (!program.link(messages) || !program.getIntermediate(EShLangCompute)) {
        if (error) *error=std::string("Vulkan compute link: ")+program.getInfoLog();
        return false;
    }
    std::vector<uint32_t> compiled;
    glslang::SpvOptions options;
    options.disableOptimizer=true;
    options.validate=false;
    glslang::GlslangToSpv(*program.getIntermediate(EShLangCompute),
                         compiled,&options);
    if (compiled.empty()) {
        if (error) *error="Vulkan compute: compiler returned empty SPIR-V";
        return false;
    }
    spirv=std::move(compiled);
    return true;
}


} // namespace aiverse::gfx::vk
