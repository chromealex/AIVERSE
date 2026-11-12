// Deep links → the engine's "links" protocol (docs/plugins.md) on iOS: URL schemes (Info.plist
// CFBundleURLTypes from plugin.json) and universal links. The engine's app delegate does not forward
// openURL / continueUserActivity to plugins, so this plugin adds the two UIApplicationDelegate
// methods to the delegate's class at load time (or wraps existing ones), the way attribution SDKs do.
// Universal links also need the Associated Domains entitlement (applinks:<host>) in the Xcode project.
#import <UIKit/UIKit.h>
#import <objc/runtime.h>
#import "engine/platform/apple/AiversePlugin.h"

static NSString* gLaunchUrl = @"";
static BOOL gReady = NO;
static NSMutableArray<NSString*>* gEarly;   // links before the game called init

static void deliver(NSURL* url) {
    if (!url) return;
    NSString* s = url.absoluteString ?: @"";
    if (!gReady) {
        if (!gEarly) gEarly = [NSMutableArray array];
        [gEarly addObject:s];
        if (gLaunchUrl.length == 0) gLaunchUrl = s;
        return;
    }
    AiversePluginEmit(@"deeplinks", @"link", @{@"url": s});
}

static BOOL (*gOrigOpen)(id, SEL, UIApplication*, NSURL*, NSDictionary*) = NULL;
static BOOL (*gOrigActivity)(id, SEL, UIApplication*, NSUserActivity*, id) = NULL;

static BOOL aiverseOpenURL(id self, SEL sel, UIApplication* app, NSURL* url, NSDictionary* opts) {
    deliver(url);
    return gOrigOpen ? gOrigOpen(self, sel, app, url, opts) : YES;
}

static BOOL aiverseContinue(id self, SEL sel, UIApplication* app, NSUserActivity* activity, id handler) {
    if ([activity.activityType isEqualToString:NSUserActivityTypeBrowsingWeb]) deliver(activity.webpageURL);
    return gOrigActivity ? gOrigActivity(self, sel, app, activity, handler) : YES;
}

static void hook(Class cls, SEL sel, IMP imp, void** orig, const char* types) {
    Method m = class_getInstanceMethod(cls, sel);
    if (m) *orig = (void*)method_setImplementation(m, imp);
    else class_addMethod(cls, sel, imp, types);
}

@interface AiverseDeepLinksPlugin : NSObject <AiversePlugin>
@end

@implementation AiverseDeepLinksPlugin

+ (void)load {
    // the delegate exists once the app finished launching; launch URLs arrive in the options too
    [[NSNotificationCenter defaultCenter] addObserverForName:UIApplicationDidFinishLaunchingNotification object:nil queue:nil
                                                  usingBlock:^(NSNotification* n) {
        NSURL* url = n.userInfo[UIApplicationLaunchOptionsURLKey];
        if (url) deliver(url);
        Class cls = [UIApplication.sharedApplication.delegate class];
        if (!cls) return;
        hook(cls, @selector(application:openURL:options:), (IMP)aiverseOpenURL, (void**)&gOrigOpen, "B@:@@@");
        hook(cls, @selector(application:continueUserActivity:restorationHandler:), (IMP)aiverseContinue, (void**)&gOrigActivity, "B@:@@@?");
    }];
}

- (void)call:(NSString*)method args:(NSDictionary*)args reply:(AiversePluginReply*)reply {
    if ([method isEqualToString:@"init"]) {
        gReady = YES;
        NSArray* early = gEarly;
        gEarly = nil;
        for (NSString* s in early) if (![s isEqualToString:gLaunchUrl]) AiversePluginEmit(@"deeplinks", @"link", @{@"url": s});
        [reply ok:nil];
    } else if ([method isEqualToString:@"initial"]) {
        [reply ok:@{@"url": gLaunchUrl}];
    } else if ([method isEqualToString:@"lifecycle"]) {
        [reply ok:nil];
    } else {
        [reply fail:[@"deeplinks: unknown method " stringByAppendingString:method]];
    }
}

@end
