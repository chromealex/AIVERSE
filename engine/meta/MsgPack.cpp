#include "MsgPack.h"
#include "Encoding.h"

#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace aiverse::meta::msgpack {

// ---------------------------------------------------------------- Value

Value Value::boolean(bool b) { Value v; v.type_ = Type::Bool; v.b_ = b; return v; }
Value Value::integer(int64_t x) {
    if (x >= 0) return uinteger(uint64_t(x));
    Value v; v.type_ = Type::Int; v.i_ = x; return v;
}
Value Value::uinteger(uint64_t x) { Value v; v.type_ = Type::UInt; v.u_ = x; return v; }
Value Value::float32(float x) { Value v; v.type_ = Type::Float32; v.d_ = double(x); return v; }
Value Value::float64(double x) { Value v; v.type_ = Type::Float64; v.d_ = x; return v; }
Value Value::str(std::string s) { Value v; v.type_ = Type::Str; v.s_ = std::move(s); return v; }
Value Value::bin(Bytes b) { Value v; v.type_ = Type::Bin; v.bin_ = std::move(b); return v; }
Value Value::array(std::vector<Value> items) { Value v; v.type_ = Type::Array; v.items_ = std::move(items); return v; }
Value Value::map() { Value v; v.type_ = Type::Map; return v; }
Value Value::ext(int8_t type, Bytes data) { Value v; v.type_ = Type::Ext; v.ext_ = type; v.bin_ = std::move(data); return v; }

int64_t Value::asInt(int64_t def) const {
    if (type_ == Type::Int) return i_;
    if (type_ == Type::UInt && u_ <= uint64_t(INT64_MAX)) return int64_t(u_);
    return def;
}
uint64_t Value::asUInt(uint64_t def) const {
    if (type_ == Type::UInt) return u_;
    if (type_ == Type::Int && i_ >= 0) return uint64_t(i_);
    return def;
}
double Value::asDouble(double def) const {
    switch (type_) {
    case Type::Int: return double(i_);
    case Type::UInt: return double(u_);
    case Type::Float32: case Type::Float64: return d_;
    default: return def;
    }
}
const Value& Value::operator[](size_t i) const {
    static const Value kNil;
    return type_ == Type::Array && i < items_.size() ? items_[i] : kNil;
}
const Value* Value::find(std::string_view k) const {
    if (type_ != Type::Map) return nullptr;
    for (size_t i = 0; i + 1 < items_.size(); i += 2)
        if (items_[i].type_ == Type::Str && items_[i].s_ == k) return &items_[i + 1];
    return nullptr;
}
Value& Value::push(Value v) {
    if (type_ != Type::Array) *this = array();
    items_.push_back(std::move(v));
    return items_.back();
}
Value& Value::set(Value key, Value v) {
    if (type_ != Type::Map) *this = map();
    for (size_t i = 0; i + 1 < items_.size(); i += 2)
        if (items_[i] == key) { items_[i + 1] = std::move(v); return items_[i + 1]; }
    items_.push_back(std::move(key));
    items_.push_back(std::move(v));
    return items_.back();
}
bool Value::operator==(const Value& o) const {
    if (isInteger() && o.isInteger()) {
        if ((type_ == Type::Int) != (o.type_ == Type::Int)) return false;   // a negative never equals an unsigned
        return type_ == Type::Int ? i_ == o.i_ : u_ == o.u_;
    }
    if (type_ != o.type_) return false;
    switch (type_) {
    case Type::Nil: return true;
    case Type::Bool: return b_ == o.b_;
    case Type::Float32: case Type::Float64: return d_ == o.d_ || (std::isnan(d_) && std::isnan(o.d_));
    case Type::Str: return s_ == o.s_;
    case Type::Bin: return bin_ == o.bin_;
    case Type::Ext: return ext_ == o.ext_ && bin_ == o.bin_;
    case Type::Array: case Type::Map: return items_ == o.items_;
    default: return false;
    }
}

// ---------------------------------------------------------------- Writer

void Writer::be(uint64_t v, int bytes) {
    for (int i = bytes - 1; i >= 0; --i) out_.push_back(uint8_t(v >> (8 * i)));
}
Writer& Writer::nil() { out_.push_back(0xc0); return *this; }
Writer& Writer::boolean(bool b) { out_.push_back(b ? 0xc3 : 0xc2); return *this; }
Writer& Writer::integer(int64_t v) {
    if (v >= 0) return uinteger(uint64_t(v));
    if (v >= -32) out_.push_back(uint8_t(int8_t(v)));
    else if (v >= INT8_MIN) { out_.push_back(0xd0); be(uint64_t(v), 1); }
    else if (v >= INT16_MIN) { out_.push_back(0xd1); be(uint64_t(v), 2); }
    else if (v >= INT32_MIN) { out_.push_back(0xd2); be(uint64_t(v), 4); }
    else { out_.push_back(0xd3); be(uint64_t(v), 8); }
    return *this;
}
Writer& Writer::uinteger(uint64_t v) {
    if (v < 128) out_.push_back(uint8_t(v));
    else if (v < 256) { out_.push_back(0xcc); be(v, 1); }
    else if (v < 65536) { out_.push_back(0xcd); be(v, 2); }
    else if (v <= 0xffffffffull) { out_.push_back(0xce); be(v, 4); }
    else { out_.push_back(0xcf); be(v, 8); }
    return *this;
}
Writer& Writer::float32(float v) {
    uint32_t bits;
    std::memcpy(&bits, &v, 4);
    out_.push_back(0xca);
    be(bits, 4);
    return *this;
}
Writer& Writer::float64(double v) {
    uint64_t bits;
    std::memcpy(&bits, &v, 8);
    out_.push_back(0xcb);
    be(bits, 8);
    return *this;
}
Writer& Writer::str(std::string_view s) {
    const size_t n = s.size();
    if (n <= 31) out_.push_back(uint8_t(0xa0 | n));
    else if (n <= 255 && !opt_.legacyRaw) { out_.push_back(0xd9); be(n, 1); }
    else if (n <= 65535) { out_.push_back(0xda); be(n, 2); }
    else { out_.push_back(0xdb); be(n, 4); }
    out_.insert(out_.end(), s.begin(), s.end());
    return *this;
}
Writer& Writer::bin(const uint8_t* p, size_t n) {
    if (opt_.legacyRaw) return str(std::string_view(reinterpret_cast<const char*>(p), n));
    if (n <= 255) { out_.push_back(0xc4); be(n, 1); }
    else if (n <= 65535) { out_.push_back(0xc5); be(n, 2); }
    else { out_.push_back(0xc6); be(n, 4); }
    out_.insert(out_.end(), p, p + n);
    return *this;
}
Writer& Writer::ext(int8_t type, const uint8_t* p, size_t n) {
    if (opt_.legacyRaw) return bin(p, n);   // the old spec has no ext: keep the payload as raw
    switch (n) {
    case 1: out_.push_back(0xd4); break;
    case 2: out_.push_back(0xd5); break;
    case 4: out_.push_back(0xd6); break;
    case 8: out_.push_back(0xd7); break;
    case 16: out_.push_back(0xd8); break;
    default:
        if (n <= 255) { out_.push_back(0xc7); be(n, 1); }
        else if (n <= 65535) { out_.push_back(0xc8); be(n, 2); }
        else { out_.push_back(0xc9); be(n, 4); }
    }
    out_.push_back(uint8_t(type));
    out_.insert(out_.end(), p, p + n);
    return *this;
}
Writer& Writer::arrayHeader(uint32_t n) {
    if (n <= 15) out_.push_back(uint8_t(0x90 | n));
    else if (n <= 65535) { out_.push_back(0xdc); be(n, 2); }
    else { out_.push_back(0xdd); be(n, 4); }
    return *this;
}
Writer& Writer::mapHeader(uint32_t n) {
    if (n <= 15) out_.push_back(uint8_t(0x80 | n));
    else if (n <= 65535) { out_.push_back(0xde); be(n, 2); }
    else { out_.push_back(0xdf); be(n, 4); }
    return *this;
}
Writer& Writer::value(const Value& v) {
    switch (v.type()) {
    case Type::Nil: return nil();
    case Type::Bool: return boolean(v.asBool());
    case Type::Int: return integer(v.asInt());
    case Type::UInt: return uinteger(v.asUInt());
    case Type::Float32: return float32(float(v.asDouble()));
    case Type::Float64: return float64(v.asDouble());
    case Type::Str: return str(v.asStr());
    case Type::Bin: return bin(v.asBin().data(), v.asBin().size());
    case Type::Ext: return ext(v.extType(), v.asBin().data(), v.asBin().size());
    case Type::Array:
        arrayHeader(uint32_t(v.size()));
        for (auto& e : v.items()) value(e);
        return *this;
    case Type::Map:
        mapHeader(uint32_t(v.size()));
        for (auto& e : v.items()) value(e);   // alternating key/value
        return *this;
    }
    return *this;
}

Bytes encode(const Value& v, EncodeOptions o) {
    Writer w(o);
    w.value(v);
    return w.take();
}

// ---------------------------------------------------------------- decoder

namespace {
struct Reader {
    const uint8_t* p;
    size_t n, pos = 0;
    DecodeLimits lim;
    uint32_t items = 0;
    std::string err;

    bool fail(const std::string& e) { if (err.empty()) err = e + " at byte " + std::to_string(pos); return false; }
    bool need(size_t k) { return k <= n - pos ? true : fail("unexpected end of data"); }
    uint64_t be(int bytes) {
        uint64_t v = 0;
        for (int i = 0; i < bytes; ++i) v = v << 8 | p[pos++];
        return v;
    }
    bool len(int bytes, uint64_t& out) {
        if (!need(size_t(bytes))) return false;
        out = be(bytes);
        return true;
    }
    bool raw(uint64_t k, std::string& s) {
        if (!need(k)) return false;
        s.assign(reinterpret_cast<const char*>(p + pos), size_t(k));
        pos += size_t(k);
        return true;
    }
    bool rawBytes(uint64_t k, Bytes& b) {
        if (!need(k)) return false;
        b.assign(p + pos, p + pos + size_t(k));
        pos += size_t(k);
        return true;
    }
    bool container(uint64_t count, bool isMap, uint32_t depth, Value& out) {
        if (depth >= lim.maxDepth) return fail("nesting too deep");
        // Every element needs at least one byte: reject absurd counts before allocating.
        if (count > n - pos || items + count > lim.maxItems) return fail("container too large");
        items += uint32_t(count);
        out = isMap ? Value::map() : Value::array();
        for (uint64_t i = 0; i < count; ++i) {
            if (isMap) {
                Value k, v;
                if (!read(k, depth + 1) || !read(v, depth + 1)) return false;
                out.set(std::move(k), std::move(v));
            } else {
                Value v;
                if (!read(v, depth + 1)) return false;
                out.push(std::move(v));
            }
        }
        return true;
    }
    bool ext(uint64_t k, Value& out) {
        if (!need(1)) return false;
        const int8_t t = int8_t(p[pos++]);
        Bytes b;
        if (!rawBytes(k, b)) return false;
        out = Value::ext(t, std::move(b));
        return true;
    }
    bool read(Value& out, uint32_t depth) {
        if (!need(1)) return false;
        const uint8_t b = p[pos++];
        uint64_t k = 0;
        std::string s;
        Bytes bin;
        if (b <= 0x7f) { out = Value::uinteger(b); return true; }
        if (b >= 0xe0) { out = Value::integer(int8_t(b)); return true; }
        if (b >= 0x80 && b <= 0x8f) return container(b & 0x0f, true, depth, out);
        if (b >= 0x90 && b <= 0x9f) return container(b & 0x0f, false, depth, out);
        if (b >= 0xa0 && b <= 0xbf) { if (!raw(b & 0x1f, s)) return false; out = Value::str(std::move(s)); return true; }
        switch (b) {
        case 0xc0: out = Value::nil(); return true;
        case 0xc2: out = Value::boolean(false); return true;
        case 0xc3: out = Value::boolean(true); return true;
        case 0xc4: case 0xc5: case 0xc6:
            if (!len(b == 0xc4 ? 1 : b == 0xc5 ? 2 : 4, k) || !rawBytes(k, bin)) return false;
            out = Value::bin(std::move(bin));
            return true;
        case 0xc7: case 0xc8: case 0xc9:
            return len(b == 0xc7 ? 1 : b == 0xc8 ? 2 : 4, k) && ext(k, out);
        case 0xca: {
            if (!need(4)) return false;
            const uint32_t bits = uint32_t(be(4));
            float f;
            std::memcpy(&f, &bits, 4);
            out = Value::float32(f);
            return true;
        }
        case 0xcb: {
            if (!need(8)) return false;
            const uint64_t bits = be(8);
            double d;
            std::memcpy(&d, &bits, 8);
            out = Value::float64(d);
            return true;
        }
        case 0xcc: if (!len(1, k)) return false; out = Value::uinteger(k); return true;
        case 0xcd: if (!len(2, k)) return false; out = Value::uinteger(k); return true;
        case 0xce: if (!len(4, k)) return false; out = Value::uinteger(k); return true;
        case 0xcf: if (!len(8, k)) return false; out = Value::uinteger(k); return true;
        case 0xd0: if (!len(1, k)) return false; out = Value::integer(int8_t(k)); return true;
        case 0xd1: if (!len(2, k)) return false; out = Value::integer(int16_t(k)); return true;
        case 0xd2: if (!len(4, k)) return false; out = Value::integer(int32_t(k)); return true;
        case 0xd3: if (!len(8, k)) return false; out = Value::integer(int64_t(k)); return true;
        case 0xd4: return ext(1, out);
        case 0xd5: return ext(2, out);
        case 0xd6: return ext(4, out);
        case 0xd7: return ext(8, out);
        case 0xd8: return ext(16, out);
        case 0xd9: case 0xda: case 0xdb:   // str8 (new spec) / raw16 / raw32 (both specs)
            if (!len(b == 0xd9 ? 1 : b == 0xda ? 2 : 4, k) || !raw(k, s)) return false;
            out = Value::str(std::move(s));
            return true;
        case 0xdc: return len(2, k) && container(k, false, depth, out);
        case 0xdd: return len(4, k) && container(k, false, depth, out);
        case 0xde: return len(2, k) && container(k, true, depth, out);
        case 0xdf: return len(4, k) && container(k, true, depth, out);
        default: --pos; return fail("reserved byte 0xc1");
        }
    }
};
} // namespace

bool decode(const uint8_t* data, size_t n, Value& out, std::string* error, size_t* consumed, DecodeLimits limits) {
    Reader r{data, n, 0, limits, 0, {}};
    Value v;
    if (!data && n) { if (error) *error = "null data"; return false; }
    if (!r.read(v, 0)) {
        if (error) *error = r.err;
        return false;
    }
    out = std::move(v);
    if (consumed) *consumed = r.pos;
    return true;
}

// ---------------------------------------------------------------- JSON bridge

Json toJson(const Value& v) {
    switch (v.type()) {
    case Type::Nil: return Json();
    case Type::Bool: return Json::fromBool(v.asBool());
    case Type::Int: return Json::number(v.asInt());
    case Type::UInt: return Json::numberText(std::to_string(v.asUInt()));
    case Type::Float32: case Type::Float64: {
        const double d = v.asDouble();
        if (!std::isfinite(d)) return Json();
        char buf[40];
        std::snprintf(buf, sizeof buf, "%.17g", d);
        return Json::numberText(buf);
    }
    case Type::Str: return Json::string(v.asStr());
    case Type::Bin: return Json::string(enc::base64(v.asBin().data(), v.asBin().size()));
    case Type::Ext: {
        Json j = Json::object();
        j.set("$ext", Json::number(int64_t(v.extType())));
        j.set("data", Json::string(enc::base64(v.asBin().data(), v.asBin().size())));
        return j;
    }
    case Type::Array: {
        Json j = Json::array();
        for (auto& e : v.items()) j.push(toJson(e));
        return j;
    }
    case Type::Map: {
        Json j = Json::object();
        for (size_t i = 0; i < v.size(); ++i) {
            const Value& k = v.key(i);
            j.set(k.isStr() ? k.asStr() : toJson(k).dump(-1), toJson(v.val(i)));
        }
        return j;
    }
    }
    return Json();
}

Value fromJson(const Json& j) {
    switch (j.type()) {
    case Json::Type::Null: return Value::nil();
    case Json::Type::Bool: return Value::boolean(j.boolean());
    case Json::Type::String: return Value::str(j.str());
    case Json::Type::Number: {
        const std::string& lit = j.numberLiteral();
        const bool integral = lit.find_first_of(".eE") == std::string::npos;
        if (integral) {
            errno = 0;
            char* end = nullptr;
            if (!lit.empty() && lit[0] == '-') {
                const long long x = std::strtoll(lit.c_str(), &end, 10);
                if (errno == 0 && end && *end == 0) return Value::integer(x);
            } else {
                const unsigned long long x = std::strtoull(lit.c_str(), &end, 10);
                if (errno == 0 && end && *end == 0) return Value::uinteger(x);
            }
        }
        return Value::float64(std::strtod(lit.c_str(), nullptr));
    }
    case Json::Type::Array: {
        Value a = Value::array();
        for (auto& e : j.items()) a.push(fromJson(e));
        return a;
    }
    case Json::Type::Object: {
        Value m = Value::map();
        for (auto& [k, e] : j.members()) m.set(Value::str(k), fromJson(e));
        return m;
    }
    }
    return Value::nil();
}

} // namespace aiverse::meta::msgpack
