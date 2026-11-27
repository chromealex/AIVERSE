#import "StudioAssetSchemeHandler.h"
#import <Foundation/Foundation.h>

#include "../StudioWebAsset.h"

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

    NSString* encoded=[NSURLComponents componentsWithURL:url resolvingAgainstBaseURL:NO].percentEncodedPath;
    NSData* path=[[encoded stringByRemovingPercentEncoding] dataUsingEncoding:NSUTF8StringEncoding];
    if (!path || !root_) {
        [task didFailWithError:studioAssetError(@"Invalid Studio asset path")];
        return;
    }
    auto asset=aiverse::studio::studioWebAsset(root_.fileSystemRepresentation,
        std::string_view(static_cast<const char*>(path.bytes),path.length));
    if (!asset) {
        [task didFailWithError:studioAssetError(@"Missing Studio web asset")];
        return;
    }
    NSString* resolved=[NSString stringWithUTF8String:asset->file.c_str()];
    NSString* mime=[NSString stringWithUTF8String:asset->mime];
    NSError* readError=nil;
    NSData* bytes=[NSData dataWithContentsOfFile:resolved
                                       options:NSDataReadingMappedIfSafe
                                         error:&readError];
    if (!bytes || bytes.length > asset->maxBytes) {
        [task didFailWithError:readError ?: studioAssetError(@"Studio asset could not be read")];
        return;
    }
    NSURLResponse* response=[[NSURLResponse alloc] initWithURL:url
                                                     MIMEType:mime
                                        expectedContentLength:(NSInteger)bytes.length
                                             textEncodingName:[mime hasPrefix:@"text/"] ? @"utf-8" : nil];
    if ([resolved.lastPathComponent isEqualToString:@"index.html"] || [resolved.pathExtension isEqualToString:@"js"])
        NSLog(@"[studio-web] served %@ (%lu bytes)",resolved.lastPathComponent,(unsigned long)bytes.length);
    [task didReceiveResponse:response];
    [task didReceiveData:bytes];
    [task didFinish];
}
- (void)webView:(WKWebView*)webView stopURLSchemeTask:(id<WKURLSchemeTask>)task {
    (void)webView;
    (void)task; // Synchronous, bounded file reads have already completed.
}
@end
