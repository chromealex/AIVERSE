#include "Hpack.h"
#include <array>

namespace aiverse::grpc::hpack {
namespace {

struct Code {
    uint32_t bits;
    uint8_t len;
};
constexpr Code kHuffman[257] = {
    {0x1ff8u,13}, {0x7fffd8u,23}, {0xfffffe2u,28}, {0xfffffe3u,28}, {0xfffffe4u,28}, {0xfffffe5u,28}, {0xfffffe6u,28}, {0xfffffe7u,28},
    {0xfffffe8u,28}, {0xffffeau,24}, {0x3ffffffcu,30}, {0xfffffe9u,28}, {0xfffffeau,28}, {0x3ffffffdu,30}, {0xfffffebu,28}, {0xfffffecu,28},
    {0xfffffedu,28}, {0xfffffeeu,28}, {0xfffffefu,28}, {0xffffff0u,28}, {0xffffff1u,28}, {0xffffff2u,28}, {0x3ffffffeu,30}, {0xffffff3u,28},
    {0xffffff4u,28}, {0xffffff5u,28}, {0xffffff6u,28}, {0xffffff7u,28}, {0xffffff8u,28}, {0xffffff9u,28}, {0xffffffau,28}, {0xffffffbu,28},
    {0x14u,6}, {0x3f8u,10}, {0x3f9u,10}, {0xffau,12}, {0x1ff9u,13}, {0x15u,6}, {0xf8u,8}, {0x7fau,11},
    {0x3fau,10}, {0x3fbu,10}, {0xf9u,8}, {0x7fbu,11}, {0xfau,8}, {0x16u,6}, {0x17u,6}, {0x18u,6},
    {0x0u,5}, {0x1u,5}, {0x2u,5}, {0x19u,6}, {0x1au,6}, {0x1bu,6}, {0x1cu,6}, {0x1du,6},
    {0x1eu,6}, {0x1fu,6}, {0x5cu,7}, {0xfbu,8}, {0x7ffcu,15}, {0x20u,6}, {0xffbu,12}, {0x3fcu,10},
    {0x1ffau,13}, {0x21u,6}, {0x5du,7}, {0x5eu,7}, {0x5fu,7}, {0x60u,7}, {0x61u,7}, {0x62u,7},
    {0x63u,7}, {0x64u,7}, {0x65u,7}, {0x66u,7}, {0x67u,7}, {0x68u,7}, {0x69u,7}, {0x6au,7},
    {0x6bu,7}, {0x6cu,7}, {0x6du,7}, {0x6eu,7}, {0x6fu,7}, {0x70u,7}, {0x71u,7}, {0x72u,7},
    {0xfcu,8}, {0x73u,7}, {0xfdu,8}, {0x1ffbu,13}, {0x7fff0u,19}, {0x1ffcu,13}, {0x3ffcu,14}, {0x22u,6},
    {0x7ffdu,15}, {0x3u,5}, {0x23u,6}, {0x4u,5}, {0x24u,6}, {0x5u,5}, {0x25u,6}, {0x26u,6},
    {0x27u,6}, {0x6u,5}, {0x74u,7}, {0x75u,7}, {0x28u,6}, {0x29u,6}, {0x2au,6}, {0x7u,5},
    {0x2bu,6}, {0x76u,7}, {0x2cu,6}, {0x8u,5}, {0x9u,5}, {0x2du,6}, {0x77u,7}, {0x78u,7},
    {0x79u,7}, {0x7au,7}, {0x7bu,7}, {0x7ffeu,15}, {0x7fcu,11}, {0x3ffdu,14}, {0x1ffdu,13}, {0xffffffcu,28},
    {0xfffe6u,20}, {0x3fffd2u,22}, {0xfffe7u,20}, {0xfffe8u,20}, {0x3fffd3u,22}, {0x3fffd4u,22}, {0x3fffd5u,22}, {0x7fffd9u,23},
    {0x3fffd6u,22}, {0x7fffdau,23}, {0x7fffdbu,23}, {0x7fffdcu,23}, {0x7fffddu,23}, {0x7fffdeu,23}, {0xffffebu,24}, {0x7fffdfu,23},
    {0xffffecu,24}, {0xffffedu,24}, {0x3fffd7u,22}, {0x7fffe0u,23}, {0xffffeeu,24}, {0x7fffe1u,23}, {0x7fffe2u,23}, {0x7fffe3u,23},
    {0x7fffe4u,23}, {0x1fffdcu,21}, {0x3fffd8u,22}, {0x7fffe5u,23}, {0x3fffd9u,22}, {0x7fffe6u,23}, {0x7fffe7u,23}, {0xffffefu,24},
    {0x3fffdau,22}, {0x1fffddu,21}, {0xfffe9u,20}, {0x3fffdbu,22}, {0x3fffdcu,22}, {0x7fffe8u,23}, {0x7fffe9u,23}, {0x1fffdeu,21},
    {0x7fffeau,23}, {0x3fffddu,22}, {0x3fffdeu,22}, {0xfffff0u,24}, {0x1fffdfu,21}, {0x3fffdfu,22}, {0x7fffebu,23}, {0x7fffecu,23},
    {0x1fffe0u,21}, {0x1fffe1u,21}, {0x3fffe0u,22}, {0x1fffe2u,21}, {0x7fffedu,23}, {0x3fffe1u,22}, {0x7fffeeu,23}, {0x7fffefu,23},
    {0xfffeau,20}, {0x3fffe2u,22}, {0x3fffe3u,22}, {0x3fffe4u,22}, {0x7ffff0u,23}, {0x3fffe5u,22}, {0x3fffe6u,22}, {0x7ffff1u,23},
    {0x3ffffe0u,26}, {0x3ffffe1u,26}, {0xfffebu,20}, {0x7fff1u,19}, {0x3fffe7u,22}, {0x7ffff2u,23}, {0x3fffe8u,22}, {0x1ffffecu,25},
    {0x3ffffe2u,26}, {0x3ffffe3u,26}, {0x3ffffe4u,26}, {0x7ffffdeu,27}, {0x7ffffdfu,27}, {0x3ffffe5u,26}, {0xfffff1u,24}, {0x1ffffedu,25},
    {0x7fff2u,19}, {0x1fffe3u,21}, {0x3ffffe6u,26}, {0x7ffffe0u,27}, {0x7ffffe1u,27}, {0x3ffffe7u,26}, {0x7ffffe2u,27}, {0xfffff2u,24},
    {0x1fffe4u,21}, {0x1fffe5u,21}, {0x3ffffe8u,26}, {0x3ffffe9u,26}, {0xffffffdu,28}, {0x7ffffe3u,27}, {0x7ffffe4u,27}, {0x7ffffe5u,27},
    {0xfffecu,20}, {0xfffff3u,24}, {0xfffedu,20}, {0x1fffe6u,21}, {0x3fffe9u,22}, {0x1fffe7u,21}, {0x1fffe8u,21}, {0x7ffff3u,23},
    {0x3fffeau,22}, {0x3fffebu,22}, {0x1ffffeeu,25}, {0x1ffffefu,25}, {0xfffff4u,24}, {0xfffff5u,24}, {0x3ffffeau,26}, {0x7ffff4u,23},
    {0x3ffffebu,26}, {0x7ffffe6u,27}, {0x3ffffecu,26}, {0x3ffffedu,26}, {0x7ffffe7u,27}, {0x7ffffe8u,27}, {0x7ffffe9u,27}, {0x7ffffeau,27},
    {0x7ffffebu,27}, {0xffffffeu,28}, {0x7ffffecu,27}, {0x7ffffedu,27}, {0x7ffffeeu,27}, {0x7ffffefu,27}, {0x7fffff0u,27}, {0x3ffffeeu,26},
    {0x3fffffffu,30},
};

// Binary decoding tree built once from the canonical code table.
struct Tree {
    struct Node {
        int16_t child[2] = {-1, -1};
        int16_t sym = -1;
    };
    std::vector<Node> nodes;
    Tree() {
        nodes.reserve(520);
        nodes.emplace_back();
        for (int s = 0; s < 257; ++s) {
            int n = 0;
            for (int b = kHuffman[s].len - 1; b >= 0; --b) {
                const int bit = int((kHuffman[s].bits >> b) & 1);
                if (nodes[size_t(n)].child[bit] < 0) {
                    nodes[size_t(n)].child[bit] = int16_t(nodes.size());
                    nodes.emplace_back();
                }
                n = nodes[size_t(n)].child[bit];
            }
            nodes[size_t(n)].sym = int16_t(s);
        }
    }
};
const Tree &tree() {
    static const Tree t;
    return t;
}

const std::array<std::pair<const char *, const char *>, 61> kStatic = {{
    {":authority", ""}, {":method", "GET"}, {":method", "POST"}, {":path", "/"}, {":path", "/index.html"},
    {":scheme", "http"}, {":scheme", "https"}, {":status", "200"}, {":status", "204"}, {":status", "206"},
    {":status", "304"}, {":status", "400"}, {":status", "404"}, {":status", "500"}, {"accept-charset", ""},
    {"accept-encoding", "gzip, deflate"}, {"accept-language", ""}, {"accept-ranges", ""}, {"accept", ""},
    {"access-control-allow-origin", ""}, {"age", ""}, {"allow", ""}, {"authorization", ""},
    {"cache-control", ""}, {"content-disposition", ""}, {"content-encoding", ""}, {"content-language", ""},
    {"content-length", ""}, {"content-location", ""}, {"content-range", ""}, {"content-type", ""},
    {"cookie", ""}, {"date", ""}, {"etag", ""}, {"expect", ""}, {"expires", ""}, {"from", ""}, {"host", ""},
    {"if-match", ""}, {"if-modified-since", ""}, {"if-none-match", ""}, {"if-range", ""},
    {"if-unmodified-since", ""}, {"last-modified", ""}, {"link", ""}, {"location", ""}, {"max-forwards", ""},
    {"proxy-authenticate", ""}, {"proxy-authorization", ""}, {"range", ""}, {"referer", ""}, {"refresh", ""},
    {"retry-after", ""}, {"server", ""}, {"set-cookie", ""}, {"strict-transport-security", ""},
    {"transfer-encoding", ""}, {"user-agent", ""}, {"vary", ""}, {"via", ""}, {"www-authenticate", ""},
}};

bool readInt(const uint8_t *&p, const uint8_t *end, int prefixBits, uint64_t &out) {
    if (p >= end)
        return false;
    const uint8_t mask = uint8_t((1u << prefixBits) - 1);
    uint64_t v = *p++ & mask;
    if (v < mask) {
        out = v;
        return true;
    }
    for (int shift = 0; shift <= 56; shift += 7) {
        if (p >= end)
            return false;
        const uint8_t b = *p++;
        v += uint64_t(b & 0x7f) << shift;
        if (!(b & 0x80)) {
            out = v;
            return v < (1ull << 32);
        }
    }
    return false;
}

void writeInt(std::string &out, uint8_t flags, int prefixBits, uint64_t v) {
    const uint64_t mask = (1u << prefixBits) - 1;
    if (v < mask) {
        out.push_back(char(flags | uint8_t(v)));
        return;
    }
    out.push_back(char(flags | uint8_t(mask)));
    v -= mask;
    while (v >= 0x80) {
        out.push_back(char(uint8_t(v & 0x7f) | 0x80));
        v >>= 7;
    }
    out.push_back(char(uint8_t(v)));
}

bool readString(const uint8_t *&p, const uint8_t *end, std::string &out) {
    if (p >= end)
        return false;
    const bool huff = (*p & 0x80) != 0;
    uint64_t n = 0;
    if (!readInt(p, end, 7, n) || n > uint64_t(end - p))
        return false;
    std::string_view raw(reinterpret_cast<const char *>(p), size_t(n));
    p += n;
    out.clear();
    if (huff)
        return huffmanDecode(raw, out);
    out.assign(raw.data(), raw.size());
    return true;
}

void writeString(std::string &out, std::string_view s) {
    const size_t h = huffmanLength(s);
    if (h < s.size()) {
        writeInt(out, 0x80, 7, h);
        huffmanEncode(s, out);
    } else {
        writeInt(out, 0x00, 7, s.size());
        out.append(s.data(), s.size());
    }
}

} // namespace

bool huffmanDecode(std::string_view in, std::string &out) {
    const auto &t = tree();
    int n = 0;
    int depth = 0;       // bits consumed since the last emitted symbol
    bool allOnes = true; // padding must be a prefix of EOS (all ones), < 8 bits
    for (unsigned char c : in) {
        for (int b = 7; b >= 0; --b) {
            const int bit = (c >> b) & 1;
            n = t.nodes[size_t(n)].child[bit];
            if (n < 0)
                return false;
            ++depth;
            allOnes = allOnes && bit == 1;
            const int sym = t.nodes[size_t(n)].sym;
            if (sym >= 0) {
                if (sym == 256)
                    return false; // EOS inside a string
                out.push_back(char(sym));
                n = 0;
                depth = 0;
                allOnes = true;
            }
        }
    }
    return depth < 8 && allOnes;
}

size_t huffmanLength(std::string_view in) {
    uint64_t bits = 0;
    for (unsigned char c : in)
        bits += kHuffman[c].len;
    return size_t((bits + 7) / 8);
}

void huffmanEncode(std::string_view in, std::string &out) {
    uint64_t acc = 0;
    int nbits = 0;
    for (unsigned char c : in) {
        acc = (acc << kHuffman[c].len) | kHuffman[c].bits;
        nbits += kHuffman[c].len;
        while (nbits >= 8) {
            nbits -= 8;
            out.push_back(char(uint8_t(acc >> nbits)));
        }
        acc &= (1ull << nbits) - 1;
    }
    if (nbits > 0)
        out.push_back(char(uint8_t((acc << (8 - nbits)) | ((1u << (8 - nbits)) - 1))));
}

bool Decoder::lookup(uint64_t index, Header &out) const {
    if (index == 0)
        return false;
    if (index <= kStatic.size()) {
        out = {kStatic[index - 1].first, kStatic[index - 1].second};
        return true;
    }
    const uint64_t d = index - kStatic.size() - 1;
    if (d >= table_.size())
        return false;
    out = table_[size_t(d)];
    return true;
}

void Decoder::evict() {
    while (size_ > maxSize_ && !table_.empty()) {
        size_ -= table_.back().first.size() + table_.back().second.size() + 32;
        table_.pop_back();
    }
}

void Decoder::insert(Header h) {
    const size_t sz = h.first.size() + h.second.size() + 32;
    if (sz > maxSize_) {
        table_.clear();
        size_ = 0;
        return;
    }
    size_ += sz;
    table_.push_front(std::move(h));
    evict();
}

bool Decoder::decode(std::string_view block, HeaderList &out, size_t maxListBytes) {
    const uint8_t *p = reinterpret_cast<const uint8_t *>(block.data());
    const uint8_t *end = p + block.size();
    size_t total = 0;
    bool headerSeen = false;
    while (p < end) {
        const uint8_t b = *p;
        Header h;
        if (b & 0x80) { // indexed
            uint64_t idx = 0;
            if (!readInt(p, end, 7, idx) || !lookup(idx, h))
                return false;
        } else if ((b & 0xe0) == 0x20) { // dynamic table size update (only at block start)
            uint64_t sz = 0;
            if (headerSeen || !readInt(p, end, 5, sz) || sz > maxAllowed_)
                return false;
            maxSize_ = uint32_t(sz);
            evict();
            continue;
        } else {
            const bool incremental = (b & 0xc0) == 0x40;
            const int prefix = incremental ? 6 : 4;
            uint64_t idx = 0;
            if (!readInt(p, end, prefix, idx))
                return false;
            if (idx) {
                Header named;
                if (!lookup(idx, named))
                    return false;
                h.first = std::move(named.first);
            } else if (!readString(p, end, h.first)) {
                return false;
            }
            if (!readString(p, end, h.second))
                return false;
            if (incremental)
                insert(h);
        }
        headerSeen = true;
        total += h.first.size() + h.second.size() + 32;
        if (total > maxListBytes)
            return false;
        out.push_back(std::move(h));
    }
    return true;
}

void Encoder::encode(const HeaderList &headers, std::string &out) const {
    for (const auto &[name, value] : headers) {
        int nameIdx = 0;
        int fullIdx = 0;
        for (size_t i = 0; i < kStatic.size(); ++i) {
            if (name == kStatic[i].first) {
                if (!nameIdx)
                    nameIdx = int(i + 1);
                if (value == kStatic[i].second) {
                    fullIdx = int(i + 1);
                    break;
                }
            }
        }
        if (fullIdx) {
            writeInt(out, 0x80, 7, uint64_t(fullIdx));
            continue;
        }
        // Literal without indexing; credentials are marked never-indexed for intermediaries.
        const bool secret = name == "authorization" || name == "cookie";
        writeInt(out, secret ? 0x10 : 0x00, 4, uint64_t(nameIdx));
        if (!nameIdx)
            writeString(out, name);
        writeString(out, value);
    }
}

} // namespace aiverse::grpc::hpack
