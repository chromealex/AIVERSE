// ScriptSound (the Lua `sound` table, engine/view/game/ScriptSound.h) over the AudioEngine.
#pragma once
#include "Audio.h"
#include "../view/game/ScriptSound.h"

namespace aiverse::game {

class AudioScriptSound final : public ScriptSound {
public:
    explicit AudioScriptSound(audio::AudioEngine& e) : e_(e) {}
    bool available() const override;
    uint32_t play(const std::string& name, const ScriptSoundPlay& p) override;
    void set(uint32_t voice, float gain, float pan, float pitch) override;
    void position(uint32_t voice, float x, float y, float z) override { e_.setVoicePosition(voice, x, y, z); }
    void stop(uint32_t voice, float fade) override;
    void pause(uint32_t voice, bool paused) override;
    void seek(uint32_t voice, float seconds) override;
    bool state(uint32_t voice, float& time, bool& paused) const override;
    int variants(const std::string& name) const override;
    float length(const std::string& name, int variant) const override;
    bool music(const std::string& name, float fade) override;
    void musicGain(float gain) override;
    std::string currentMusic() const override;
    ScriptSoundSettings settings() const override;
private:
    audio::AudioEngine& e_;
};

} // namespace aiverse::game
