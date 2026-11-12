// App Tracking Transparency (iOS 14+) → the engine's "privacy" protocol (docs/plugins.md).
// The prompt only appears while the app is active (since iOS 17.4 a request made earlier is answered
// "denied" without a dialog), so a request waits for UIApplicationDidBecomeActive when needed.
// Config: prompt "ready" asks `delay` seconds after the first interactive frame (Drone Fall asks 2 s
// after its root screen) and sends the answer as a "status" event; "manual" waits for "request".
#import <AppTrackingTransparency/AppTrackingTransparency.h>
#import <AdSupport/AdSupport.h>
#import <UIKit/UIKit.h>
#import "engine/platform/apple/AiversePlugin.h"

@interface AiverseAttPlugin : NSObject <AiversePlugin>
@end

static NSString* AiverseAttStatus(void) {
    if (@available(iOS 14, *)) {
        switch (ATTrackingManager.trackingAuthorizationStatus) {
        case ATTrackingManagerAuthorizationStatusNotDetermined: return @"notDetermined";
        case ATTrackingManagerAuthorizationStatusRestricted: return @"restricted";
        case ATTrackingManagerAuthorizationStatusDenied: return @"denied";
        case ATTrackingManagerAuthorizationStatusAuthorized: return @"authorized";
        }
    }
    return @"unavailable"; // iOS 13: no prompt; the ad id follows the system "Limit Ad Tracking" switch
}

@implementation AiverseAttPlugin {
    NSString* _prompt;
    double _delay;
    BOOL _asked;
    id _observer;
}

- (void)call:(NSString*)method args:(NSDictionary*)args reply:(AiversePluginReply*)reply {
    if ([method isEqualToString:@"init"]) {
        _prompt = [args[@"prompt"] isKindOfClass:NSString.class] ? args[@"prompt"] : @"manual";
        _delay = args[@"delay"] ? [args[@"delay"] doubleValue] : 1.0;
        [reply ok:nil];
    } else if ([method isEqualToString:@"status"]) {
        [reply ok:@{@"status": AiverseAttStatus()}];
    } else if ([method isEqualToString:@"request"]) {
        [self request:^(NSString* status) { [reply ok:@{@"status": status}]; }];
    } else if ([method isEqualToString:@"lifecycle"]) {
        [reply ok:nil];
        if ([args[@"event"] isEqual:@"ready"] && [_prompt isEqualToString:@"ready"] && !_asked) {
            _asked = YES;
            dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(MAX(0.0, _delay) * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
                [self request:^(NSString* status) { AiversePluginEmit(@"att", @"status", @{@"status": status}); }];
            });
        }
    } else {
        [reply fail:[@"att: unknown method " stringByAppendingString:method]];
    }
}

- (void)request:(void (^)(NSString*))done {
    if (@available(iOS 14, *)) {
        if (ATTrackingManager.trackingAuthorizationStatus != ATTrackingManagerAuthorizationStatusNotDetermined) { done(AiverseAttStatus()); return; }
        if (UIApplication.sharedApplication.applicationState != UIApplicationStateActive) {
            __weak AiverseAttPlugin* weakSelf = self;
            _observer = [NSNotificationCenter.defaultCenter addObserverForName:UIApplicationDidBecomeActiveNotification object:nil
                                                                         queue:NSOperationQueue.mainQueue usingBlock:^(NSNotification*) {
                AiverseAttPlugin* s = weakSelf;
                if (!s) return;
                [NSNotificationCenter.defaultCenter removeObserver:s->_observer];
                s->_observer = nil;
                [s request:done];
            }];
            return;
        }
        [ATTrackingManager requestTrackingAuthorizationWithCompletionHandler:^(ATTrackingManagerAuthorizationStatus) {
            dispatch_async(dispatch_get_main_queue(), ^{ done(AiverseAttStatus()); });
        }];
        return;
    }
    done(AiverseAttStatus());
}

@end
