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
bool g_tapInstalled = false;
std::atomic<bool> g_recording{false};
std::atomic<int> g_gen{0}; // recognizer callbacks from replaced tasks are ignored
unsigned g_session = 0;
int g_emptyTasks = 0;

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
    g_recording = false;
    if (g_engine) [g_engine stop];
    if (g_tapInstalled) { [g_engine.inputNode removeTapOnBus:0]; g_tapInstalled = false; }
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
    ++g_gen;
    shutdown();
}

void newTask();

void taskEnded(int gen) {   // main thread: a task ended by itself
    if (gen != g_gen) return;
    {
        std::lock_guard<std::mutex> lk(g_m);
        g_emptyTasks = g_part.empty() ? g_emptyTasks + 1 : 0;
        g_done = joined();
        g_part.clear();
    }
    if (g_listening && g_emptyTasks < 4) { newTask(); return; }   // still dictating: go on
    if (g_listening && g_emptyTasks >= 4) {
        std::lock_guard<std::mutex> lk(g_m);
        if (g_done.empty()) g_err = "ничего не распознано: проверьте микрофон (Системные настройки → Звук → Вход)";
    }
    g_emptyTasks = 0;
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
    if (format.sampleRate <= 0 || format.channelCount == 0) { fail("нет микрофона"); g_engine = nil; return; }
    newTask();
    [input installTapOnBus:0 bufferSize:2048 format:format block:^(AVAudioPCMBuffer* buffer, AVAudioTime*) {
        std::lock_guard<std::mutex> lk(g_m);
        if (g_request) [g_request appendAudioPCMBuffer:buffer];
    }];
    g_tapInstalled = true;
    [g_engine prepare];
    NSError* err = nil;
    if (![g_engine startAndReturnError:&err]) {
        fail(std::string("микрофон не запустился: ") + (err ? err.localizedDescription.UTF8String : "?"));
        ++g_gen;
        shutdown();
    } else g_recording = true;
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
    g_emptyTasks = 0;
    g_hasResult = false;
    g_listening = true;
    const std::string loc = locale;
    const auto session = ++g_session;
    [SFSpeechRecognizer requestAuthorization:^(SFSpeechRecognizerAuthorizationStatus status) {
        dispatch_async(dispatch_get_main_queue(), ^{
            if (session != g_session || !g_listening) return;
            if (status != SFSpeechRecognizerAuthorizationStatusAuthorized) {
                fail("Speech recognition access is required. Check Privacy & Security in System Settings.");
                return;
            }
            [AVCaptureDevice requestAccessForMediaType:AVMediaTypeAudio completionHandler:^(BOOL granted) {
                dispatch_async(dispatch_get_main_queue(), ^{
                    if (session != g_session || !g_listening) return;
                    if (granted) begin(loc);
                    else fail("Microphone access is required. Check Privacy & Security in System Settings.");
                });
            }];
        });
    }];
}

void cancel() {
    ++g_session;
    ++g_gen;
    g_listening = false;
    g_finishing = false;
    g_hasResult = false;
    shutdown();
}

void stop() {
    if (!g_listening) return;
    g_finishing = true;
    g_listening = false;
    g_recording = false;
    if (g_engine) [g_engine stop];
    if (g_tapInstalled) { [g_engine.inputNode removeTapOnBus:0]; g_tapInstalled = false; }
    SFSpeechAudioBufferRecognitionRequest* request = nil;
    { std::lock_guard<std::mutex> lock(g_m); request = g_request; }
    if (!request) { finish(); return; }
    [request endAudio];
    const auto session = g_session;
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 3 * NSEC_PER_SEC), dispatch_get_main_queue(), ^{
        if (session == g_session && g_finishing) finish();
    });
}

bool recording() { return g_recording; }
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
