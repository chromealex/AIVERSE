// Dictation stub for platforms without a built-in recognizer hookup (EditorSpeech.h).
#include "EditorSpeech.h"

namespace rts::editor::speech {
bool supported() { return false; }
void start(const std::string&, const std::vector<std::string>&) {}
void stop() {}
bool busy() { return false; }
bool listening() { return false; }
std::string transcript() { return {}; }
bool takeResult(std::string&) { return false; }
std::string error() { return "диктовка в редакторе есть только на macOS: используйте системную диктовку"; }
} // namespace rts::editor::speech
