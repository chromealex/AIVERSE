// Lua `sound` table for presentation scripts (view scripts and the client's UI Lua): voice handles with
// gain / pan / pitch every frame, pause, seek and state, named music with crossfades and a script music
// level, the player's sound settings. Presentation only: nothing here reaches the simulation.
// The host implements ScriptSound over its audio engine (modules/rts/audio/AudioScriptSound.h); without
// one (headless, tests) the table exists and every call is a harmless no-op. docs/scripting.md § Sound.
#pragma once
#include <cstdint>
#include <string>

struct lua_State;

namespace rts::game {

struct ScriptSoundPlay {
    float gain = 1, pan = 0, pitch = 1, at = 0;   // at: seconds into the clip
    int variant = -1;                             // 0-based; -1 random
    int loop = -1;                                // -1 = the sound's own setting
};

struct ScriptSoundSettings {
    float music = 1, sfx = 1;          // the player's volumes 0..1
    bool musicOn = true, sfxOn = true; // on / off switches (independent of the volumes)
};

class ScriptSound {
public:
    virtual ~ScriptSound() = default;
    virtual bool available() const = 0;                          // a mixer runs (else plays return no handle)
    virtual uint32_t play(const std::string& name, const ScriptSoundPlay& p) = 0;   // 0 = not started
    virtual void set(uint32_t voice, float gain, float pan, float pitch) = 0;      // NaN = unchanged
    virtual void stop(uint32_t voice, float fade) = 0;
    virtual void pause(uint32_t voice, bool paused) = 0;
    virtual void seek(uint32_t voice, float seconds) = 0;
    virtual bool state(uint32_t voice, float& time, bool& paused) const = 0;      // false = not playing
    virtual int variants(const std::string& name) const = 0;                      // 0 = unknown sound
    virtual float length(const std::string& name, int variant) const = 0;         // seconds, 0 unknown
    virtual bool music(const std::string& name, float fade) = 0;                   // "" stops
    virtual void musicGain(float gain) = 0;
    virtual std::string currentMusic() const = 0;
    virtual ScriptSoundSettings settings() const = 0;
};

// Registers the global `sound` table. *host may change or be null later (read on every call).
void openScriptSound(lua_State* L, ScriptSound* const* host);

} // namespace rts::game
