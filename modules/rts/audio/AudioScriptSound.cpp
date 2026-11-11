#include "AudioScriptSound.h"
#include <algorithm>
#include <cmath>

namespace aiverse::game {

bool AudioScriptSound::available() const { return e_.running(); }

uint32_t AudioScriptSound::play(const std::string& name, const ScriptSoundPlay& p) {
    audio::PlayParams pp;
    pp.volume = p.gain;
    pp.pan = std::clamp(p.pan, -1.0f, 1.0f);
    pp.pitch = std::clamp(p.pitch, 0.01f, 8.0f);
    pp.variant = p.variant;
    pp.startAt = p.at;
    pp.loop = p.loop;
    return e_.play(name, pp);
}
void AudioScriptSound::set(uint32_t v, float gain, float pan, float pitch) {
    audio::VoiceParams vp;
    vp.volume = gain;
    vp.pan = pan;
    vp.pitch = pitch;
    e_.setVoice(v, vp);
}
void AudioScriptSound::stop(uint32_t v, float fade) { e_.stop(v, std::max(0.002f, fade)); }
void AudioScriptSound::pause(uint32_t v, bool paused) { e_.pauseVoice(v, paused); }
void AudioScriptSound::seek(uint32_t v, float seconds) { e_.seekVoice(v, seconds); }
bool AudioScriptSound::state(uint32_t v, float& time, bool& paused) const {
    const audio::VoiceState s = e_.voiceState(v);
    time = s.time;
    paused = s.paused;
    return s.playing;
}
int AudioScriptSound::variants(const std::string& name) const {
    const audio::SoundDef* d = e_.sound(e_.find(name));
    return d ? int(d->clips.size()) : 0;
}
float AudioScriptSound::length(const std::string& name, int variant) const { return e_.clipSeconds(e_.find(name), variant); }
bool AudioScriptSound::music(const std::string& name, float fade) {
    if (name.empty()) { e_.stopMusic(std::max(0.0f, fade)); return true; }
    return e_.playMusic(name, std::max(0.0f, fade));
}
void AudioScriptSound::musicGain(float gain) { e_.setMusicGain(gain); }
std::string AudioScriptSound::currentMusic() const { return e_.currentMusic(); }
ScriptSoundSettings AudioScriptSound::settings() const {
    ScriptSoundSettings s;
    s.music = e_.volume(audio::Bus::Music);
    s.sfx = e_.volume(audio::Bus::Sfx);
    s.musicOn = e_.busEnabled(audio::Bus::Music);
    s.sfxOn = e_.busEnabled(audio::Bus::Sfx);
    return s;
}

} // namespace aiverse::game
