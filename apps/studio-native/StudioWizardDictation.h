#pragma once
#include "apps/editor/EditorSpeech.h"
#include "engine/data/Json.h"
#include <stdexcept>

namespace aiverse::studio {

class StudioWizardDictation {
public:
    void cancel() {
        if (!owner_.empty()) editor::speech::cancel();
        owner_.clear();
    }

    Json handle(const Json& params) {
        namespace speech = editor::speech;
        const auto action = params.getStr("action");
        const auto owner = params.getStr("owner");
        Json reply = Json::object();
        reply.set("supported", Json::fromBool(speech::supported()));
        reply.set("phase", Json::string("idle"));
        if (action == "available") return reply;
        if (owner.empty() || owner.size() > 96) throw std::runtime_error("Invalid dictation session");
        if (action == "start") {
            if (!speech::supported()) throw std::runtime_error("Use system dictation in this text field");
            if (!owner_.empty() || speech::busy()) throw std::runtime_error("Another field is using dictation");
            owner_ = owner;
            speech::start(speech::localeForLanguage(params.getStr("language", "en")), speech::pitchVocabulary());
        } else if (action != "poll" && action != "stop" && action != "cancel") {
            throw std::runtime_error("Unknown dictation action");
        }
        if (owner_ != owner) return reply;
        if (action == "cancel") { cancel(); return reply; }
        if (action == "stop") speech::stop();
        if (speech::busy()) {
            const char* phase = speech::recording() ? "listening" : speech::listening() ? "starting" : "finishing";
            reply.set("phase", Json::string(phase));
            reply.set("text", Json::string(speech::transcript()));
        } else {
            std::string text;
            speech::takeResult(text);
            reply.set("phase", Json::string("done"));
            reply.set("text", Json::string(text));
            reply.set("error", Json::string(speech::error()));
            owner_.clear();
        }
        return reply;
    }
private:
    std::string owner_;
};
}
