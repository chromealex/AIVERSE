// Dictation on macOS (EditorSpeech.h): SFSpeechRecognizer fed by AVAudioEngine's microphone tap, partial results live.
// A recognition task ends on its own after a long pause or about a minute of speech: while the user is still dictating a
// new task is started on the same microphone and the texts are joined. Words to expect (game names, genre terms) go to
// contextualStrings, which helps a lot with English names inside Russian speech.
// Needs NSMicrophoneUsageDescription and NSSpeechRecognitionUsageDescription in the editor's Info.plist.
#include "EditorSpeech.h"

#import <AVFoundation/AVFoundation.h>
#import <Foundation/Foundation.h>
#import <Speech/Speech.h>

#include <atomic>
#include <mutex>

namespace aiverse::editor::speech {

namespace {
std::mutex g_m;
std::string g_done, g_part, g_err, g_result;   // g_done: texts of finished tasks; g_part: the current task's text
std::atomic<bool> g_listening{false}, g_finishing{false}, g_hasResult{false};
AVAudioEngine* g_engine = nil;
SFSpeechRecognizer* g_recognizer = nil;
SFSpeechAudioBufferRecognitionRequest* g_request = nil;   // guarded by g_m (the tap thread appends to it)
SFSpeechRecognitionTask* g_task = nil;
NSArray<NSString*>* g_vocab = nil;
int g_gen = 0;   // task generation: callbacks of a replaced task are ignored

std::string joined() {   // under g_m
    if (g_done.empty()) return g_part;
    if (g_part.empty()) return g_done;
    return g_done + " " + g_part;
}

void fail(const std::string& e) {
    std::lock_guard<std::mutex> lk(g_m);
    g_err = e;
    g_listening = false;
    g_finishing = false;
}

void shutdown() {   // main thread
    if (g_engine) { [g_engine stop]; [g_engine.inputNode removeTapOnBus:0]; }
    if (g_task) [g_task cancel];
    g_engine = nil; g_task = nil;
    std::lock_guard<std::mutex> lk(g_m);
    g_request = nil;
}

void finish() {   // the text so far is the result
    {
        std::lock_guard<std::mutex> lk(g_m);
        g_result = joined();
        g_hasResult = true;
    }
    g_listening = false;
    g_finishing = false;
    dispatch_async(dispatch_get_main_queue(), ^{ shutdown(); });
}

void newTask();

void taskEnded(int gen) {   // main thread: a task ended by itself
    if (gen != g_gen) return;
    static int empty = 0;   // tasks in a row that ended with nothing heard: a broken recognizer must not spin
    {
        std::lock_guard<std::mutex> lk(g_m);
        empty = g_part.empty() ? empty + 1 : 0;
        g_done = joined();
        g_part.clear();
    }
    if (g_listening && empty < 4) { newTask(); return; }   // still dictating: go on
    if (g_listening && empty >= 4) {
        std::lock_guard<std::mutex> lk(g_m);
        if (g_done.empty()) g_err = "ничего не распознано: проверьте микрофон (Системные настройки → Звук → Вход)";
    }
    empty = 0;
    finish();
}

void newTask() {   // main thread
    SFSpeechAudioBufferRecognitionRequest* request = [[SFSpeechAudioBufferRecognitionRequest alloc] init];
    request.shouldReportPartialResults = YES;
    request.taskHint = SFSpeechRecognitionTaskHintDictation;
    if (g_vocab.count) request.contextualStrings = g_vocab;
    if (@available(macOS 13.0, *)) request.addsPunctuation = YES;
    {
        std::lock_guard<std::mutex> lk(g_m);
        g_request = request;
    }
    const int gen = ++g_gen;
    g_task = [g_recognizer recognitionTaskWithRequest:request resultHandler:^(SFSpeechRecognitionResult* result, NSError* error) {
        if (result) {
            std::lock_guard<std::mutex> lk(g_m);
            if (gen == g_gen) g_part = result.bestTranscription.formattedString.UTF8String;
        }
        if (error || (result && result.isFinal)) dispatch_async(dispatch_get_main_queue(), ^{ taskEnded(gen); });
    }];
}

void begin(const std::string& locale) {   // main thread
    g_recognizer = [[SFSpeechRecognizer alloc] initWithLocale:[NSLocale localeWithLocaleIdentifier:@(locale.c_str())]];
    if (!g_recognizer || !g_recognizer.available) { fail("распознавание речи для «" + locale + "» сейчас недоступно"); return; }
    g_recognizer.defaultTaskHint = SFSpeechRecognitionTaskHintDictation;
    g_engine = [[AVAudioEngine alloc] init];
    AVAudioInputNode* input = g_engine.inputNode;
    AVAudioFormat* format = [input outputFormatForBus:0];
    if (format.sampleRate <= 0) { fail("нет микрофона"); g_engine = nil; return; }
    newTask();
    [input installTapOnBus:0 bufferSize:2048 format:format block:^(AVAudioPCMBuffer* buffer, AVAudioTime*) {
        std::lock_guard<std::mutex> lk(g_m);
        if (g_request) [g_request appendAudioPCMBuffer:buffer];
    }];
    [g_engine prepare];
    NSError* err = nil;
    if (![g_engine startAndReturnError:&err]) {
        fail(std::string("микрофон не запустился: ") + (err ? err.localizedDescription.UTF8String : "?"));
        shutdown();
    }
}
} // namespace

bool supported() { return true; }

void start(const std::string& locale, const std::vector<std::string>& vocabulary) {
    if (g_listening || g_finishing) return;
    {
        std::lock_guard<std::mutex> lk(g_m);
        g_done.clear(); g_part.clear(); g_err.clear(); g_result.clear();
    }
    NSMutableArray<NSString*>* vocab = [NSMutableArray array];
    for (const auto& w : vocabulary) [vocab addObject:@(w.c_str())];
    g_vocab = vocab;
    g_hasResult = false;
    g_listening = true;
    const std::string loc = locale;
    [SFSpeechRecognizer requestAuthorization:^(SFSpeechRecognizerAuthorizationStatus status) {
        if (status != SFSpeechRecognizerAuthorizationStatusAuthorized) {
            fail("нет доступа к распознаванию речи: Системные настройки → Конфиденциальность и безопасность → Распознавание речи → AIVERSE Editor");
            return;
        }
        [AVCaptureDevice requestAccessForMediaType:AVMediaTypeAudio completionHandler:^(BOOL granted) {
            if (!granted) { fail("нет доступа к микрофону: Системные настройки → Конфиденциальность и безопасность → Микрофон → AIVERSE Editor"); return; }
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
        SFSpeechAudioBufferRecognitionRequest* r = nil;
        {
            std::lock_guard<std::mutex> lk(g_m);
            r = g_request;
        }
        if (r) [r endAudio];   // the task gives its final text, then taskEnded() -> finish()
        else finish();         // nothing started (permissions pending)
    });
}

bool busy() { return g_listening || g_finishing; }
bool listening() { return g_listening; }
std::string transcript() { std::lock_guard<std::mutex> lk(g_m); return joined(); }
std::string error() { std::lock_guard<std::mutex> lk(g_m); return g_err; }

bool takeResult(std::string& out) {
    if (!g_hasResult) return false;
    std::lock_guard<std::mutex> lk(g_m);
    out = g_result;
    g_hasResult = false;
    return true;
}

} // namespace aiverse::editor::speech
