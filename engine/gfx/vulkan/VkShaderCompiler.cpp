#include "VkShaderCompiler.h"
#include "../ColorSpace.h"

#include <glslang/Public/ResourceLimits.h>
#include <glslang/Public/ShaderLang.h>
#include <SPIRV/GlslangToSpv.h>

#include <cctype>
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

std::string translateGlsl(const std::string& body, bool vertex, bool linearTarget, uint32_t* shadowSlots) {
    std::string s = stripComments(body);
    replaceWord(s, "gl_VertexID", "gl_VertexIndex");
    replaceWord(s, "gl_InstanceID", "gl_InstanceIndex");
    s = bindResources(s, shadowSlots);
    if (vertex) s = wrapVertexMain(s);
    return "#version 450\n#define AIVERSE_VULKAN 1\n" + colorShaderPrefix(false, linearTarget) + s;
}

bool compileProgram(const std::string& vertexBody, const std::string& fragmentBody, bool linearTarget, SpirvProgram& out) {
    const uint64_t key = fnv(vertexBody, fragmentBody, linearTarget);
    std::lock_guard<std::mutex> lock(g_mutex);
    if (auto it = g_cache.find(key); it != g_cache.end()) { out = *it->second; return out.error.empty(); }
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
            opt.disableOptimizer = true;
            opt.validate = false;
            glslang::GlslangToSpv(*program.getIntermediate(EShLangVertex), result.vertex, &opt);
            glslang::GlslangToSpv(*program.getIntermediate(EShLangFragment), result.fragment, &opt);
        }
    }
    g_cache.emplace(key, std::make_shared<SpirvProgram>(result));
    out = std::move(result);
    return out.error.empty();
}

bool compileComputeProgram(const std::string& glsl, std::vector<uint32_t>& out,
                           std::string& error) {
    out.clear();
    error.clear();
    if (glsl.empty()) { error = "empty Vulkan compute shader"; return false; }
    std::lock_guard<std::mutex> lock(g_mutex);
    std::call_once(g_init, [] { glslang::InitializeProcess(); });
    const EShMessages messages = EShMessages(EShMsgSpvRules | EShMsgVulkanRules);
    glslang::TShader shader(EShLangCompute);
    const char* source = glsl.c_str();
    shader.setStrings(&source, 1);
    shader.setEnvInput(glslang::EShSourceGlsl, EShLangCompute,
                       glslang::EShClientVulkan, 100);
    shader.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_1);
    shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_3);
    if (!shader.parse(GetDefaultResources(), 450, false, messages)) {
        error = std::string("compute shader: ") + shader.getInfoLog();
        return false;
    }
    glslang::TProgram program;
    program.addShader(&shader);
    if (!program.link(messages)) {
        error = std::string("compute link: ") + program.getInfoLog();
        return false;
    }
    glslang::SpvOptions options;
    options.disableOptimizer = true;
    options.validate = false;
    glslang::GlslangToSpv(*program.getIntermediate(EShLangCompute), out, &options);
    if (out.empty()) { error = "empty SPIR-V compute result"; return false; }
    return true;
}

} // namespace aiverse::gfx::vk
