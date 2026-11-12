// AppMetrica iOS SDK (AppMetricaAnalytics 5.16) → the engine's "analytics" protocol (docs/plugins.md).
// Same config and event mapping as the Android plugin (structuredEvents off: only "event" calls).
#import <AppMetricaCore/AppMetricaCore.h>
#import "engine/platform/apple/AiversePlugin.h"

@interface AiverseAppMetricaPlugin : NSObject <AiversePlugin>
@end

@implementation AiverseAppMetricaPlugin {
    BOOL _started, _structured;
}

- (void)call:(NSString*)method args:(NSDictionary*)args reply:(AiversePluginReply*)reply {
    if ([method isEqualToString:@"init"]) {
        NSString* key = [args[@"apiKey"] isKindOfClass:NSString.class] ? args[@"apiKey"] : @"";
        AMAAppMetricaConfiguration* cfg = key.length ? [[AMAAppMetricaConfiguration alloc] initWithAPIKey:key] : nil;
        if (!cfg) { [reply fail:@"appmetrica: apiKey is required (or invalid)"]; return; }
        cfg.locationTracking = [args[@"locationTracking"] boolValue];
        cfg.handleFirstActivationAsUpdate = [args[@"firstActivationAsUpdate"] boolValue];
        cfg.logsEnabled = [args[@"logs"] boolValue];
        [AMAAppMetrica activateWithConfiguration:cfg];
        _structured = [args[@"structuredEvents"] boolValue];
        _started = YES;
        [reply ok:nil];
        return;
    }
    if ([method isEqualToString:@"lifecycle"]) { [reply ok:nil]; return; }
    if ([method isEqualToString:@"consent"]) {
        [AMAAppMetrica setDataSendingEnabled:args[@"analytics"] ? [args[@"analytics"] boolValue] : YES];
        [reply ok:nil];
        return;
    }
    if ([method isEqualToString:@"user"]) {
        if ([args[@"id"] isKindOfClass:NSString.class]) AMAAppMetrica.userProfileID = [args[@"id"] length] ? args[@"id"] : nil;
        [reply ok:nil];
        return;
    }
    if ([method isEqualToString:@"flush"]) { if (_started) [AMAAppMetrica sendEventsBuffer]; [reply ok:nil]; return; }
    if (!_started) { [reply fail:@"appmetrica: not initialized"]; return; }
    if (![method isEqualToString:@"event"] && !_structured) { [reply ok:nil]; return; }
    NSMutableDictionary* params = [args[@"fields"] isKindOfClass:NSDictionary.class] ? [args[@"fields"] mutableCopy] : [NSMutableDictionary dictionary];
    NSString* name = method;
    if ([method isEqualToString:@"event"]) name = args[@"name"];
    else if ([method isEqualToString:@"design"]) { name = args[@"id"]; if (args[@"value"]) params[@"value"] = args[@"value"]; }
    else if ([method isEqualToString:@"progression"]) { for (NSString* k in @[@"status", @"path", @"score"]) if (args[k]) params[k] = args[k]; }
    else if ([method isEqualToString:@"resource"]) { for (NSString* k in @[@"flow", @"currency", @"amount", @"itemType", @"itemId"]) if (args[k]) params[k] = args[k]; }
    else if ([method isEqualToString:@"purchase"]) { for (NSString* k in @[@"currency", @"priceMicros", @"itemType", @"itemId", @"cartType"]) if (args[k]) params[k] = args[k]; }
    else { [reply fail:[@"appmetrica: unknown method " stringByAppendingString:method]]; return; }
    [AMAAppMetrica reportEvent:name parameters:params onFailure:nil];
    [reply ok:nil];
}

@end
