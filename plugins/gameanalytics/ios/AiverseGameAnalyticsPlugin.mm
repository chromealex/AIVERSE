// GameAnalytics iOS SDK (GA-SDK-IOS 5.1) → the engine's "analytics" protocol (docs/plugins.md).
// Same config as the Android plugin; configure* calls must come before initializeWithGameKey.
#import <GameAnalytics/GameAnalytics.h>
#import "engine/platform/apple/AiversePlugin.h"

@interface AiverseGameAnalyticsPlugin : NSObject <AiversePlugin>
@end

static NSArray* AiverseGAStrings(id v) {
    NSMutableArray* r = [NSMutableArray array];
    if ([v isKindOfClass:NSArray.class]) for (id s in (NSArray*)v) [r addObject:[NSString stringWithFormat:@"%@", s]];
    return r;
}

// Flat fields: strings and numbers only (the engine already turned booleans into 1 / 0).
static NSDictionary* AiverseGAFields(id v) {
    NSMutableDictionary* r = [NSMutableDictionary dictionary];
    if (![v isKindOfClass:NSDictionary.class]) return r;
    [(NSDictionary*)v enumerateKeysAndObjectsUsingBlock:^(id k, id obj, BOOL*) {
        if ([obj isKindOfClass:NSString.class] || [obj isKindOfClass:NSNumber.class]) r[k] = obj;
    }];
    return r;
}

@implementation AiverseGameAnalyticsPlugin {
    BOOL _started, _customEvents;
    NSMutableDictionary* _global;
}

- (void)call:(NSString*)method args:(NSDictionary*)args reply:(AiversePluginReply*)reply {
    if ([method isEqualToString:@"init"]) { [self start:args reply:reply]; return; }
    if ([method isEqualToString:@"lifecycle"] || [method isEqualToString:@"flush"]) { [reply ok:nil]; return; }
    if ([method isEqualToString:@"consent"]) {
        [GameAnalytics setEnabledEventSubmission:args[@"analytics"] ? [args[@"analytics"] boolValue] : YES];
        [reply ok:nil];
        return;
    }
    if ([method isEqualToString:@"user"]) { [self user:args]; [reply ok:nil]; return; }
    if (!_started) { [reply fail:@"gameanalytics: not initialized (gameKey / secretKey missing?)"]; return; }
    NSDictionary* fields = AiverseGAFields(args[@"fields"]);
    if ([method isEqualToString:@"event"]) {
        if (_customEvents) [GameAnalytics addDesignEventWithEventId:args[@"name"] customFields:fields];
    } else if ([method isEqualToString:@"design"]) {
        if (args[@"value"]) [GameAnalytics addDesignEventWithEventId:args[@"id"] value:args[@"value"] customFields:fields];
        else [GameAnalytics addDesignEventWithEventId:args[@"id"] customFields:fields];
    } else if ([method isEqualToString:@"progression"]) {
        NSString* s = args[@"status"];
        GAProgressionStatus st = [s isEqualToString:@"complete"] ? GAProgressionStatusComplete
                               : [s isEqualToString:@"fail"] ? GAProgressionStatusFail : GAProgressionStatusStart;
        NSArray* p = AiverseGAStrings(args[@"path"]);
        if (p.count == 0) { [reply ok:nil]; return; }
        NSString* p2 = p.count > 1 ? p[1] : nil;
        NSString* p3 = p.count > 2 ? p[2] : nil;
        if (args[@"score"])
            [GameAnalytics addProgressionEventWithProgressionStatus:st progression01:p[0] progression02:p2 progression03:p3
                                                              score:[args[@"score"] integerValue] customFields:fields];
        else
            [GameAnalytics addProgressionEventWithProgressionStatus:st progression01:p[0] progression02:p2 progression03:p3
                                                       customFields:fields];
    } else if ([method isEqualToString:@"resource"]) {
        GAResourceFlowType flow = [args[@"flow"] isEqual:@"sink"] ? GAResourceFlowTypeSink : GAResourceFlowTypeSource;
        [GameAnalytics addResourceEventWithFlowType:flow currency:args[@"currency"] amount:args[@"amount"]
                                           itemType:args[@"itemType"] itemId:args[@"itemId"] customFields:fields];
    } else if ([method isEqualToString:@"purchase"]) {
        NSInteger cents = (NSInteger)llround([args[@"priceMicros"] doubleValue] / 10000.0);
        NSString* cart = [args[@"cartType"] length] ? args[@"cartType"] : @"shop";
        NSDictionary* receipt = [args[@"receipt"] isKindOfClass:NSDictionary.class] ? args[@"receipt"] : @{};
        NSString* tx = [receipt[@"transactionId"] isKindOfClass:NSString.class] ? receipt[@"transactionId"] : @"";
        if (tx.length) // validated through the App Store Server API (SDK 5.1)
            [GameAnalytics addBusinessEventWithCurrency:args[@"currency"] amount:cents itemType:args[@"itemType"] itemId:args[@"itemId"]
                                               cartType:cart transactionId:tx customFields:fields];
        else
            [GameAnalytics addBusinessEventWithCurrency:args[@"currency"] amount:cents itemType:args[@"itemType"] itemId:args[@"itemId"]
                                               cartType:cart receipt:nil customFields:fields];
    } else {
        [reply fail:[@"gameanalytics: unknown method " stringByAppendingString:method]];
        return;
    }
    [reply ok:nil];
}

- (void)start:(NSDictionary*)cfg reply:(AiversePluginReply*)reply {
    NSString* gameKey = cfg[@"gameKey"], *secret = cfg[@"secretKey"];
    if (![gameKey isKindOfClass:NSString.class] || ![secret isKindOfClass:NSString.class] || !gameKey.length || !secret.length) {
        [reply fail:@"gameanalytics: gameKey and secretKey are required"];
        return;
    }
    _customEvents = cfg[@"customEvents"] ? [cfg[@"customEvents"] boolValue] : YES;
    _global = [NSMutableDictionary dictionary];
    [GameAnalytics setEnabledInfoLog:[cfg[@"infoLog"] boolValue]];
    [GameAnalytics setEnabledVerboseLog:[cfg[@"verboseLog"] boolValue]];
    [GameAnalytics setEnabledErrorReporting:cfg[@"errorReporting"] ? [cfg[@"errorReporting"] boolValue] : YES];
    if (cfg[@"build"]) [GameAnalytics configureBuild:[NSString stringWithFormat:@"%@", cfg[@"build"]]];
    NSArray* v;
    if ((v = AiverseGAStrings(cfg[@"currencies"])).count) [GameAnalytics configureAvailableResourceCurrencies:v];
    if ((v = AiverseGAStrings(cfg[@"itemTypes"])).count) [GameAnalytics configureAvailableResourceItemTypes:v];
    if ((v = AiverseGAStrings(cfg[@"dimensions01"])).count) [GameAnalytics configureAvailableCustomDimensions01:v];
    if ((v = AiverseGAStrings(cfg[@"dimensions02"])).count) [GameAnalytics configureAvailableCustomDimensions02:v];
    if ((v = AiverseGAStrings(cfg[@"dimensions03"])).count) [GameAnalytics configureAvailableCustomDimensions03:v];
    if ([cfg[@"userId"] isKindOfClass:NSString.class]) [GameAnalytics configureUserId:cfg[@"userId"]];
    [GameAnalytics initializeWithGameKey:gameKey gameSecret:secret];
    _started = YES;
    [reply ok:nil];
}

- (void)user:(NSDictionary*)args {
    if ([args[@"id"] isKindOfClass:NSString.class]) [GameAnalytics configureExternalUserId:args[@"id"]];
    NSDictionary* props = [args[@"properties"] isKindOfClass:NSDictionary.class] ? args[@"properties"] : nil;
    if (!props) return;
    if (!_global) _global = [NSMutableDictionary dictionary];
    __block BOOL global = NO;
    [props enumerateKeysAndObjectsUsingBlock:^(NSString* k, id obj, BOOL*) {
        NSString* value = [NSString stringWithFormat:@"%@", obj];
        if ([k isEqualToString:@"dimension01"]) [GameAnalytics setCustomDimension01:value];
        else if ([k isEqualToString:@"dimension02"]) [GameAnalytics setCustomDimension02:value];
        else if ([k isEqualToString:@"dimension03"]) [GameAnalytics setCustomDimension03:value];
        else { self->_global[k] = value; global = YES; }
    }];
    if (global) [GameAnalytics setGlobalCustomEventFields:[_global copy]];
}

@end
