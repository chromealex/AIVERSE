// Dictation for the editor's text fields (the game wizard's idea and meta wishes): speech -> text with the OS's own
// recognizer, no keys. macOS: Speech framework + AVAudioEngine (EditorSpeech_mac.mm; asks for the microphone and speech
// recognition once). Elsewhere: not supported (EditorSpeech.cpp) — the OS dictation still types into the field.
#pragma once
#include <string>
#include <vector>

namespace aiverse::editor::speech {
bool supported();
// Starts listening in `locale` ("ru-RU", "en-US"); permissions are asked asynchronously (error() says when refused).
void start(const std::string& locale, const std::vector<std::string>& vocabulary = {});   // vocabulary: words to expect
void stop();                    // ends the audio; the last words arrive a moment later
bool busy();                    // listening or finishing
bool listening();
std::string transcript();       // the text heard so far in this session
bool takeResult(std::string& out);   // the session's final text, once, after it ended
std::string error();
} // namespace aiverse::editor::speech
