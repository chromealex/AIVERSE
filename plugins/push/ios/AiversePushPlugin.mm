// Notifications → the engine's "push" protocol (docs/plugins.md, engine/services/Push.h) on iOS:
// UNUserNotificationCenter for the permission, local notifications (time interval triggers; the
// category / tag live in userInfo so cancel() can find them) and the delegate that shows pushes in
// the foreground ("received") and reports taps ("opened"). Remote push: registerForRemoteNotifications;
// the APNs device token reaches the plugin through a method added to the app delegate's class (the
// engine's delegate does not forward it). Needs the Push Notifications capability (aps-environment
// entitlement) in the Xcode project for remote push; local notifications work without it.
#import <UIKit/UIKit.h>
#import <UserNotifications/UserNotifications.h>
#import <objc/runtime.h>
#import "engine/platform/apple/AiversePlugin.h"

static NSString* gToken = @"";
static NSDictionary* gLaunchOpen = nil;   // a tap before the game called init
static BOOL gReady = NO;

static void emitOpened(NSDictionary* info, NSString* ident) {
    NSDictionary* data = info[@"aiverse.data"] ?: info;
    NSDictionary* ev = @{@"id": info[@"aiverse.id"] ?: ident ?: @"", @"data": data ?: @{}};
    if (!gReady) { gLaunchOpen = ev; return; }
    AiversePluginEmit(@"push", @"opened", ev);
}

static void tokenIMP(id self, SEL _cmd, UIApplication* app, NSData* token) {
    NSMutableString* hex = [NSMutableString string];
    const unsigned char* b = (const unsigned char*)token.bytes;
    for (NSUInteger i = 0; i < token.length; i++) [hex appendFormat:@"%02x", b[i]];
    gToken = hex;
    AiversePluginEmit(@"push", @"token", @{@"token": hex});
}

@interface AiversePushPlugin : NSObject <AiversePlugin, UNUserNotificationCenterDelegate>
@end

@implementation AiversePushPlugin

+ (void)load {
    [[NSNotificationCenter defaultCenter] addObserverForName:UIApplicationDidFinishLaunchingNotification object:nil queue:nil
                                                  usingBlock:^(NSNotification* n) {
        Class cls = [UIApplication.sharedApplication.delegate class];
        SEL sel = @selector(application:didRegisterForRemoteNotificationsWithDeviceToken:);
        if (cls && !class_getInstanceMethod(cls, sel)) class_addMethod(cls, sel, (IMP)tokenIMP, "v@:@@");
        static AiversePushPlugin* delegate;   // the center keeps its delegate weakly
        delegate = [AiversePushPlugin new];
        UNUserNotificationCenter.currentNotificationCenter.delegate = delegate;
    }];
}

- (void)call:(NSString*)method args:(NSDictionary*)args reply:(AiversePluginReply*)reply {
    UNUserNotificationCenter* center = UNUserNotificationCenter.currentNotificationCenter;
    if ([method isEqualToString:@"init"]) {
        gReady = YES;
        if (gLaunchOpen) { AiversePluginEmit(@"push", @"opened", gLaunchOpen); gLaunchOpen = nil; }
        [center getNotificationSettingsWithCompletionHandler:^(UNNotificationSettings* s) {
            if (s.authorizationStatus == UNAuthorizationStatusAuthorized)
                dispatch_async(dispatch_get_main_queue(), ^{ [UIApplication.sharedApplication registerForRemoteNotifications]; });
        }];
        if ([args[@"askOnStart"] boolValue]) [self permission:nil];
        [reply ok:nil];
    } else if ([method isEqualToString:@"permission"]) {
        [self permission:reply];
    } else if ([method isEqualToString:@"token"]) {
        [reply ok:@{@"token": gToken}];
    } else if ([method isEqualToString:@"schedule"]) {
        UNMutableNotificationContent* c = [UNMutableNotificationContent new];
        c.title = args[@"title"] ?: @"";
        c.body = args[@"body"] ?: @"";
        c.sound = UNNotificationSound.defaultSound;
        c.userInfo = @{@"aiverse.id": args[@"id"] ?: @"", @"aiverse.category": args[@"category"] ?: @"",
                       @"aiverse.tag": args[@"tag"] ?: @"", @"aiverse.data": args[@"data"] ?: @{}};
        double delay = MAX(1.0, [args[@"delay"] doubleValue]);
        double repeat = [args[@"repeat"] doubleValue];
        // a repeating trigger fires every `repeat` seconds (>= 60) starting one interval from now
        UNTimeIntervalNotificationTrigger* t = repeat >= 60
            ? [UNTimeIntervalNotificationTrigger triggerWithTimeInterval:repeat repeats:YES]
            : [UNTimeIntervalNotificationTrigger triggerWithTimeInterval:delay repeats:NO];
        UNNotificationRequest* r = [UNNotificationRequest requestWithIdentifier:args[@"id"] content:c trigger:t];
        [center addNotificationRequest:r withCompletionHandler:^(NSError* e) {
            if (e) [reply fail:e.localizedDescription]; else [reply ok:nil];
        }];
    } else if ([method isEqualToString:@"cancel"]) {
        if ([args[@"all"] boolValue]) {
            [center removeAllPendingNotificationRequests];
            [center removeAllDeliveredNotifications];
            [reply ok:nil];
            return;
        }
        [center getPendingNotificationRequestsWithCompletionHandler:^(NSArray<UNNotificationRequest*>* list) {
            NSMutableArray* ids = [NSMutableArray array];
            for (UNNotificationRequest* r in list) {
                NSDictionary* u = r.content.userInfo;
                if ((args[@"id"] && [r.identifier isEqualToString:args[@"id"]]) ||
                    (args[@"category"] && [u[@"aiverse.category"] isEqual:args[@"category"]]) ||
                    (args[@"tag"] && [u[@"aiverse.tag"] isEqual:args[@"tag"]]))
                    [ids addObject:r.identifier];
            }
            [center removePendingNotificationRequestsWithIdentifiers:ids];
            [center removeDeliveredNotificationsWithIdentifiers:ids];
            [reply ok:nil];
        }];
    } else if ([method isEqualToString:@"badge"]) {
        UIApplication.sharedApplication.applicationIconBadgeNumber = [args[@"count"] integerValue];
        [reply ok:nil];
    } else if ([method isEqualToString:@"lifecycle"]) {
        [reply ok:nil];
    } else {
        [reply fail:[@"push: unknown method " stringByAppendingString:method]];
    }
}

- (void)permission:(AiversePluginReply*)reply {
    UNAuthorizationOptions opts = UNAuthorizationOptionAlert | UNAuthorizationOptionSound | UNAuthorizationOptionBadge;
    [UNUserNotificationCenter.currentNotificationCenter requestAuthorizationWithOptions:opts completionHandler:^(BOOL granted, NSError* e) {
        if (granted) dispatch_async(dispatch_get_main_queue(), ^{ [UIApplication.sharedApplication registerForRemoteNotifications]; });
        [reply ok:@{@"granted": @(granted)}];
    }];
}

// foreground: report it and still show the banner
- (void)userNotificationCenter:(UNUserNotificationCenter*)center willPresentNotification:(UNNotification*)n
         withCompletionHandler:(void (^)(UNNotificationPresentationOptions))done {
    UNNotificationContent* c = n.request.content;
    if (gReady) AiversePluginEmit(@"push", @"received", @{@"title": c.title ?: @"", @"body": c.body ?: @"",
                                                          @"data": c.userInfo[@"aiverse.data"] ?: c.userInfo ?: @{}, @"foreground": @YES});
    done(UNNotificationPresentationOptionBanner | UNNotificationPresentationOptionSound);
}

- (void)userNotificationCenter:(UNUserNotificationCenter*)center didReceiveNotificationResponse:(UNNotificationResponse*)r
         withCompletionHandler:(void (^)(void))done {
    emitOpened(r.notification.request.content.userInfo, r.notification.request.identifier);
    done();
}

@end
