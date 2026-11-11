// Plugin backend for iOS and macOS: classes from Info.plist "AiversePlugins", called on the main thread.
#import "AiversePlugin.h"
#include "../Plugins.h"

#include <TargetConditionals.h>
#if TARGET_OS_IPHONE
#import <UIKit/UIKit.h>
#endif

#include <string>

@interface AiversePluginReply ()
- (instancetype)initWithCallId:(uint64_t)callId;
@end

namespace {
std::string toStd(NSString* s) {
    const char* u = s ? s.UTF8String : nullptr;
    return u ? std::string(u) : std::string();
}

std::string toJson(id data) {
    if (!data || data == NSNull.null) return "null";
    NSError* err = nil;
    NSData* d = [NSJSONSerialization dataWithJSONObject:data options:NSJSONWritingFragmentsAllowed error:&err];
    if (!d) return "null";
    return std::string(static_cast<const char*>(d.bytes), d.length);
}

NSString* classNameFor(const std::string& name) {
    NSDictionary* map = [NSBundle.mainBundle objectForInfoDictionaryKey:@"AiversePlugins"];
    id cls = [map isKindOfClass:NSDictionary.class] ? map[[NSString stringWithUTF8String:name.c_str()]] : nil;
    return [cls isKindOfClass:NSString.class] ? cls : nil;
}

NSMutableDictionary<NSString*, id<AiversePlugin>>* instances() {
    static NSMutableDictionary* d = [NSMutableDictionary new];
    return d;
}

struct AppleBackend : aiverse::plugins::detail::Backend {
    bool available(const std::string& name) override {
        NSString* cls = classNameFor(name);
        return cls && NSClassFromString(cls) != nil;
    }
    void call(const std::string& name, const std::string& method, const std::string& args, uint64_t callId) override {
        NSString* n = [NSString stringWithUTF8String:name.c_str()];
        NSString* m = [NSString stringWithUTF8String:method.c_str()];
        NSData* a = [NSData dataWithBytes:args.data() length:args.size()];
        dispatch_async(dispatch_get_main_queue(), ^{ // never re-enter the game inside call()
            AiversePluginReply* reply = [[AiversePluginReply alloc] initWithCallId:callId];
            id<AiversePlugin> p = instances()[n];
            if (!p) {
                Class cls = NSClassFromString(classNameFor(toStd(n)));
                if (![cls conformsToProtocol:@protocol(AiversePlugin)]) { [reply fail:[NSString stringWithFormat:@"plugin '%@' is not in this build", n]]; return; }
                p = [cls new];
                instances()[n] = p;
            }
            NSDictionary* dict = [NSJSONSerialization JSONObjectWithData:a options:0 error:nil];
            if (![dict isKindOfClass:NSDictionary.class]) dict = @{};
            @try {
                [p call:m args:dict reply:reply];
            } @catch (NSException* e) {
                [reply fail:[NSString stringWithFormat:@"%@.%@: %@", n, m, e.reason]];
            }
        });
    }
};
AppleBackend g_backend;

// Installed when the runtime loads; the Apple platform files run before any plugin call.
struct Install { Install() { aiverse::plugins::detail::setBackend(&g_backend); } } g_install;
} // namespace

@implementation AiversePluginReply {
    uint64_t _callId;
    BOOL _done;
}
- (instancetype)initWithCallId:(uint64_t)callId { if ((self = [super init])) _callId = callId; return self; }
- (void)ok:(id)data {
    @synchronized (self) { if (_done) return; _done = YES; }
    aiverse::plugins::detail::completeCall(_callId, true, toJson(data));
}
- (void)fail:(NSString*)error {
    @synchronized (self) { if (_done) return; _done = YES; }
    aiverse::plugins::detail::completeCall(_callId, false, toStd(error.length ? error : @"error"));
}
@end

void AiversePluginEmit(NSString* plugin, NSString* event, id data) {
    aiverse::plugins::emit(toStd(plugin), toStd(event), toJson(data));
}

id AiversePluginPresentingViewController(void) {
#if TARGET_OS_IPHONE
    UIViewController* vc = nil;
    for (UIScene* s in UIApplication.sharedApplication.connectedScenes)
        if ([s isKindOfClass:UIWindowScene.class])
            for (UIWindow* w in ((UIWindowScene*)s).windows) if (w.isKeyWindow) vc = w.rootViewController;
    if (!vc) vc = UIApplication.sharedApplication.delegate.window.rootViewController;
    while (vc.presentedViewController) vc = vc.presentedViewController;
    return vc;
#else
    return nil;
#endif
}
