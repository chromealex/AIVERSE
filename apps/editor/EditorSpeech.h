// Dictation for the editor's text fields (the game wizard's idea and meta wishes): speech -> text with the OS's own
// recognizer, no keys. macOS: Speech framework + AVAudioEngine (EditorSpeech_mac.mm; asks for the microphone and speech
// recognition once). Elsewhere: not supported (EditorSpeech.cpp) — the OS dictation still types into the field.
#pragma once
#include <string>
#include <vector>

namespace aiverse::editor::speech {
// Words the recognizer should expect in a game pitch: game names (often English inside Russian speech) and genre terms.
inline const std::vector<std::string>& pitchVocabulary() {
    static const std::vector<std::string> v = {
        "StarCraft", "StarCraft 2", "Warcraft", "Warcraft 3", "Command & Conquer", "Red Alert", "Age of Empires", "Company of Heroes",
        "Dune", "Total War", "They Are Billions", "Northgard", "Stronghold", "Settlers", "Anno", "Rise of Nations", "Supreme Commander",
        "Clash Royale", "Clash of Clans", "Brawl Stars", "Rush Royale", "Kingdom Rush", "Bloons", "Plants vs Zombies", "Dota", "League of Legends",
        "Mindustry", "Factorio", "RimWorld", "Frostpunk", "Northgard", "Bad North", "Mini Metro", "Auto Chess", "Hearthstone",
        "RTS", "MOBA", "tower defense", "Tower Defense", "idle", "roguelike", "рогалик", "PvP", "PvE", "GDD", "ГДД", "мета", "метагейм",
        "юниты", "юнит", "апгрейды", "апгрейд", "прокачка", "фракции", "фракция", "ресурсы", "лут", "боссы", "босс", "волны", "лейны",
        "гарнизон", "башни", "казармы", "рабочие", "харвестер", "микро", "макро", "туман войны", "скирмиш", "кампания", "туториал",
        "софт валюта", "хард валюта", "гемы", "кристаллы", "золото", "боевой пропуск", "батл пасс", "ежедневные награды", "сундуки",
        "лутбоксы", "гача", "кланы", "лидерборд", "рейтинг", "ивенты", "оффлайн доход", "монетизация", "реклама за награду", "инапы",
        "мобилка", "портрет", "сай-фай", "стимпанк", "фэнтези", "постапокалипсис", "зомби", "орки", "эльфы", "роботы", "мехи"};
    return v;
}
inline std::string localeForLanguage(const std::string& lang) {
    return lang == "ru" ? "ru-RU" : lang == "en" ? "en-US" : lang == "uk" ? "uk-UA" : lang == "kk" ? "kk-KZ"
                                     : lang == "de" ? "de-DE" : lang == "fr" ? "fr-FR" : lang == "es" ? "es-ES" : lang == "pt" ? "pt-BR"
                                     : lang == "it" ? "it-IT" : lang == "tr" ? "tr-TR" : lang == "zh" ? "zh-CN" : lang == "ja" ? "ja-JP"
                                     : lang == "ko" ? "ko-KR" : lang;
}
bool supported();
// Starts listening in `locale` ("ru-RU", "en-US"); permissions are asked asynchronously (error() says when refused).
void start(const std::string& locale, const std::vector<std::string>& vocabulary = {});   // vocabulary: words to expect
void stop();                    // ends the audio; the last words arrive a moment later
void cancel();                  // discard pending audio and authorization callbacks
bool recording();               // microphone is capturing audio
bool busy();                    // listening or finishing
bool listening();
std::string transcript();       // the text heard so far in this session
bool takeResult(std::string& out);   // the session's final text, once, after it ended
std::string error();
} // namespace aiverse::editor::speech
