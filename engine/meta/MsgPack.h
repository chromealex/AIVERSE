// MessagePack (https://msgpack.org) values, encoder and decoder. Host/meta code only: floats are
// allowed here, never in simulation state. Decoding is bounded (depth, item count) and never throws.
//
//   msgpack::Value v = msgpack::Value::array({msgpack::Value::integer(300), msgpack::Value::str("hi")});
//   msgpack::Bytes b = msgpack::encode(v);                  // 92 cd 01 2c a2 68 69
//   msgpack::Value back; std::string err;
//   msgpack::decode(b.data(), b.size(), back, &err);
//
// EncodeOptions::legacyRaw writes the pre-2013 spec ("raw" only: no str8, no bin, no ext), which is
// what msgpack-java 0.6 and old MsgPack-CLI peers produce and expect. The decoder reads both specs.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "engine/data/Json.h"

namespace aiverse::meta::msgpack {

using Bytes = std::vector<uint8_t>;

enum class Type : uint8_t { Nil, Bool, Int, UInt, Float32, Float64, Str, Bin, Array, Map, Ext };

class Value {
public:
    Value() = default;
    static Value nil() { return Value(); }
    static Value boolean(bool b);
    static Value integer(int64_t v);            // stored as UInt when v >= 0 (same wire bytes)
    static Value uinteger(uint64_t v);
    static Value float32(float v);
    static Value float64(double v);
    static Value str(std::string s);
    static Value bin(Bytes b);
    static Value array(std::vector<Value> items = {});
    static Value map();                        // add entries with set()
    static Value ext(int8_t type, Bytes data);

    Type type() const { return type_; }
    bool isNil() const { return type_ == Type::Nil; }
    bool isInteger() const { return type_ == Type::Int || type_ == Type::UInt; }
    bool isStr() const { return type_ == Type::Str; }
    bool isArray() const { return type_ == Type::Array; }
    bool isMap() const { return type_ == Type::Map; }

    bool asBool(bool def = false) const { return type_ == Type::Bool ? b_ : def; }
    int64_t asInt(int64_t def = 0) const;      // Int, or UInt that fits int64
    uint64_t asUInt(uint64_t def = 0) const;   // UInt, or nonnegative Int
    double asDouble(double def = 0) const;     // any number
    const std::string& asStr() const { return s_; }            // Str
    const Bytes& asBin() const { return bin_; }                // Bin / Ext payload
    int8_t extType() const { return ext_; }

    // Arrays: items. Maps: entries are stored as alternating key/value in items (deterministic order).
    size_t size() const { return type_ == Type::Map ? items_.size() / 2 : items_.size(); }
    const std::vector<Value>& items() const { return items_; }
    const Value& operator[](size_t i) const;                   // array element (nil if out of range)
    const Value& key(size_t i) const { return items_[2 * i]; }
    const Value& val(size_t i) const { return items_[2 * i + 1]; }
    const Value* find(std::string_view strKey) const;          // map lookup by string key
    Value& push(Value v);                                      // arrays
    Value& set(Value key, Value v);                            // maps (replaces an equal key)
    Value& set(const std::string& key, Value v) { return set(str(key), std::move(v)); }

    bool operator==(const Value& o) const;
    bool operator!=(const Value& o) const { return !(*this == o); }

private:
    Type type_ = Type::Nil;
    bool b_ = false;
    int8_t ext_ = 0;
    int64_t i_ = 0;
    uint64_t u_ = 0;
    double d_ = 0;
    std::string s_;
    Bytes bin_;
    std::vector<Value> items_;
};

struct EncodeOptions {
    bool legacyRaw = false;   // old spec: strings and binary as raw (fixraw/raw16/raw32), no str8/bin/ext
};

// Low-level writer: explicit headers, smallest integer encoding (the same choice msgpack-java's
// Packer and MsgPack-CLI make), so producers can match a peer byte for byte.
class Writer {
public:
    explicit Writer(EncodeOptions o = {}) : opt_(o) {}
    Writer& nil();
    Writer& boolean(bool b);
    Writer& integer(int64_t v);
    Writer& uinteger(uint64_t v);
    Writer& float32(float v);
    Writer& float64(double v);
    Writer& str(std::string_view s);
    Writer& bin(const uint8_t* p, size_t n);
    Writer& ext(int8_t type, const uint8_t* p, size_t n);
    Writer& arrayHeader(uint32_t n);
    Writer& mapHeader(uint32_t n);
    Writer& value(const Value& v);
    const Bytes& bytes() const { return out_; }
    Bytes take() { return std::move(out_); }

private:
    void be(uint64_t v, int bytes);
    EncodeOptions opt_;
    Bytes out_;
};

Bytes encode(const Value& v, EncodeOptions o = {});

struct DecodeLimits {
    uint32_t maxDepth = 64;
    uint32_t maxItems = 1u << 20;   // total array elements + map entries
};
// Decodes one value from the front of data. consumed (optional) receives its size, so a stream of
// concatenated values can be read one after another. Returns false (and error) on malformed input.
bool decode(const uint8_t* data, size_t n, Value& out, std::string* error = nullptr, size_t* consumed = nullptr,
            DecodeLimits limits = {});
inline bool decode(const Bytes& b, Value& out, std::string* error = nullptr) { return decode(b.data(), b.size(), out, error); }

// JSON bridge for UI/meta code. Integers stay exact (int64/uint64 literals); other numbers are
// float64. toJson: Bin -> base64 string, Ext -> {"$ext": type, "data": base64}, non-string map
// keys -> their JSON text. fromJson: integral literals -> integer, others -> float64.
Json toJson(const Value& v);
Value fromJson(const Json& j);

} // namespace aiverse::meta::msgpack
