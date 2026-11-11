// Objective-C plugins for iOS / macOS (payments, ads, ...). Game-side API: engine/platform/Plugins.h.
//
// A plugin is a class conforming to AiversePlugin, listed in Info.plist under AiversePlugins
// (aiversebuild writes it from plugins/<name>/plugin.json):
//   <key>AiversePlugins</key><dict><key>admob</key><string>AiverseAdMobPlugin</string></dict>
// It is created on first use (-init) and called on the main thread. Arguments arrive as parsed
// JSON (NSDictionary); answer through the reply exactly once, now or later, from any thread.
#pragma once
#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

@interface AiversePluginReply : NSObject
// data: NSDictionary / NSArray / NSString / NSNumber / NSNull / nil (JSON-serializable).
- (void)ok:(nullable id)data;
// @"cancelled" means the user backed out (not an error to show).
- (void)fail:(NSString*)error;
@end

@protocol AiversePlugin <NSObject>
// method "init" comes first, with the plugin's config from game.json.
- (void)call:(NSString*)method args:(NSDictionary*)args reply:(AiversePluginReply*)reply;
@end

#ifdef __cplusplus
extern "C" {
#endif
// Sends an event to the game (any thread). "app.pause"/"app.resume" pause and resume the game.
void AiversePluginEmit(NSString* plugin, NSString* event, id _Nullable data);
// The view controller to present ads / payment sheets from (iOS); nil on macOS.
id _Nullable AiversePluginPresentingViewController(void);
#ifdef __cplusplus
}
#endif

NS_ASSUME_NONNULL_END
