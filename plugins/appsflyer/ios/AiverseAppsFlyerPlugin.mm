// AppsFlyer SDK 6.15 → the engine's "analytics" and "links" protocols (docs/plugins.md) on iOS.
// Same mapping as the Android plugin (AppsFlyerPlugin.java). The SDK must be started on every
// UIApplicationDidBecomeActive; waitForATTUserAuthorizationWithTimeoutInterval lets the attribution
// wait for the App Tracking Transparency answer (the "att" privacy plugin asks).
#import <UIKit/UIKit.h>
#import <AppsFlyerLib/AppsFlyerLib.h>
#import <objc/runtime.h>
#import "engine/platform/apple/AiversePlugin.h"

// OneLink universal links / URL schemes must reach the SDK: the engine's app delegate does not forward
// them, so the two delegate methods are wrapped (chained with the deeplinks plugin's wrappers).
static BOOL (*gAfOrigOpen)(id, SEL, UIApplication*, NSURL*, NSDictionary*) = NULL;
static BOOL (*gAfOrigActivity)(id, SEL, UIApplication*, NSUserActivity*, id) = NULL;
static BOOL afOpenURL(id self, SEL sel, UIApplication* app, NSURL* url, NSDictionary* opts) {
    [AppsFlyerLib.shared handleOpenUrl:url options:opts];
    return gAfOrigOpen ? gAfOrigOpen(self, sel, app, url, opts) : YES;
}
static BOOL afContinue(id self, SEL sel, UIApplication* app, NSUserActivity* activity, id handler) {
    [AppsFlyerLib.shared continueUserActivity:activity restorationHandler:nil];
    return gAfOrigActivity ? gAfOrigActivity(self, sel, app, activity, handler) : YES;
}
static void afHook(Class cls, SEL sel, IMP imp, void** orig, const char* types) {
    Method m = class_getInstanceMethod(cls, sel);
    if (m) *orig = (void*)method_setImplementation(m, imp);
    else class_addMethod(cls, sel, imp, types);
}

@interface AiverseAppsFlyerPlugin : NSObject <AiversePlugin, AppsFlyerDeepLinkDelegate>
@end

@implementation AiverseAppsFlyerPlugin {
    BOOL _started, _resolved, _deferred;
    NSString* _url;
    NSDictionary* _data;
    NSMutableArray<AiversePluginReply*>* _waiting;
}

+ (void)load {
    [[NSNotificationCenter defaultCenter] addObserverForName:UIApplicationDidFinishLaunchingNotification object:nil queue:nil
                                                  usingBlock:^(NSNotification*) {
        Class cls = [UIApplication.sharedApplication.delegate class];
        if (!cls) return;
        afHook(cls, @selector(application:openURL:options:), (IMP)afOpenURL, (void**)&gAfOrigOpen, "B@:@@@");
        afHook(cls, @selector(application:continueUserActivity:restorationHandler:), (IMP)afContinue, (void**)&gAfOrigActivity, "B@:@@@?");
    }];
}

static NSDictionary* fields(NSDictionary* d) { return [d isKindOfClass:NSDictionary.class] ? d : @{}; }

- (void)call:(NSString*)method args:(NSDictionary*)args reply:(AiversePluginReply*)reply {
    AppsFlyerLib* af = AppsFlyerLib.shared;
    if ([method isEqualToString:@"init"]) {
        [self start:args];
        [reply ok:nil];
    } else if ([method isEqualToString:@"event"]) {
        [af logEvent:args[@"name"] withValues:fields(args[@"fields"])];
        [reply ok:nil];
    } else if ([method isEqualToString:@"design"]) {
        NSMutableDictionary* v = [fields(args[@"fields"]) mutableCopy];
        if (args[@"value"]) v[@"value"] = args[@"value"];
        [af logEvent:[args[@"id"] stringByReplacingOccurrencesOfString:@":" withString:@"_"] withValues:v];
        [reply ok:nil];
    } else if ([method isEqualToString:@"progression"]) {
        if ([args[@"status"] isEqualToString:@"complete"]) {
            NSMutableDictionary* v = [fields(args[@"fields"]) mutableCopy];
            v[AFEventParamLevel] = [args[@"path"] componentsJoinedByString:@":"] ?: @"";
            if (args[@"score"]) v[AFEventParamScore] = args[@"score"];
            [af logEvent:AFEventLevelAchieved withValues:v];
        }
        [reply ok:nil];
    } else if ([method isEqualToString:@"purchase"]) {
        NSMutableDictionary* v = [fields(args[@"fields"]) mutableCopy];
        v[AFEventParamRevenue] = @([args[@"priceMicros"] doubleValue] / 1e6);
        v[AFEventParamCurrency] = args[@"currency"] ?: @"";
        v[AFEventParamContentId] = args[@"itemId"] ?: @"";
        v[AFEventParamContentType] = args[@"itemType"] ?: @"";
        NSDictionary* receipt = fields(args[@"receipt"]);
        id order = receipt[@"transactionId"] ?: receipt[@"token"];
        if (order) v[AFEventParamOrderId] = order;
        [af logEvent:AFEventPurchase withValues:v];
        [reply ok:nil];
    } else if ([method isEqualToString:@"user"]) {
        if (args[@"id"]) af.customerUserID = args[@"id"];
        if (args[@"properties"]) [af setAdditionalData:fields(args[@"properties"])];
        [reply ok:nil];
    } else if ([method isEqualToString:@"consent"]) {
        BOOL analytics = args[@"analytics"] ? [args[@"analytics"] boolValue] : YES;
        BOOL tracking = args[@"tracking"] ? [args[@"tracking"] boolValue] : YES;
        [af setConsentData:[[AppsFlyerConsent alloc] initForGDPRUserWithHasConsentForDataUsage:analytics hasConsentForAdsPersonalization:tracking]];
        af.anonymizeUser = !analytics;
        [reply ok:nil];
    } else if ([method isEqualToString:@"initial"] || [method isEqualToString:@"deferredLink"]) {
        if (_resolved) [self answer:reply];
        else [_waiting addObject:reply];
    } else if ([method isEqualToString:@"resource"] || [method isEqualToString:@"flush"] || [method isEqualToString:@"lifecycle"]) {
        [reply ok:nil];
    } else {
        [reply fail:[@"appsflyer: unknown method " stringByAppendingString:method]];
    }
}

- (void)start:(NSDictionary*)cfg {
    if (_started) return;
    _started = YES;
    _waiting = [NSMutableArray array];
    _url = @"";
    _data = @{};
    AppsFlyerLib* af = AppsFlyerLib.shared;
    af.appsFlyerDevKey = cfg[@"devKey"] ?: @"";
    af.appleAppID = [cfg[@"appId"] description] ?: @"";
    af.isDebug = [cfg[@"debug"] boolValue];
    af.deepLinkDelegate = self;
    if ([cfg[@"oneLinkDomains"] isKindOfClass:NSArray.class]) af.oneLinkCustomDomains = cfg[@"oneLinkDomains"];
    [af waitForATTUserAuthorizationWithTimeoutInterval:cfg[@"waitForATT"] ? [cfg[@"waitForATT"] doubleValue] : 60];
    [af start];
    [[NSNotificationCenter defaultCenter] addObserverForName:UIApplicationDidBecomeActiveNotification object:nil queue:nil
                                                  usingBlock:^(NSNotification*) { [AppsFlyerLib.shared start]; }];
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 10 * NSEC_PER_SEC), dispatch_get_main_queue(), ^{ [self resolve]; });
}

- (void)didResolveDeepLink:(AppsFlyerDeepLinkResult*)result {
    if (result.status == AFSDKDeepLinkResultStatusFound && result.deepLink) {
        AppsFlyerDeepLink* d = result.deepLink;
        NSDictionary* click = d.clickEvent ?: @{};
        NSString* url = click[@"link"] ?: d.deeplinkValue ?: @"";
        if (!_resolved) { _url = url; _deferred = d.isDeferred; _data = click; }
        AiversePluginEmit(@"appsflyer", @"link", @{@"url": url, @"deferred": @(d.isDeferred), @"data": click});
    }
    [self resolve];
}

- (void)resolve {
    if (_resolved) return;
    _resolved = YES;
    for (AiversePluginReply* r in _waiting) [self answer:r];
    [_waiting removeAllObjects];
}

- (void)answer:(AiversePluginReply*)reply {
    [reply ok:@{@"url": _url ?: @"", @"deferred": @(_deferred), @"data": _data ?: @{}}];
}

@end
