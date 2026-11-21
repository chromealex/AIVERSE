#import "StudioAssetSchemeHandler.h"
#import <Foundation/Foundation.h>

static NSString* studioMimeType(NSString* extension) {
    static NSDictionary<NSString*,NSString*>* mime;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        mime = @{
            @"html": @"text/html",
            @"js": @"text/javascript",
            @"mjs": @"text/javascript",
            @"css": @"text/css",
            @"json": @"application/json",
            @"svg": @"image/svg+xml",
            @"png": @"image/png",
            @"jpg": @"image/jpeg",
            @"jpeg": @"image/jpeg",
            @"webp": @"image/webp",
            @"avif": @"image/avif",
            @"gif": @"image/gif",
            @"ico": @"image/x-icon",
            @"woff": @"font/woff",
            @"woff2": @"font/woff2",
            @"ttf": @"font/ttf",
            @"wasm": @"application/wasm"
        };
    });
    return mime[extension.lowercaseString];
}
static NSError* studioAssetError(NSString* description) {
    return [NSError errorWithDomain:NSURLErrorDomain
                               code:NSURLErrorFileDoesNotExist
                           userInfo:@{NSLocalizedDescriptionKey:description}];
}
@implementation StudioAssetSchemeHandler {
    NSString* root_;
}
- (instancetype)initWithRoot:(NSString*)root {
    if ((self = [super init])) {
        root_ = [[[root stringByStandardizingPath] stringByResolvingSymlinksInPath] copy];
    }
    return self;
}
- (void)webView:(WKWebView*)webView startURLSchemeTask:(id<WKURLSchemeTask>)task {
    (void)webView;
    NSURL* url=task.request.URL;
    if (![url.scheme isEqualToString:@"aiverse-studio"] ||
        ![url.host isEqualToString:@"shell"]) {
        [task didFailWithError:studioAssetError(@"Invalid AIVERSE Studio origin")];
        return;
    }

    NSString* requestPath=[url.path stringByRemovingPercentEncoding];
    // @"\\0" is NOT a valid embedded NUL sentinel here. Construct an actual
    // length-1 NSString, otherwise an empty marker could reject every asset.
    unichar nullCharacter=0;
    NSString* nullMarker=[NSString stringWithCharacters:&nullCharacter length:1];
    if (!requestPath || ![requestPath hasPrefix:@"/"] ||
        [requestPath containsString:@"\\"] || [requestPath containsString:nullMarker]) {
        [task didFailWithError:studioAssetError(@"Invalid Studio asset path")];
        return;
    }
    if ([requestPath isEqualToString:@"/"]) requestPath=@"/index.html";
    for (NSString* segment in [requestPath componentsSeparatedByString:@"/"]) {
        if ([segment isEqualToString:@".."] || [segment isEqualToString:@"."]) {
            [task didFailWithError:studioAssetError(@"Studio asset path traversal")];
            return;
        }
    }
    NSString* relative=[requestPath substringFromIndex:1];
    NSString* resolved=[[[root_ stringByAppendingPathComponent:relative]
        stringByStandardizingPath] stringByResolvingSymlinksInPath];
    if (![resolved hasPrefix:[root_ stringByAppendingString:@"/"]]) {
        [task didFailWithError:studioAssetError(@"Studio resource outside web bundle")];
        return;
    }
    NSString* mime=studioMimeType(resolved.pathExtension);
    BOOL directory=NO;
    if (!mime || ![[NSFileManager defaultManager] fileExistsAtPath:resolved isDirectory:&directory] ||
        directory) {
        NSLog(@"[studio-web] bundled asset unavailable: %@ (%@)",requestPath,resolved);
        [task didFailWithError:studioAssetError([@"Missing web asset " stringByAppendingString:requestPath])];
        return;
    }
    NSError* readError=nil;
    NSData* bytes=[NSData dataWithContentsOfFile:resolved
                                       options:NSDataReadingMappedIfSafe
                                         error:&readError];
    if (!bytes || bytes.length > 32u*1024u*1024u) {
        [task didFailWithError:readError ?: studioAssetError(@"Studio asset could not be read")];
        return;
    }
    NSURLResponse* response=[[NSURLResponse alloc] initWithURL:url
                                                     MIMEType:mime
                                        expectedContentLength:(NSInteger)bytes.length
                                             textEncodingName:[mime hasPrefix:@"text/"] ? @"utf-8" : nil];
    if ([relative isEqualToString:@"index.html"] || [resolved.pathExtension isEqualToString:@"js"])
        NSLog(@"[studio-web] served %@ (%lu bytes)",relative,(unsigned long)bytes.length);
    [task didReceiveResponse:response];
    [task didReceiveData:bytes];
    [task didFinish];
}
- (void)webView:(WKWebView*)webView stopURLSchemeTask:(id<WKURLSchemeTask>)task {
    (void)webView;
    (void)task; // Synchronous, bounded file reads have already completed.
}
@end
