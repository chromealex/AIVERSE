// Dictation on macOS (EditorSpeech.h): SFSpeechRecognizer fed by AVAudioEngine's microphone tap, partial results live.
// Needs NSMicrophoneUsageDescription and NSSpeechRecognitionUsageDescription in the editor's Info.plist.
#include "EditorSpeech.h"

#import <AVFoundation/AVFoundation.h>
#import <Foundation/Foundation.h>
#import <Speech/Speech.h>

#include <atomic>
#include <mutex>

namespace rts::editor::speech {

namespace {
std::mutex g_m;
std::string g_text, g_err, g_result;
std::atomic<bool> g_listening{false}, g_finishing{false}, g_hasResult{false};
AVAudioEngine* g_engine = nil;
SFSpeechRecognizer* g_recognizer = nil;
SFSpeechAudioBufferRecognitionRequest* g_request = nil;
SFSpeechRecognitionTask* g_task = nil;

void fail(const std::string& e) {
    std::lock_guard<std::mutex> lk(g_m);
    g_err = e;
    g_listening = false;
    g_finishing = false;
}

void finish() {   // the task ended (final result or error): the text so far is the result
    {
        std::lock_guard<std::mutex> lk(g_m);
        g_result = g_text;
        g_hasResult = true;
    }
    g_listening = false;
    g_finishing = false;
    dispatch_async(dispatch_get_main_queue(), ^{
        if (g_engine) { [g_engine stop]; [g_engine.inputNode removeTapOnBus:0]; }
        g_engine = nil; g_request = nil; g_task = nil;
    });
}

void begin(const std::string& locale) {
    g_recognizer = [[SFSpeechRecognizer alloc] initWithLocale:[NSLocale localeWithLocaleIdentifier:@(locale.c_str())]];
    if (!g_recognizer || !g_recognizer.available) { fail("распознавание речи для «" + locale + "» сейчас недоступно"); return; }
    g_request = [[SFSpeechAudioBufferRecognitionRequest alloc] init];
    g_request.shouldReportPartialResults = YES;
    if (@available(macOS 13.0, *)) g_request.addsPunctuation = YES;
    g_engine = [[AVAudioEngine alloc] init];
    AVAudioInputNode* input = g_engine.inputNode;
    AVAudioFormat* format = [input outputFormatForBus:0];
    if (format.sampleRate <= 0) { fail("нет микрофона"); return; }
    SFSpeechAudioBufferRecognitionRequest* request = g_request;
    [input installTapOnBus:0 bufferSize:1024 format:format block:^(AVAudioPCMBuffer* buffer, AVAudioTime*) { [request appendAudioPCMBuffer:buffer]; }];
    [g_engine prepare];
    NSError* err = nil;
    if (![g_engine startAndReturnError:&err]) {
        fail(std::string("микрофон не запустился: ") + (err ? err.localizedDescription.UTF8String : "?"));
        [input removeTapOnBus:0];
        return;
    }
    g_task = [g_recognizer recognitionTaskWithRequest:g_request resultHandler:^(SFSpeechRecognitionResult* result, NSError* error) {
        if (result) {
            std::lock_guard<std::mutex> lk(g_m);
            g_text = result.bestTranscription.formattedString.UTF8String;
        }
        if (error || (result && result.isFinal)) finish();
    }];
}
} // namespace

bool supported() { return true; }

void start(const std::string& locale) {
    if (g_listening || g_finishing) return;
    {
        std::lock_guard<std::mutex> lk(g_m);
        g_text.clear(); g_err.clear(); g_result.clear();
    }
    g_hasResult = false;
    g_listening = true;
    const std::string loc = locale;
    [SFSpeechRecognizer requestAuthorization:^(SFSpeechRecognizerAuthorizationStatus status) {
        if (status != SFSpeechRecognizerAuthorizationStatusAuthorized) {
            fail("нет доступа к распознаванию речи: Системные настройки → Конфиденциальность и безопасность → Распознавание речи → RTS Editor");
            return;
        }
        [AVCaptureDevice requestAccessForMediaType:AVMediaTypeAudio completionHandler:^(BOOL granted) {
            if (!granted) { fail("нет доступа к микрофону: Системные настройки → Конфиденциальность и безопасность → Микрофон → RTS Editor"); return; }
            dispatch_async(dispatch_get_main_queue(), ^{ if (g_listening) begin(loc); });
        }];
    }];
}

void stop() {
    if (!g_listening) return;
    g_finishing = true;
    g_listening = false;
    dispatch_async(dispatch_get_main_queue(), ^{
        if (g_engine) { [g_engine stop]; [g_engine.inputNode removeTapOnBus:0]; }
        if (g_request) [g_request endAudio];
        if (!g_task) finish();   // nothing started (permissions pending): end now
    });
}

bool busy() { return g_listening || g_finishing; }
bool listening() { return g_listening; }
std::string transcript() { std::lock_guard<std::mutex> lk(g_m); return g_text; }
std::string error() { std::lock_guard<std::mutex> lk(g_m); return g_err; }

bool takeResult(std::string& out) {
    if (!g_hasResult) return false;
    std::lock_guard<std::mutex> lk(g_m);
    out = g_result;
    g_hasResult = false;
    return true;
}

} // namespace rts::editor::speech
