// Facebook / Meta iOS SDK (FBSDKCoreKit 18) → the engine's "analytics" protocol (docs/plugins.md).
// FacebookAppID / FacebookClientToken / auto-logging switches come from Info.plist (aiversebuild).
// Since SDK 17 the advertiser tracking flag follows ATTrackingManager by itself: the "att" plugin's
// prompt is what enables IDFA-based attribution, nothing to set here.
#import <UIKit/UIKit.h>
#import <FBSDKCoreKit/FBSDKCoreKit.h>
#import <FBSDKCoreKit/FBSDKCoreKit-Swift.h>   // Swift classes: FBSDKApplicationDelegate, FBSDKSettings
#import "engine/platform/apple/AiversePlugin.h"

@interface AiverseFacebookPlugin : NSObject <AiversePlugin>
@end

static NSDictionary* AiverseFBParams(id v, NSNumber** valueToSum) {
    NSMutableDictionary* r = [NSMutableDictionary dictionary];
    if (![v isKindOfClass:NSDictionary.class]) return r;
    [(NSDictionary*)v enumerateKeysAndObjectsUsingBlock:^(NSString* k, id obj, BOOL*) {
        if ([k isEqualToString:@"_valueToSum"] && [obj isKindOfClass:NSNumber.class]) { if (valueToSum) *valueToSum = obj; return; }
        if ([obj isKindOfClass:NSString.class] || [obj isKindOfClass:NSNumber.class]) r[k] = obj;
    }];
    return r;
}

@implementation AiverseFacebookPlugin {
    BOOL _started;
    BOOL _consent;
}

- (void)call:(NSString*)method args:(NSDictionary*)args reply:(AiversePluginReply*)reply {
    if ([method isEqualToString:@"init"]) {
        // The plugin starts after launch: initialise the SDK now and count this activation (later ones
        // the SDK logs itself on applicationDidBecomeActive).
        [FBSDKApplicationDelegate.sharedInstance application:UIApplication.sharedApplication didFinishLaunchingWithOptions:@{}];
        if (FBSDKSettings.sharedSettings.isAutoLogAppEventsEnabled) [FBSDKAppEvents.shared activateApp];
        _started = YES;
        _consent = YES;
        [reply ok:nil];
        return;
    }
    if ([method isEqualToString:@"lifecycle"] || [method isEqualToString:@"design"] || [method isEqualToString:@"progression"] ||
        [method isEqualToString:@"resource"]) { [reply ok:nil]; return; }   // no App Events equivalent
    if ([method isEqualToString:@"consent"]) {
        _consent = args[@"analytics"] ? [args[@"analytics"] boolValue] : YES;
        BOOL tracking = args[@"tracking"] ? [args[@"tracking"] boolValue] : YES;
        FBSDKSettings.sharedSettings.isAutoLogAppEventsEnabled = _consent;
        FBSDKSettings.sharedSettings.isAdvertiserIDCollectionEnabled = _consent && tracking;
        [FBSDKSettings.sharedSettings setDataProcessingOptions:(_consent ? @[] : @[@"LDU"]) country:0 state:0];
        [reply ok:nil];
        return;
    }
    if ([method isEqualToString:@"user"]) {
        if ([args[@"id"] isKindOfClass:NSString.class]) FBSDKAppEvents.shared.userID = [args[@"id"] length] ? args[@"id"] : nil;
        [reply ok:nil];
        return;
    }
    if ([method isEqualToString:@"flush"]) { [FBSDKAppEvents.shared flush]; [reply ok:nil]; return; }
    if ([method isEqualToString:@"deferredLink"]) {
        [FBSDKAppLinkUtility fetchDeferredAppLink:^(NSURL* url, NSError* error) {
            [reply ok:url ? @{@"url": url.absoluteString} : @{}];
        }];
        return;
    }
    if (!_started) { [reply fail:@"facebook: not initialized"]; return; }
    if (!_consent) { [reply ok:nil]; return; }
    NSNumber* valueToSum = nil;
    NSMutableDictionary* params = [AiverseFBParams(args[@"fields"], &valueToSum) mutableCopy];
    if ([method isEqualToString:@"event"]) {
        if (valueToSum) [FBSDKAppEvents.shared logEvent:args[@"name"] valueToSum:valueToSum.doubleValue parameters:params];
        else [FBSDKAppEvents.shared logEvent:args[@"name"] parameters:params];
    } else if ([method isEqualToString:@"purchase"]) {
        if ([args[@"itemId"] length]) params[@"fb_content_id"] = args[@"itemId"];
        if ([args[@"itemType"] length]) params[@"fb_content_type"] = args[@"itemType"];
        double amount = [args[@"priceMicros"] doubleValue] / 1e6;
        [FBSDKAppEvents.shared logPurchase:amount currency:args[@"currency"] parameters:params];
    } else {
        [reply fail:[@"facebook: unknown method " stringByAppendingString:method]];
        return;
    }
    [reply ok:nil];
}

@end
