#include "JsonCompose.h"
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

namespace aiverse {
namespace fs = std::filesystem;

namespace {
// Piece keys in JsonOrigin::where: "<top>" for a top-level value (first file for collections),
// "<top>\x1f<key>" for a member of a top-level object, "<top>\x1f#<id>" / "<top>\x1f[<n>]" for array entries.
const char kSep = '\x1f';

std::string memberPiece(const std::string& top, const std::string& key) { return top + kSep + key; }
std::string itemPiece(const std::string& top, const Json& item, size_t index) {
    if (item.isObject() && item["id"].isString()) return top + kSep + "#" + item["id"].str();
    return top + kSep + "[" + std::to_string(index) + "]";
}
std::string describe(const std::string& piece) {   // "types\x1fknight" -> "types.knight"
    std::string s = piece;
    for (auto& c : s) if (c == kSep) c = '.';
    return s;
}

struct Composer {
    const JsonReadText& read;
    JsonOrigin& origin;
    std::string rootDir;
    std::vector<std::string> stack;

    [[noreturn]] void clash(const std::string& piece, size_t a, size_t b) {
        throw JsonError(origin.files[b] + ": '" + describe(piece) + "' is already defined in " + origin.files[a]);
    }
    void claim(const std::string& piece, size_t file) {
        auto [it, fresh] = origin.where.emplace(piece, file);
        if (!fresh) clash(piece, it->second, file);
    }

    void mergeTop(Json& out, const std::string& key, const Json& v, size_t file) {
        Json* have = out.find(key);
        if (v.isObject()) {
            if (have && !have->isObject()) clash(key, origin.where[key], file);
            if (!have) { origin.where.emplace(key, file); have = &out.set(key, Json::object()); }
            for (auto& [m, x] : v.members()) { claim(memberPiece(key, m), file); have->set(m, x); }
        } else if (v.isArray()) {
            if (have && !have->isArray()) clash(key, origin.where[key], file);
            if (!have) { origin.where.emplace(key, file); have = &out.set(key, Json::array()); }
            for (auto& x : v.items()) { claim(itemPiece(key, x, have->size()), file); have->push(x); }
        } else {
            if (have) clash(key, origin.where[key], file);
            claim(key, file);
            out.set(key, v);
        }
    }

    std::string join(const std::string& from, const std::string& rel) {
        if (rel.empty() || fs::path(rel).is_absolute()) throw JsonError(from + ": include '" + rel + "' must be a relative path");
        const fs::path full = (fs::path(from).parent_path() / rel).lexically_normal();
        const fs::path inside = rootDir.empty() ? full : full.lexically_relative(fs::path(rootDir));
        if (inside.empty() || *inside.begin() == "..") throw JsonError(from + ": include '" + rel + "' points outside the game folder");
        return full.generic_string();
    }

    void add(const std::string& path, Json& out) {
        for (auto& s : stack) if (s == path) throw JsonError(path + ": included from itself (" + stack.back() + ")");
        std::string text;
        if (!read(path, text)) throw JsonError(stack.empty() ? "cannot open " + path : stack.back() + ": include '" + path + "' not found");
        Json doc;
        try { doc = Json::parse(text); } catch (const JsonError& e) { throw JsonError(path + ": " + e.what()); }
        if (!doc.isObject()) throw JsonError(path + ": must hold a JSON object");
        const size_t file = origin.files.size();
        origin.files.push_back(path);
        origin.text[file] = std::move(text);
        std::vector<std::string> includes;
        if (const Json& inc = doc["include"]; inc.isString()) includes.push_back(inc.str());
        else if (inc.isArray()) {
            for (auto& x : inc.items()) {
                if (!x.isString()) throw JsonError(path + ": include must list file paths");
                includes.push_back(x.str());
            }
        } else if (!inc.isNull()) throw JsonError(path + ": include must be a path or a list of paths");
        if (doc.has("include")) origin.includes[file] = doc["include"];
        for (auto& [k, v] : doc.members()) if (k != "include") mergeTop(out, k, v, file);
        stack.push_back(path);
        for (auto& rel : includes) add(join(path, rel), out);
        stack.pop_back();
    }
};

// Entries of a top-level collection by name: object members, or array items with a string "id".
bool isEntry(const Json& e) { return e.isObject() && (e.has("extends") || e.has("abstract")); }

Json deepMerge(const Json& base, const Json& over) {
    if (!base.isObject() || !over.isObject()) return over;
    Json out = base;
    for (auto& [k, v] : over.members()) out.set(k, deepMerge(base[k], v));
    return out;
}

struct Resolver {
    const std::string& section;
    std::map<std::string, const Json*> byName;
    std::map<std::string, Json> done;
    std::vector<std::string> path;

    const Json& resolve(const std::string& name) {
        if (auto it = done.find(name); it != done.end()) return it->second;
        for (auto& p : path)
            if (p == name) {
                std::string chain;
                for (auto& q : path) chain += "'" + q + "' -> ";
                throw JsonError(section + ": extends cycle " + chain + "'" + name + "'");
            }
        const Json& e = *byName.at(name);
        path.push_back(name);
        Json out = Json::object();
        const Json& ext = e["extends"];
        std::vector<std::string> bases;
        if (ext.isString()) bases.push_back(ext.str());
        else if (ext.isArray()) for (auto& b : ext.items()) bases.push_back(b.str());
        else if (!ext.isNull()) throw JsonError(section + " '" + name + "': extends must be a name or a list of names");
        for (auto& b : bases) {
            if (!byName.count(b)) throw JsonError(section + " '" + name + "': extends unknown '" + b + "'");
            Json base = resolve(b);
            base.erase("abstract");
            out = deepMerge(out, base);
        }
        out = deepMerge(out, e);
        out.erase("extends");
        path.pop_back();
        return done[name] = std::move(out);
    }
};
} // namespace

JsonReadText fileReader() {
    return [](const std::string& path, std::string& out) {
        std::ifstream f(path, std::ios::binary);
        if (!f) return false;
        std::ostringstream ss;
        ss << f.rdbuf();
        out = ss.str();
        return true;
    };
}

Json composeJson(const std::string& path, const JsonReadText& read, JsonOrigin* originOut) {
    JsonOrigin local;
    JsonOrigin& origin = originOut ? *originOut : local;
    origin = JsonOrigin{};
    Composer c{read, origin, fs::path(path).parent_path().lexically_normal().generic_string(), {}};
    Json out = Json::object();
    c.add(fs::path(path).lexically_normal().generic_string(), out);
    return out;
}

std::vector<std::string> composedFiles(const std::string& path, const JsonReadText& read) {
    JsonOrigin o;
    composeJson(path, read, &o);
    return o.files;
}

bool needsExtends(const Json& doc) {
    for (auto& [k, v] : doc.members()) {
        if (v.isObject()) { for (auto& [m, e] : v.members()) if (isEntry(e)) return true; }
        else if (v.isArray()) { for (auto& e : v.items()) if (isEntry(e)) return true; }
    }
    return false;
}

Json resolveExtends(const Json& doc) {
    Json out = doc;
    for (auto& [section, v] : out.membersMut()) {
        bool any = false;
        if (v.isObject()) { for (auto& [m, e] : v.members()) any = any || isEntry(e); }
        else if (v.isArray()) { for (auto& e : v.items()) any = any || isEntry(e); }
        if (!any) continue;
        Resolver r{section, {}, {}, {}};
        if (v.isObject()) {
            for (auto& [m, e] : v.members()) if (e.isObject()) r.byName[m] = &e;
            Json res = Json::object();
            for (auto& [m, e] : v.members()) {
                if (!e.isObject()) { res.set(m, e); continue; }
                Json x = r.resolve(m);
                if (x.getBool("abstract")) continue;
                x.erase("abstract");
                res.set(m, std::move(x));
            }
            v = std::move(res);
        } else {
            for (auto& e : v.items())
                if (e.isObject() && e["id"].isString()) {
                    if (r.byName.count(e["id"].str())) throw JsonError(section + ": id '" + e["id"].str() + "' is used twice");
                    r.byName[e["id"].str()] = &e;
                }
            Json res = Json::array();
            for (auto& e : v.items()) {
                if (!e.isObject() || !e["id"].isString()) {
                    if (isEntry(e)) throw JsonError(section + ": an entry with extends / abstract needs an \"id\"");
                    res.push(e);
                    continue;
                }
                Json x = r.resolve(e["id"].str());
                if (x.getBool("abstract")) continue;
                x.erase("abstract");
                res.push(std::move(x));
            }
            v = std::move(res);
        }
    }
    return out;
}

Json readComposedJson(const std::string& path, const JsonReadText& read) {
    Json j = composeJson(path, read);
    return needsExtends(j) ? resolveExtends(j) : j;
}

std::vector<std::pair<std::string, Json>> splitComposed(const Json& doc, const JsonOrigin& origin) {
    const size_t n = std::max<size_t>(1, origin.files.size());
    std::vector<Json> out(n, Json::object());
    for (auto& [file, inc] : origin.includes) if (file < n) out[file].set("include", inc);
    auto fileOf = [&](const std::string& piece, size_t fallback) {
        auto it = origin.where.find(piece);
        return it != origin.where.end() && it->second < n ? it->second : fallback;
    };
    for (auto& [k, v] : doc.members()) {
        if (k == "include") continue;
        const size_t home = fileOf(k, 0);
        if (v.isObject()) {
            size_t last = home;
            if (v.members().empty()) out[home].set(k, Json::object());
            for (auto& [m, x] : v.members()) {
                last = fileOf(memberPiece(k, m), last);
                Json* sec = out[last].find(k);
                if (!sec) sec = &out[last].set(k, Json::object());
                sec->set(m, x);
            }
        } else if (v.isArray()) {
            size_t last = home;
            if (v.items().empty()) out[home].set(k, Json::array());
            for (size_t i = 0; i < v.items().size(); ++i) {
                last = fileOf(itemPiece(k, v.items()[i], i), last);
                Json* sec = out[last].find(k);
                if (!sec) sec = &out[last].set(k, Json::array());
                sec->push(v.items()[i]);
            }
        } else {
            out[home].set(k, v);
        }
    }
    std::vector<std::pair<std::string, Json>> r;
    for (size_t i = 0; i < n; ++i) r.emplace_back(i < origin.files.size() ? origin.files[i] : std::string(), std::move(out[i]));
    return r;
}

} // namespace aiverse
