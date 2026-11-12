// HPACK (RFC 7541) header compression for the HTTP/2 transport and the in-process test server.
// Decoder: full (static + dynamic table, Huffman, size updates). Encoder: static-table indexing
// plus literals without indexing, so it never depends on the peer's table state.
#pragma once
#include <cstdint>
#include <deque>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace aiverse::grpc::hpack {

using Header = std::pair<std::string, std::string>;
using HeaderList = std::vector<Header>;

class Decoder {
  public:
    // Limit announced in our SETTINGS_HEADER_TABLE_SIZE.
    explicit Decoder(uint32_t maxTableSize = 4096) : maxAllowed_(maxTableSize), maxSize_(maxTableSize) {}
    // Decodes one complete header block. False = COMPRESSION_ERROR (connection must be closed).
    bool decode(std::string_view block, HeaderList &out, size_t maxListBytes = 256 * 1024);

  private:
    bool lookup(uint64_t index, Header &out) const;
    void insert(Header h);
    void evict();
    std::deque<Header> table_;
    size_t size_ = 0;
    uint32_t maxAllowed_;
    uint32_t maxSize_;
};

class Encoder {
  public:
    void encode(const HeaderList &headers, std::string &out) const;
};

// Exposed for tests.
bool huffmanDecode(std::string_view in, std::string &out);
void huffmanEncode(std::string_view in, std::string &out);
size_t huffmanLength(std::string_view in);

} // namespace aiverse::grpc::hpack
