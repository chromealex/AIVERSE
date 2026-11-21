// Serve built HTML5 assets to WKWebView with a stable, self-contained origin.
// file:// is not reliable for ESM module imports on current WebKit builds.
// No network socket is opened. Strictly read-only files from the bundled dist/.
#pragma once
#import <WebKit/WebKit.h>

@interface StudioAssetSchemeHandler : NSObject <WKURLSchemeHandler>
- (instancetype)initWithRoot:(NSString*)root;
@end
