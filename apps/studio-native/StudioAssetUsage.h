// Where content assets are named: a text scan of the project's data files (rules, maps, materials,
// scripts, UI, glTF). "Unused" means no file names the asset (scripts that build paths at runtime
// are not followed), so the UI calls it "not referenced by name".
#pragma once
#include "engine/data/Json.h"
#include <filesystem>
#include <cctype>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <vector>

namespace aiverse::studio {
struct StudioTextFile { std::string path; std::string text; };   // project-relative path

inline std::vector<StudioTextFile> studioProjectTextFiles(const std::filesystem::path& project, size_t maxFiles = 6000) {
    namespace fs = std::filesystem;
    std::vector<StudioTextFile> out;
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(project, fs::directory_options::skip_permission_denied, ec);
         !ec && it != fs::recursive_directory_iterator() && out.size() < maxFiles; it.increment(ec)) {
        const std::string name = it->path().filename().string();
        if (it->is_symlink(ec)) { if (it->is_directory(ec)) it.disable_recursion_pending(); continue; }
        if (it->is_directory(ec)) {
            if (name == "Builds" || name == "Logs" || name == "logs" || name == "dist" || name == ".aiverse" ||
                name == ".git" || name == "node_modules" || name == ".aiverse-cache")
                it.disable_recursion_pending();
            continue;
        }
        std::string ext = it->path().extension().string();
        for (auto& c : ext) c = char(std::tolower(static_cast<unsigned char>(c)));
        if (ext != ".json" && ext != ".lua" && ext != ".rml" && ext != ".rcss" && ext != ".gltf" && ext != ".aiverse") continue;
        if (it->file_size(ec) > 32ull * 1024 * 1024) continue;
        std::ifstream in(it->path(), std::ios::binary);
        StudioTextFile f;
        f.path = fs::relative(it->path(), project, ec).generic_string();
        f.text.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        out.push_back(std::move(f));
    }
    return out;
}

// Recovery diagnostics must not silently treat truncated or skipped files as
// evidence of no dependencies. This scanner is advisory, not a deletion gate.
struct StudioRecoveryTextScan {
    size_t visited=0, scanned=0, skipped=0;
    bool truncated=false;
    Json mentions=Json::array();
};
inline StudioRecoveryTextScan studioRecoveryTextScan(const std::filesystem::path& project,
    const std::vector<std::string>& needles){
    namespace fs=std::filesystem;
    StudioRecoveryTextScan out;
    constexpr size_t maxVisits=40000,maxFiles=6000,maxBytes=128*1024*1024,maxFile=8*1024*1024;
    size_t total=0;
    std::error_code ec;
    fs::recursive_directory_iterator it(project,ec),end;
    if(ec){out.truncated=true;return out;}
    for(;it!=end;it.increment(ec)){
        if(ec){out.truncated=true;break;}
        if(++out.visited>maxVisits||out.scanned>=maxFiles){out.truncated=true;break;}
        const auto stat=it->symlink_status(ec);
        if(ec){out.truncated=true;break;}
        if(fs::is_symlink(stat)){it.disable_recursion_pending();++out.skipped;continue;}
        const auto name=it->path().filename().string();
        if(fs::is_directory(stat)){
            if(name==".aiverse-cache"||name==".git"||name=="node_modules"||name=="Builds"||name=="dist"||name=="Logs"||name=="logs"){
                it.disable_recursion_pending();++out.skipped;
            }
            continue;
        }
        if(!fs::is_regular_file(stat)){++out.skipped;continue;}
        auto ext=it->path().extension().string();
        for(char& ch:ext)ch=char(std::tolower(static_cast<unsigned char>(ch)));
        static const std::set<std::string> supported={".json",".lua",".rml",".rcss",".gltf",".aiverse",".cpp",".cc",".cxx",".c",".h",".hpp",".cs",".ts",".js",".hlsl",".glsl",".metal",".xml",".yaml",".yml",".toml"};
        if(!supported.count(ext)){++out.skipped;continue;}
        const auto bytes=it->file_size(ec);
        if(ec){out.truncated=true;break;}
        if(bytes>maxFile||bytes>maxBytes-total){++out.skipped;out.truncated=true;continue;}
        std::ifstream stream(it->path(),std::ios::binary);
        if(!stream){++out.skipped;out.truncated=true;continue;}
        std::string contents(static_cast<size_t>(bytes),'\0');
        if(bytes&&!stream.read(contents.data(),static_cast<std::streamsize>(bytes))){++out.skipped;out.truncated=true;continue;}
        if(stream.peek()!=std::char_traits<char>::eof()||stream.bad()){++out.skipped;out.truncated=true;continue;}
        ++out.scanned;total+=static_cast<size_t>(bytes);
        for(const auto& needle:needles){
            if(needle.empty()||contents.find(needle)==std::string::npos)continue;
            if(out.mentions.size()>=200){out.truncated=true;break;}
            Json row=Json::object();row.set("file",Json::string(it->path().lexically_relative(project).generic_string()));
            row.set("reference",Json::string(needle));out.mentions.push(std::move(row));
        }
    }
    if(ec)out.truncated=true;
    return out;
}

// Every "content/..." name in the texts, plus files a .gltf names by relative uri.
inline std::set<std::string> studioNamedContent(const std::vector<StudioTextFile>& files) {
    std::set<std::string> named;
    auto stop = [](char c) { return c == '"' || c == '\'' || c == '`' || c == ' ' || c == '\n' || c == '\r' || c == '\t' || c == ')' || c == ',' || c == '\\'; };
    for (const auto& f : files) {
        for (size_t at = f.text.find("content/"); at != std::string::npos; at = f.text.find("content/", at + 8)) {
            if (at > 0 && (std::isalnum(static_cast<unsigned char>(f.text[at - 1])) || f.text[at - 1] == '_')) continue;
            size_t end = at + 8;
            while (end < f.text.size() && !stop(f.text[end]) && end - at < 600) ++end;
            named.insert(f.text.substr(at + 8, end - at - 8));
        }
        if (f.path.size() > 5 && f.path.compare(f.path.size() - 5, 5, ".gltf") == 0 && f.path.rfind("content/", 0) == 0) {
            const std::string folder = f.path.substr(8, f.path.rfind('/') == std::string::npos ? 0 : f.path.rfind('/') + 1 - 8);
            for (size_t at = f.text.find("\"uri\""); at != std::string::npos; at = f.text.find("\"uri\"", at + 5)) {
                const size_t q1 = f.text.find('"', f.text.find(':', at) + 1);
                if (q1 == std::string::npos) break;
                const size_t q2 = f.text.find('"', q1 + 1);
                if (q2 == std::string::npos) break;
                const std::string uri = f.text.substr(q1 + 1, q2 - q1 - 1);
                if (uri.rfind("data:", 0) != 0 && uri.find("..") == std::string::npos) named.insert(folder + uri);
            }
        }
    }
    return named;
}

// Lines that name one asset (content-relative path), at most `limit`.
inline Json studioAssetMentions(const std::vector<StudioTextFile>& files, const std::string& contentPath, size_t limit = 200) {
    Json out = Json::array();
    const std::string needle = "content/" + contentPath;
    for (const auto& f : files) {
        size_t line = 1, lineStart = 0, scanned = 0;
        for (size_t at = f.text.find(needle); at != std::string::npos && out.size() < limit; at = f.text.find(needle, at + needle.size())) {
            for (; scanned < at; ++scanned) if (f.text[scanned] == '\n') { ++line; lineStart = scanned + 1; }
            size_t lineEnd = f.text.find('\n', at);
            if (lineEnd == std::string::npos) lineEnd = f.text.size();
            std::string snippet = f.text.substr(lineStart, std::min<size_t>(lineEnd - lineStart, 220));
            const size_t first = snippet.find_first_not_of(" \t");
            Json row = Json::object();
            row.set("file", Json::string(f.path));
            row.set("line", Json::number(int64_t(line)));
            row.set("text", Json::string(first == std::string::npos ? std::string() : snippet.substr(first)));
            out.push(std::move(row));
        }
    }
    return out;
}
} // namespace aiverse::studio
