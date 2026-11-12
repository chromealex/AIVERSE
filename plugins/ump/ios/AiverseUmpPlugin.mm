// Google UMP → the engine's "consent" protocol (docs/plugins.md) on iOS; same flow as UmpPlugin.java.
#import <UIKit/UIKit.h>
#import <UserMessagingPlatform/UserMessagingPlatform.h>
#import "engine/platform/apple/AiversePlugin.h"

@interface AiverseUmpPlugin : NSObject <AiversePlugin>
@end

@implementation AiverseUmpPlugin {
    NSDictionary* _config;
}

- (void)call:(NSString*)method args:(NSDictionary*)args reply:(AiversePluginReply*)reply {
    if ([method isEqualToString:@"init"]) {
        _config = args;
        [reply ok:nil];
    } else if ([method isEqualToString:@"request"]) {
        UMPRequestParameters* p = [UMPRequestParameters new];
        p.tagForUnderAgeOfConsent = [_config[@"underAge"] boolValue];
        NSArray* test = _config[@"testDevices"];
        if ([args[@"debugGeography"] isEqual:@"eea"] || test) {
            UMPDebugSettings* d = [UMPDebugSettings new];
            if ([args[@"debugGeography"] isEqual:@"eea"]) d.geography = UMPDebugGeographyEEA;
            if ([test isKindOfClass:NSArray.class]) d.testDeviceIdentifiers = test;
            p.debugSettings = d;
        }
        AiversePluginEmit(@"ump", @"app.pause", nil);
        [UMPConsentInformation.sharedInstance requestConsentInfoUpdateWithParameters:p completionHandler:^(NSError* e) {
            if (e) { AiversePluginEmit(@"ump", @"app.resume", nil); [self answer:reply error:e]; return; }
            [UMPConsentForm loadAndPresentIfRequiredFromViewController:AiversePluginPresentingViewController() completionHandler:^(NSError* fe) {
                AiversePluginEmit(@"ump", @"app.resume", nil);
                [self answer:reply error:fe];
            }];
        }];
    } else if ([method isEqualToString:@"options"]) {
        [UMPConsentForm presentPrivacyOptionsFormFromViewController:AiversePluginPresentingViewController() completionHandler:^(NSError* e) {
            [self answer:reply error:e];
        }];
    } else if ([method isEqualToString:@"lifecycle"]) {
        [reply ok:nil];
    } else {
        [reply fail:[@"ump: unknown method " stringByAppendingString:method]];
    }
}

- (void)answer:(AiversePluginReply*)reply error:(NSError*)e {
    UMPConsentInformation* info = UMPConsentInformation.sharedInstance;
    NSString* status = info.consentStatus == UMPConsentStatusNotRequired ? @"notRequired"
                     : info.consentStatus == UMPConsentStatusRequired ? @"required"
                     : info.consentStatus == UMPConsentStatusObtained ? @"obtained" : @"unknown";
    NSString* purposes = [NSUserDefaults.standardUserDefaults stringForKey:@"IABTCF_PurposeConsents"] ?: @"";
    BOOL personalized = [status isEqualToString:@"notRequired"] ||
        (purposes.length >= 4 && [purposes characterAtIndex:0] == '1' && [purposes characterAtIndex:2] == '1' && [purposes characterAtIndex:3] == '1');
    NSMutableDictionary* r = [@{@"status": status, @"canRequestAds": @(info.canRequestAds),
                                @"personalized": @(personalized && info.canRequestAds),
                                @"optionsRequired": @(info.privacyOptionsRequirementStatus == UMPPrivacyOptionsRequirementStatusRequired)} mutableCopy];
    if (e) r[@"error"] = e.localizedDescription;
    [reply ok:r];
}

@end
