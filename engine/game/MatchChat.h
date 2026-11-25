#pragma once
#include "Components.h"
#include "engine/sim/Command.h"
#include <bit>
#include <string_view>

namespace aiverse::game {

// Keep the existing replay, rollback and network command encodings. City admits 32 argument words.
inline constexpr size_t kMaxChatBytes = 256;

inline bool validChatText(std::string_view text) {
    if (text.empty() || text.size() > kMaxChatBytes) return false;
    bool content = false;
    for (size_t i = 0; i < text.size();) {
        const auto first = uint8_t(text[i++]);
        if (first < 32 || first == 127) return false;
        content |= first != ' ';
        if (first < 128) continue;
        if (first < 0xc2 || first > 0xf4) return false;
        const size_t count = first < 0xe0 ? 1 : first < 0xf0 ? 2 : 3;
        if (count > text.size() - i) return false;
        const auto second = uint8_t(text[i]);
        if ((first == 0xe0 && second < 0xa0) || (first == 0xed && second >= 0xa0) ||
            (first == 0xf0 && second < 0x90) || (first == 0xf4 && second >= 0x90)) return false;
        for (size_t n = 0; n < count; ++n)
            if ((uint8_t(text[i++]) & 0xc0) != 0x80) return false;
    }
    return content;
}

inline Command chatCommand(std::string_view text) {
    Command cmd;
    cmd.type = kCmdChat;
    if (!validChatText(text)) return cmd;
    cmd.param = int64_t(text.size());
    for (size_t start = 0; start < text.size(); start += 8) {
        uint64_t word = 0;
        for (size_t n = 0; n < 8 && start + n < text.size(); ++n)
            word |= uint64_t(uint8_t(text[start + n])) << (8 * n);
        cmd.args.push_back(std::bit_cast<int64_t>(word));
    }
    return cmd;
}

inline std::string chatText(const Command& cmd) {
    if (cmd.param <= 0 || cmd.param > int64_t(kMaxChatBytes) || cmd.args.size() != (size_t(cmd.param) + 7) / 8) return {};
    std::string text(size_t(cmd.param), ' ');
    for (size_t i = 0; i < text.size(); ++i)
        text[i] = char(uint8_t(uint64_t(cmd.args[i / 8]) >> (8 * (i % 8))));
    return validChatText(text) ? text : std::string{};
}
} // namespace aiverse::game
