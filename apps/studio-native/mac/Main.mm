// AIVERSE Studio, macOS shell: WKWebView UI + native Metal Scene / Game views (sibling NSViews over the DOM
// viewport rects, no CPU frame streaming). Everything that is not Cocoa lives in apps/studio-native/StudioHost
// (shared with the Windows shell, win/Main.cpp).
#import <AppKit/AppKit.h>
#import <WebKit/WebKit.h>
#import <ScreenCaptureKit/ScreenCaptureKit.h>
#import <QuartzCore/CAMetalLayer.h>
#import <QuartzCore/CATransaction.h>
#import "StudioAssetSchemeHandler.h"
#include "apps/studio-native/StudioHost.h"
#include "apps/studio-native/StudioNetworkPlay.h"
#include "apps/studio-native/StudioGameInput.h"
#include "apps/editor/core/BuildTool.h"
#include "apps/editor/core/ProjectFile.h"
#include "engine/platform/EngineDir.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <memory>
#include <regex>
#include <string>
#include <vector>

#ifndef AIVERSE_STUDIO_SOURCE_DIR
#define AIVERSE_STUDIO_SOURCE_DIR "."
#endif

using aiverse::Json;
namespace studio = aiverse::studio;

@interface StudioWindowChrome : NSView
@end
@implementation StudioWindowChrome
- (BOOL)acceptsFirstMouse:(NSEvent*)event { (void)event; return YES; }
- (void)mouseDown:(NSEvent*)event {
    if(event.clickCount==2)[self.window performZoom:nil];
    else [self.window performWindowDragWithEvent:event];
}
@end
@class StudioShell;
// A native drop receiver is required because Finder drags local file URLs,
// not JavaScript-provided paths. These are trusted only after NSDraggingDestination
// resolves them and the drop falls inside the HTML5 Assets panel.
@interface StudioFileDropWebView : WKWebView <NSDraggingDestination>
@property(nonatomic, assign) StudioShell* shell;
@end
@interface StudioTransparentWebView : StudioFileDropWebView
@end
@implementation StudioTransparentWebView
- (BOOL)isOpaque { return NO; }
@end
@interface StudioMetalView : NSView {
    NSTrackingArea* hoverTracking_;
    NSTimeInterval lastHover_;
    BOOL sceneMenuPress_;
}
@property(nonatomic, assign) StudioShell* shell;
@end

@interface StudioFxView : StudioMetalView
@property(nonatomic) studio::StudioSurface surface;
@property(nonatomic) NSPoint pressPoint;
@property(nonatomic) BOOL clickPending;
@end
@interface StudioCharacterSurface : NSView
@property(nonatomic,strong) CALayer* photo;
@property(nonatomic,strong) CALayer* brushRing;
@property(nonatomic) CGFloat photoAspect,photoScale,photoX,photoY;
@property(nonatomic) BOOL photoMirror;
@end
@implementation StudioCharacterSurface
- (NSView*)hitTest:(NSPoint)point { (void)point;return nil; }
- (void)layout {
    [super layout];
    CGFloat h=self.bounds.size.height*self.photoScale,w=h*self.photoAspect;
    self.photo.bounds=CGRectMake(0,0,w,h);
    self.photo.position=CGPointMake(NSMidX(self.bounds)+w*self.photoX*.5,NSMidY(self.bounds)-h*self.photoY*.5);
    self.photo.transform=CATransform3DMakeScale(self.photoMirror?-1:1,1,1);
}
@end
@interface StudioGameView : StudioMetalView { NSTrackingArea* tracking_; }
@end

// Device frame drawn over the Game screen, like the legacy Game window: rounded screen corners,
// the camera cut-out and (with Safe area on) the OS safe area. Never takes input.
@interface StudioDeviceChromeView : NSView
@property(nonatomic) BOOL active, showSafe;
@property(nonatomic) CGFloat corner, cutoutRadius;
@property(nonatomic) NSRect cutout;
@property(nonatomic) NSEdgeInsets safe;
@end
@implementation StudioDeviceChromeView
- (BOOL)isFlipped { return YES; }
- (NSView*)hitTest:(NSPoint)point { (void)point; return nil; }
- (void)drawRect:(NSRect)dirty {
    (void)dirty;
    if (!self.active) return;
    const NSRect b=self.bounds;
    NSColor* bezel=[NSColor colorWithSRGBRed:12/255.0 green:12/255.0 blue:14/255.0 alpha:1];
    if (self.corner>0) { // everything outside the rounded display is bezel
        NSBezierPath* p=[NSBezierPath bezierPathWithRect:b];
        [p appendBezierPath:[NSBezierPath bezierPathWithRoundedRect:b xRadius:self.corner yRadius:self.corner]];
        p.windingRule=NSWindingRuleEvenOdd;[bezel setFill];[p fill];
    }
    const NSEdgeInsets in=self.safe;
    if (self.showSafe && (in.left>0 || in.top>0 || in.right>0 || in.bottom>0)) {
        [NSGraphicsContext saveGraphicsState];
        [[NSBezierPath bezierPathWithRoundedRect:b xRadius:self.corner yRadius:self.corner] addClip];
        const NSRect inner=NSMakeRect(in.left,in.top,b.size.width-in.left-in.right,b.size.height-in.top-in.bottom);
        NSBezierPath* shade=[NSBezierPath bezierPathWithRect:b];
        [shade appendBezierPath:[NSBezierPath bezierPathWithRect:inner]];
        shade.windingRule=NSWindingRuleEvenOdd;
        [[NSColor colorWithSRGBRed:1 green:70/255.0 blue:60/255.0 alpha:70/255.0] setFill];[shade fill];
        NSBezierPath* edge=[NSBezierPath bezierPathWithRect:NSInsetRect(inner,.75,.75)];
        edge.lineWidth=1.5;[[NSColor colorWithSRGBRed:1 green:210/255.0 blue:80/255.0 alpha:.9] setStroke];[edge stroke];
        [NSGraphicsContext restoreGraphicsState];
    }
    if (self.cutout.size.width>0 && self.cutout.size.height>0) {
        [[NSColor blackColor] setFill];
        [[NSBezierPath bezierPathWithRoundedRect:self.cutout xRadius:self.cutoutRadius yRadius:self.cutoutRadius] fill];
    }
}
@end

@interface StudioShell : NSObject <NSApplicationDelegate, NSWindowDelegate, WKScriptMessageHandler, WKNavigationDelegate, WKUIDelegate> {
@public
    NSWindow* window_;
    WKWebView* web_;
    StudioMetalView* viewport_;
    NSView* assetViews_[int(studio::StudioSurface::Count)-int(studio::StudioSurface::Material)];
    StudioFxView* fxViewport_;
    StudioGameView* gameViewport_;
    StudioDeviceChromeView* gameChrome_;
    NSTimer* timer_;
    NSString* dist_;
    NSURL* devURL_;
    StudioAssetSchemeHandler* assets_;
    std::unique_ptr<studio::StudioHostPlatform> platform_;
    std::unique_ptr<studio::StudioHost> core_;
    BOOL htmlComposite_;
    BOOL mainWebReady_;
    BOOL hasFinderProject_;
    BOOL closeApproved_;
    // Floating panel windows (Unity-style undocked tabs), one panel per window.
    NSMutableDictionary<NSString*,NSWindow*>* floatWindows_;
    NSMutableDictionary<NSString*,WKWebView*>* floatWebs_;
    __weak WKWebView* dictationWeb_;
    __unsafe_unretained WKWebView* currentWeb_; // sender of the bridge message being handled
    BOOL closingFloats_;            // main window closes: keep the float list for next launch
    NSTimer* dockWatch_;            // a floating window is being dragged: dock it when dropped over the main window
    BOOL dockHover_;
}
- (instancetype)initWithProject:(const std::string&)project dev:(BOOL)dev composite:(BOOL)composite listenPort:(int)listenPort;
- (studio::StudioHost*)core;
- (WKWebView*)makeWebViewWithFrame:(NSRect)frame;
- (void)evaluateInAllWebs:(NSString*)js;
- (Json)openFloatingPanel:(const Json&)params;
- (void)installEditMenu;
- (Json)windowRequest:(const std::string&)method params:(const Json&)params;
- (BOOL)placeSurface:(studio::StudioSurface)surface rect:(const studio::ViewportRect&)rect out:(studio::NativeViewportPlacement&)out;
- (void)updateGameChrome:(const Json&)state;
- (void)importDroppedFileURLs:(NSArray<NSURL*>*)urls atWindowPoint:(NSPoint)point fromWeb:(WKWebView*)web;
- (void)runSkyShots;
- (void)runWindShots;
@end

namespace {
NSString* ns(const std::string& s) { return [NSString stringWithUTF8String:s.c_str()] ?: @""; }
// The macOS half of StudioHostPlatform: Cocoa windows, WKWebViews, Metal layers, AppKit panels.
class MacPlatform final : public studio::StudioHostPlatform {
public:
    explicit MacPlatform(StudioShell* shell) : shell_(shell) {}
    void evaluateScript(const std::string& js, bool currentOnly) override {
        NSString* text=[[NSString alloc] initWithBytes:js.data() length:js.size() encoding:NSUTF8StringEncoding];
        if (!text) return;
        if (currentOnly && shell_->currentWeb_) [shell_->currentWeb_ evaluateJavaScript:text completionHandler:nil];
        else [shell_ evaluateInAllWebs:text];
    }
    // The Scene's device is made on its render thread: the layers were captured on the main thread.
    void setLayers(void* scene, void* game, void* fx) {
        for(int i=0;i<int(studio::StudioSurface::Count)-int(studio::StudioSurface::Material);++i)assetLayers_[i]=(__bridge void*)shell_->assetViews_[i].layer;
        sceneLayer_=scene;gameLayer_=game;fxLayer_=fx;
    }
    std::unique_ptr<aiverse::gfx::Device> createDevice(studio::StudioSurface surface) override {
        return aiverse::gfx::createMetalDevice(
            surface>=studio::StudioSurface::Material?assetLayers_[int(surface)-int(studio::StudioSurface::Material)]:
            surface==studio::StudioSurface::Scene?sceneLayer_:
            surface==studio::StudioSurface::Fx?fxLayer_:gameLayer_);
    }
    // Render-thread frames: layer changes off the main thread go into an explicit transaction.
    void sceneThreadScope(const std::function<void()>& body) override {
        @autoreleasepool {
            [CATransaction begin];
            [CATransaction setDisableActions:YES];
            struct Commit { ~Commit() { [CATransaction commit]; } } commit;
            body();
        }
    }
    bool surfaceDrawable(studio::StudioSurface surface) override {
        NSView* view=surface>=studio::StudioSurface::Material?shell_->assetViews_[int(surface)-int(studio::StudioSurface::Material)]:surface==studio::StudioSurface::Scene?(NSView*)shell_->viewport_:
            surface==studio::StudioSurface::Fx?(NSView*)shell_->fxViewport_:(NSView*)shell_->gameViewport_;
        NSWindow* window=view.window ?: shell_->window_;
        return !view.hidden && !window.isMiniaturized && window.visible;
    }
    void humanReference(studio::StudioSurface surface,const aiverse::Json& input) override {
        auto* view=(StudioCharacterSurface*)shell_->assetViews_[int(surface)-int(studio::StudioSurface::Material)];
        if(input.getStr("kind")=="brushCursor"){
            CGFloat radius=std::clamp(input["radius"].toDouble(),0.,view.bounds.size.height);
            [CATransaction begin];[CATransaction setDisableActions:YES];
            view.brushRing.hidden=radius<=0;
            view.brushRing.bounds=CGRectMake(0,0,2*radius,2*radius);
            view.brushRing.cornerRadius=radius;
            view.brushRing.position=CGPointMake(input["x"].toDouble()*view.bounds.size.width,(1-input["y"].toDouble())*view.bounds.size.height);
            [CATransaction commit];
            return;
        }
        if(input.has("image")){
            const auto src=input.getStr("image");
            NSImage* image=nil;
            if(src.size()<32*1024*1024 && src.rfind("data:image/png;base64,",0)==0){
                NSString* content=[NSString stringWithUTF8String:src.c_str()+22];
                NSData* bytes=[[NSData alloc] initWithBase64EncodedString:content options:0];
                if(bytes)image=[[NSImage alloc] initWithData:bytes];
            }
            view.photo.contents=image?(__bridge id)[image CGImageForProposedRect:nil context:nil hints:nil]:nil;
            view.photoAspect=image&&image.size.height>0?image.size.width/image.size.height:1;
        }
        view.photo.opacity=std::clamp(input["opacity"].toDouble(),0.,1.);
        view.photo.hidden=!input.getBool("visible")||!view.photo.contents;
        view.photoScale=std::clamp(input["scale"].toDouble(),.1,2.);
        view.photoX=std::clamp(input["x"].toDouble(),-1.,1.);
        view.photoY=std::clamp(input["y"].toDouble(),-1.,1.);
        view.photoMirror=input.getBool("mirror");
        view.needsLayout=YES;
    }
    void captureStudioWindow(aiverse::gfx::Device::CaptureFn done) override {
        NSWindow* window=shell_->window_;
        if (!window.isVisible || window.isMiniaturized) { done(0,0,{});return; }
        const CGWindowID target=CGWindowID(window.windowNumber);
        if (@available(macOS 14.4,*)) {
            [SCShareableContent getCurrentProcessShareableContentWithCompletionHandler:^(SCShareableContent* content,NSError* error){
                SCWindow* selected=nil;
                for (SCWindow* candidate in content.windows) if (candidate.windowID==target) {selected=candidate;break;}
                if (error || !selected) {done(0,0,{});return;}
                SCContentFilter* filter=[[SCContentFilter alloc] initWithDesktopIndependentWindow:selected];
                SCStreamConfiguration* config=[[SCStreamConfiguration alloc] init];
                const CGRect rect=filter.contentRect;
                if (rect.size.width<1 || rect.size.height<1) {done(0,0,{});return;}
                const double scale=std::min({double(filter.pointPixelScale),1280./rect.size.width,2560./rect.size.height});
                config.width=std::max(1,int(std::round(rect.size.width*scale)));
                config.height=std::max(1,int(std::round(rect.size.height*scale)));
                config.showsCursor=NO;config.ignoreShadowsSingleWindow=YES;
                [SCScreenshotManager captureImageWithFilter:filter configuration:config completionHandler:^(CGImageRef image,NSError* captureError){
                    if (captureError || !image) {done(0,0,{});return;}
                    const size_t w=CGImageGetWidth(image),h=CGImageGetHeight(image);
                    if (!w || !h || w>4096 || h>4096) {done(0,0,{});return;}
                    std::vector<uint8_t> pixels(w*h*4);
                    CGColorSpaceRef space=CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
                    CGContextRef context=CGBitmapContextCreate(pixels.data(),w,h,8,w*4,space,
                        CGBitmapInfo(kCGImageAlphaPremultipliedLast)|CGBitmapInfo(kCGBitmapByteOrder32Big));
                    CGColorSpaceRelease(space);
                    if (!context) {done(0,0,{});return;}
                    CGContextDrawImage(context,CGRectMake(0,0,w,h),image);
                    CGContextRelease(context);
                    done(int(w),int(h),std::move(pixels));
                }];
            }];
        } else done(0,0,{});
    }
    bool gameFocused() override {
        NSWindow* window=shell_->gameViewport_.window ?: shell_->window_;
        return window.isKeyWindow && window.firstResponder==shell_->gameViewport_;
    }
    bool placeSurface(studio::StudioSurface surface, const studio::ViewportRect& rect, studio::NativeViewportPlacement& out) override {
        return [shell_ placeSurface:surface rect:rect out:out];
    }
    void gameDeviceChanged(const Json& state) override { [shell_ updateGameChrome:state]; }
    double now() override { return CACurrentMediaTime(); }
    void runInBackground(std::function<void()> work, std::function<void()> then) override {
        auto w=std::make_shared<std::function<void()>>(std::move(work));
        auto t=std::make_shared<std::function<void()>>(std::move(then));
        dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED,0),^{
            (*w)();
            dispatch_async(dispatch_get_main_queue(),^{ (*t)(); });
        });
    }
    bool confirmDiscardChanges(const std::string& title, const std::string& message, const std::string& discardLabel) override {
        if (shell_->window_.attachedSheet) return false;
        NSAlert* alert=[[NSAlert alloc] init];
        alert.messageText=ns(title);
        alert.informativeText=ns(message);
        [alert addButtonWithTitle:@"Cancel"];
        [alert addButtonWithTitle:ns(discardLabel)];
        return [alert runModal]==NSAlertSecondButtonReturn;
    }
    void chooseProject(std::function<void(std::string)> done) override {
        NSOpenPanel* panel=[NSOpenPanel openPanel];
        panel.canChooseFiles=YES;panel.canChooseDirectories=YES;panel.allowsMultipleSelection=NO;panel.canCreateDirectories=NO;
        panel.prompt=@"Open Project";
        auto cb=std::make_shared<std::function<void(std::string)>>(std::move(done));
        [panel beginSheetModalForWindow:shell_->window_ completionHandler:^(NSModalResponse result) {
            (*cb)(result==NSModalResponseOK && panel.URL.path.length ? std::string(panel.URL.path.UTF8String) : std::string());
        }];
    }
    void dictationStarted() override { shell_->dictationWeb_ = shell_->currentWeb_; }
    void chooseFolder(const std::string& message, const std::string& startDir, std::function<void(std::string)> done) override {
        NSOpenPanel* panel=[NSOpenPanel openPanel];
        panel.canChooseFiles=NO;panel.canChooseDirectories=YES;panel.canCreateDirectories=YES;panel.allowsMultipleSelection=NO;
        panel.prompt=@"Choose Folder";
        panel.message=ns(message);
        if (!startDir.empty()) panel.directoryURL=[NSURL fileURLWithPath:ns(startDir)];
        auto cb=std::make_shared<std::function<void(std::string)>>(std::move(done));
        [panel beginSheetModalForWindow:shell_->window_ completionHandler:^(NSModalResponse result) {
            (*cb)(result==NSModalResponseOK && panel.URL.path.length ? std::string(panel.URL.path.UTF8String) : std::string());
        }];
    }
    void chooseSaveFile(const std::string& startDir, const std::string& defaultName, std::function<void(std::string)> done) override {
        NSSavePanel* panel=[NSSavePanel savePanel];
        panel.canCreateDirectories=YES;
        panel.nameFieldStringValue=ns(defaultName);
        panel.directoryURL=[NSURL fileURLWithPath:ns(startDir)];
        auto cb=std::make_shared<std::function<void(std::string)>>(std::move(done));
        [panel beginSheetModalForWindow:shell_->window_ completionHandler:^(NSModalResponse result) {
            (*cb)(result==NSModalResponseOK && panel.URL.path.length ? std::string(panel.URL.path.UTF8String) : std::string());
        }];
    }
    void chooseImportSources(const std::string& startDir, std::function<void(std::vector<std::filesystem::path>)> done) override {
        NSOpenPanel* panel=[NSOpenPanel openPanel];
        panel.canChooseFiles=YES;panel.canChooseDirectories=YES;panel.allowsMultipleSelection=YES;panel.canCreateDirectories=NO;
        panel.prompt=@"Import into content";
        panel.message=@"Select files or folders to copy into the project's content directory.";
        panel.directoryURL=[NSURL fileURLWithPath:ns(startDir) isDirectory:YES];
        auto cb=std::make_shared<std::function<void(std::vector<std::filesystem::path>)>>(std::move(done));
        [panel beginSheetModalForWindow:shell_->window_ completionHandler:^(NSModalResponse code) {
            std::vector<std::filesystem::path> sources;
            if (code==NSModalResponseOK) for (NSURL* url in panel.URLs) if (url.isFileURL) sources.emplace_back(url.path.UTF8String);
            (*cb)(std::move(sources));
        }];
    }
    PlayerExitStatus launchGamePlayer(const std::vector<std::string>& args) override {
        const std::filesystem::path bundle=NSBundle.mainBundle.bundlePath.UTF8String;
        const auto exe=studio::studioNetworkPlayer(bundle.parent_path()/"AiverseGame.app/Contents/MacOS/AiverseGame");
        return launchExecutable(exe,args);
    }
    void openProjectStudio(const std::filesystem::path& executable, const std::string& project) override {
        launchExecutable(executable, {"--project", project});
    }
    static PlayerExitStatus launchExecutable(const std::filesystem::path& exe, const std::vector<std::string>& args) {
        NSTask* task=[[NSTask alloc] init];
        task.executableURL=[NSURL fileURLWithPath:ns(exe.string())];
        NSMutableArray<NSString*>* argv=[NSMutableArray array];
        for(const auto& arg:args)[argv addObject:ns(arg)];
        task.arguments=argv;
        NSError* error=nil;
        if(![task launchAndReturnError:&error])throw std::runtime_error(error.localizedDescription.UTF8String);
        return [task]() -> std::optional<int> {
            if(task.running)return std::nullopt;
            return task.terminationStatus;
        };
    }
    void revealInFileManager(const std::filesystem::path& path, bool openFolder) override {
        if(openFolder){[[NSWorkspace sharedWorkspace] openURL:[NSURL fileURLWithPath:ns(path.string()) isDirectory:YES]];return;}
        [[NSWorkspace sharedWorkspace] activateFileViewerSelectingURLs:@[[NSURL fileURLWithPath:ns(path.string())]]];
    }
    std::filesystem::path legacyEditorSettingsDir() override {
        NSString* dir = NSSearchPathForDirectoriesInDomains(NSApplicationSupportDirectory, NSUserDomainMask, YES).firstObject;
        if (!dir) return {};
        return std::filesystem::path(dir.fileSystemRepresentation) / "AIVERSE Editor";
    }
    std::filesystem::path cacheDir() override {
        NSString* caches=NSSearchPathForDirectoriesInDomains(NSCachesDirectory,NSUserDomainMask,YES).firstObject ?: NSTemporaryDirectory();
        return std::filesystem::path(caches.UTF8String);
    }
    void rememberLastProject(const std::string& dir) override {
        [[NSUserDefaults standardUserDefaults] setObject:ns(dir) forKey:@"AiverseLastProject"];
    }
    Json windowRequest(const std::string& method, const Json& params) override { return [shell_ windowRequest:method params:params]; }
    bool hasFloatingWindows() override { return shell_->floatWebs_.count>0; }
    // ImageIO reads what stb_image does not (TIFF, WebP, HEIC).
    bool decodeImage(const std::filesystem::path& file, int& width, int& height, std::vector<uint8_t>& rgba) override {
        @autoreleasepool {
            NSImage* image=[[NSImage alloc] initWithContentsOfFile:ns(file.string())];
            if (!image || image.size.width<1 || image.size.height<1) return false;
            const NSInteger w=NSInteger(image.size.width),h=NSInteger(image.size.height);
            NSBitmapImageRep* rep=[[NSBitmapImageRep alloc] initWithBitmapDataPlanes:nullptr pixelsWide:w pixelsHigh:h bitsPerSample:8
                samplesPerPixel:4 hasAlpha:YES isPlanar:NO colorSpaceName:NSDeviceRGBColorSpace bytesPerRow:w*4 bitsPerPixel:32];
            [NSGraphicsContext saveGraphicsState];
            [NSGraphicsContext setCurrentContext:[NSGraphicsContext graphicsContextWithBitmapImageRep:rep]];
            [image drawInRect:NSMakeRect(0,0,w,h) fromRect:NSZeroRect operation:NSCompositingOperationCopy fraction:1.0];
            [NSGraphicsContext restoreGraphicsState];
            width=int(w);height=int(h);
            rgba.assign(rep.bitmapData,rep.bitmapData+size_t(w)*size_t(h)*4);
            return true;
        }
    }
    void log(const std::string& message) override { NSLog(@"%s",message.c_str()); }
private:
    __unsafe_unretained StudioShell* shell_;
    void* sceneLayer_=nullptr;
    void* gameLayer_=nullptr;
    void* assetLayers_[int(studio::StudioSurface::Count)-int(studio::StudioSurface::Material)]={};
    void* fxLayer_=nullptr;
};

// Developer automation for scene-only wind-mask screenshots (no desktop or
// WebView capture): AIVERSE_STUDIO_WIND_SHOTS=<dir> walks preview states,
// grabs the Metal Scene drawable as PPM files, then quits when
// AIVERSE_STUDIO_SHOTS_EXIT=1. Camera: AIVERSE_STUDIO_SHOT_CAMERA="x y z dist yaw pitch".
// Splash art (AIVERSE_STUDIO_SKY_SHOTS=<dir>): the engine's PhysicalSky at several times of day, captured
// from an own window like the wind shots. The images become the default project splash backgrounds.
struct SkyShotRunner {
    std::string dir;
    int stage=0,frames=0;bool waiting=false,done=false;
    std::shared_ptr<bool> captured=std::make_shared<bool>(false);
    NSWindow* window=nil;NSView* view=nil;
    std::unique_ptr<studio::StudioScene> scene;
    struct Stage { const char* name; float elevation, azimuth, coverage, r, g, b, pitch; };
    static const std::vector<Stage>& stages() {
        static const std::vector<Stage> list={
            {"sky-dawn",5,80,.42f,1,.78f,.58f,-.34f},
            {"sky-golden",9,250,.55f,1,.84f,.62f,-.34f},
            {"sky-day",38,200,.6f,1,.97f,.92f,-.42f},
            {"sky-dusk",3,265,.5f,1,.6f,.42f,-.3f},
            {"sky-clear",14,140,.85f,1,.92f,.8f,-.4f},
            {"sky-storm",22,30,.3f,.9f,.92f,1,-.36f},
        };
        return list;
    }
    static SkyShotRunner& get() {
        static SkyShotRunner r=[]{SkyShotRunner s;if(const char* d=std::getenv("AIVERSE_STUDIO_SKY_SHOTS"))s.dir=d;return s;}();
        return r;
    }
};
struct WindShotRunner {
    std::string dir;
    bool exitAfter=false;
    float camera[6]={0,0,0,0,0,0};
    bool hasCamera=false;
    int stage=-1, frames=0;
    bool waiting=false, done=false;
    std::shared_ptr<bool> captured=std::make_shared<bool>(false);
    // Own window + Metal scene: independent of the user's saved dock layout.
    NSWindow* window=nil;
    NSView* view=nil;
    std::unique_ptr<studio::StudioScene> scene;
    struct Stage { const char* name; const char* patch; int settle; };
    static const std::vector<Stage>& stages() {
        static const std::vector<Stage> list={
            {"0-calm",R"({"enabled":false,"debug":"off"})",40},
            {"1-wind-a",R"({"enabled":true,"speed":12,"direction":0,"gust":0.5,"scale":0.05,"debug":"off"})",60},
            {"2-wind-b",R"({"debug":"off"})",23},
            {"3-parts",R"({"debug":"parts"})",30},
            {"4-depth",R"({"debug":"depth"})",30},
            {"5-weight",R"({"debug":"weight"})",30},
            {"6-skeleton",R"({"debug":"skeleton"})",30},
        };
        return list;
    }
    static WindShotRunner& get() {
        static WindShotRunner r=[]{
            WindShotRunner w;
            if(const char* d=std::getenv("AIVERSE_STUDIO_WIND_SHOTS"))w.dir=d;
            if(const char* e=std::getenv("AIVERSE_STUDIO_SHOTS_EXIT"))w.exitAfter=std::string(e)=="1";
            if(const char* c=std::getenv("AIVERSE_STUDIO_SHOT_CAMERA"))
                w.hasCamera=std::sscanf(c,"%f %f %f %f %f %f",&w.camera[0],&w.camera[1],&w.camera[2],&w.camera[3],&w.camera[4],&w.camera[5])==6;
            return w;
        }();
        return r;
    }
};
void writePpm(const std::string& path,int w,int h,const std::vector<uint8_t>& rgba) {
    if (FILE* f=std::fopen(path.c_str(),"wb")) {
        std::fprintf(f,"P6\n%d %d\n255\n",w,h);
        for (size_t i=0;i+3<rgba.size();i+=4) std::fwrite(&rgba[i],1,3,f);
        std::fclose(f);
    }
}
}

// Last left press inside the WebView: HTML asks the host to start a title-bar
// window drag asynchronously, after the press was already delivered.
static NSEvent* gStudioLastWebMouseDown=nil;
@implementation StudioFileDropWebView
- (void)mouseDown:(NSEvent*)event { gStudioLastWebMouseDown=event; [super mouseDown:event]; }
- (NSDragOperation)draggingEntered:(id<NSDraggingInfo>)sender {
    NSArray* urls=[[sender draggingPasteboard]
        readObjectsForClasses:@[[NSURL class]]
        options:@{NSPasteboardURLReadingFileURLsOnlyKey:@YES}];
    if(urls.count && self.shell)return NSDragOperationCopy;
    return [super draggingEntered:sender];
}
- (NSDragOperation)draggingUpdated:(id<NSDraggingInfo>)sender {
    NSArray* urls=[[sender draggingPasteboard]
        readObjectsForClasses:@[[NSURL class]]
        options:@{NSPasteboardURLReadingFileURLsOnlyKey:@YES}];
    if(urls.count && self.shell)return NSDragOperationCopy;
    return [super draggingUpdated:sender];
}
- (BOOL)performDragOperation:(id<NSDraggingInfo>)sender {
    NSArray<NSURL*>* urls=[[sender draggingPasteboard]
        readObjectsForClasses:@[[NSURL class]]
        options:@{NSPasteboardURLReadingFileURLsOnlyKey:@YES}];
    if(urls.count && self.shell){
        [self.shell importDroppedFileURLs:urls atWindowPoint:sender.draggingLocation fromWeb:self];
        return YES; // native plan + explicit UI confirmation, NEVER direct file copy
    }
    return [super performDragOperation:sender];
}
@end

@implementation StudioMetalView
- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }
- (BOOL)wantsUpdateLayer { return YES; }
- (CALayer*)makeBackingLayer { return [CAMetalLayer layer]; }
- (instancetype)initWithFrame:(NSRect)frame {
    if ((self = [super initWithFrame:frame])) {
        self.wantsLayer = YES;
        self.layerContentsRedrawPolicy = NSViewLayerContentsRedrawDuringViewResize;
        // Assets panel drags carry "aiverse-asset:<content path>" as plain text.
        [self registerForDraggedTypes:@[NSPasteboardTypeString]];
    }
    return self;
}
- (studio::StudioHost*)core { return [self.shell core]; }
- (CGFloat)w { return self.bounds.size.width; }
- (CGFloat)h { return self.bounds.size.height; }
- (void)updateTrackingAreas {
    [super updateTrackingAreas];
    if(hoverTracking_)[self removeTrackingArea:hoverTracking_];
    hoverTracking_=[[NSTrackingArea alloc] initWithRect:NSZeroRect options:
        NSTrackingMouseMoved|NSTrackingMouseEnteredAndExited|NSTrackingActiveInKeyWindow|NSTrackingInVisibleRect owner:self userInfo:nil];
    [self addTrackingArea:hoverTracking_];
}
- (void)mouseMoved:(NSEvent*)event {
    // Scene hover highlight, throttled (a pick per ~50 ms).
    const NSTimeInterval now=event.timestamp;
    if (now-lastHover_<0.05) return;
    lastHover_=now;
    const NSPoint point=[self convertPoint:event.locationInWindow fromView:nil];
    if (auto* core=[self core]) core->hover(point.x,point.y,[self w],[self h]);
}
- (void)mouseExited:(NSEvent*)event { (void)event; if (auto* core=[self core]) core->hover(-1,-1,[self w],[self h]); }
static NSString* const kStudioAssetDragPrefix=@"aiverse-asset:";
// Rules panel drags carry "aiverse-type:<type id>": a new entity of that type (prefab-like).
static NSString* const kStudioTypeDragPrefix=@"aiverse-type:";
static NSString* studioDraggedAsset(id<NSDraggingInfo> sender) {
    NSString* text=[[sender draggingPasteboard] stringForType:NSPasteboardTypeString];
    return [text hasPrefix:kStudioAssetDragPrefix] ? [text substringFromIndex:kStudioAssetDragPrefix.length] :
           [text hasPrefix:kStudioTypeDragPrefix] ? text : nil;
}
- (NSDragOperation)draggingEntered:(id<NSDraggingInfo>)sender { return studioDraggedAsset(sender) ? NSDragOperationCopy : NSDragOperationNone; }
- (NSDragOperation)draggingUpdated:(id<NSDraggingInfo>)sender { return studioDraggedAsset(sender) ? NSDragOperationCopy : NSDragOperationNone; }
- (BOOL)performDragOperation:(id<NSDraggingInfo>)sender {
    NSString* asset=studioDraggedAsset(sender);
    if (!asset.length || asset.length>1024 || ![self core]) return NO;
    const NSPoint point=[self convertPoint:sender.draggingLocation fromView:nil];
    if ([asset hasPrefix:kStudioTypeDragPrefix])
        return [self core]->placeType([asset substringFromIndex:kStudioTypeDragPrefix.length].UTF8String,point.x,point.y,[self w],[self h]);
    return [self core]->placeAsset(asset.UTF8String,point.x,point.y,[self w],[self h]);
}
- (BOOL)acceptsFirstMouse:(NSEvent*)event {
    if ([self isMemberOfClass:[StudioMetalView class]] && [self core] && ([self core]->scenePickerArmed() || [self core]->brushArmed())) return YES;
    return [super acceptsFirstMouse:event];
}
- (void)primaryMouse:(NSEvent*)event phase:(int)phase {
    auto* core=[self core];
    if (!core) return;
    const NSPoint point=[self convertPoint:event.locationInWindow fromView:nil];
    const auto flags=event.modifierFlags;
    studio::StudioSceneMods mods;
    mods.shift=(flags & NSEventModifierFlagShift)!=0; mods.ctrl=(flags & NSEventModifierFlagControl)!=0;
    mods.alt=(flags & NSEventModifierFlagOption)!=0; mods.command=(flags & NSEventModifierFlagCommand)!=0;
    core->sceneMouse(phase,0,point.x,point.y,event.deltaX,event.deltaY,[self w],[self h],mods);
}
- (void)mouseDown:(NSEvent*)event {
    [self.window makeFirstResponder:self];
    [self primaryMouse:event phase:0];
}
- (void)mouseDragged:(NSEvent*)event { [self primaryMouse:event phase:1]; }
- (void)mouseUp:(NSEvent*)event { [self primaryMouse:event phase:2]; }
- (void)rightMouseDown:(NSEvent*)event {
    [self.window makeFirstResponder:self];
    sceneMenuPress_ = (event.modifierFlags & NSEventModifierFlagShift) != 0;
}
- (void)rightMouseUp:(NSEvent*)event {
    if (!sceneMenuPress_) return;
    sceneMenuPress_ = NO;
    StudioShell* shell = self.shell;
    if (!shell || self != shell->viewport_ || !self.window) return;
    WKWebView* web = shell->web_;
    if (web.window != self.window) {
        web = nil;
        for (WKWebView* candidate in shell->floatWebs_.allValues) {
            if (candidate.window == self.window) { web = candidate; break; }
        }
    }
    if (!web) return;
    const NSPoint point = [web convertPoint:event.locationInWindow fromView:nil];
    const double x = point.x, y = web.isFlipped ? point.y : web.bounds.size.height - point.y;
    if (!std::isfinite(x) || !std::isfinite(y)) return;
    [self.window makeFirstResponder:web];
    NSString* js = [NSString stringWithFormat:
        @"document.elementFromPoint(%f,%f)?.closest('.viewport')?.dispatchEvent(new MouseEvent('contextmenu',"
         "{bubbles:true,cancelable:true,shiftKey:true,button:2,clientX:%f,clientY:%f}))", x,y,x,y];
    [web evaluateJavaScript:js completionHandler:nil];
}
- (void)rightMouseDragged:(NSEvent*)event {
    if (sceneMenuPress_ || ![self core]) return;
    if (event.modifierFlags & NSEventModifierFlagOption) [self core]->zoom((event.deltaX+event.deltaY)*0.6);
    else [self core]->look(event.deltaX,event.deltaY);
}
- (void)otherMouseDragged:(NSEvent*)event { if (auto* core=[self core]) core->pan(event.deltaX,event.deltaY); }
- (void)keyUp:(NSEvent*)event {
    if (event.keyCode==9 && [self core]) { [self core]->setPivotSnap(false); return; }
    [super keyUp:event];
}
- (void)keyDown:(NSEvent*)event {
    // Physical key codes (ANSI positions): shortcuts work with any keyboard layout.
    using K=studio::StudioSceneKey;
    K key=K::Other;
    switch (event.keyCode) {
        case 0: key=K::A; break; case 1: key=K::S; break; case 2: key=K::D; break; case 3: key=K::F; break;
        case 12: key=K::Q; break; case 13: key=K::W; break; case 14: key=K::E; break; case 15: key=K::R; break;
        case 9: key=K::V; break; case 123: key=K::Left; break; case 124: key=K::Right; break;
        case 125: key=K::Down; break; case 126: key=K::Up; break; case 115: key=K::Home; break;
        case 51: case 117: key=K::Delete; break; case 53: key=K::Escape; break;
        case 33: key=K::BracketLeft; break; case 30: key=K::BracketRight; break;
        default: break;
    }
    const NSEventModifierFlags f=event.modifierFlags;
    studio::StudioSceneMods mods;
    mods.shift=(f & NSEventModifierFlagShift)!=0; mods.ctrl=(f & NSEventModifierFlagControl)!=0;
    mods.alt=(f & NSEventModifierFlagOption)!=0; mods.command=(f & NSEventModifierFlagCommand)!=0;
    // WASD/QE fly only while the right button is held; arrows always move.
    const bool flying=([NSEvent pressedMouseButtons] & (1u<<1))!=0;
    if (key!=K::Other && [self core] && [self core]->sceneKey(key,true,mods,flying)) return;
    [super keyDown:event];
}
- (void)scrollWheel:(NSEvent*)event {
    const CGFloat wheel = event.scrollingDeltaY * (event.hasPreciseScrollingDeltas ? 0.12 : 1.0);
    if (auto* core=[self core]) core->zoom(wheel);
}
@end

@implementation StudioFxView
// A dedicated VFX render target is never a map editor. Mouse gestures can
// only manipulate its isolated camera; no pick, brush or asset drop.
- (void)mouseDown:(NSEvent*)event {
    self.pressPoint=event.locationInWindow;self.clickPending=self.surface==studio::StudioSurface::Nature;
    [self.window makeFirstResponder:self];
}
- (void)mouseUp:(NSEvent*)event {
    if(self.clickPending && NSPointInRect(event.locationInWindow,NSInsetRect(NSMakeRect(self.pressPoint.x,self.pressPoint.y,0,0),-3,-3))){
        const NSPoint p=[self convertPoint:event.locationInWindow fromView:nil];
        if(auto* core=[self core])core->natureClick(p.x,p.y,self.bounds.size.width,self.bounds.size.height);
    }
    self.clickPending=NO;
}
- (void)rightMouseDown:(NSEvent*)event {(void)event;}
- (void)rightMouseUp:(NSEvent*)event {(void)event;}
- (void)otherMouseDown:(NSEvent*)event {(void)event;}
- (void)otherMouseUp:(NSEvent*)event {(void)event;}
- (void)mouseDragged:(NSEvent*)event {
    if(self.clickPending && hypot(event.locationInWindow.x-self.pressPoint.x,event.locationInWindow.y-self.pressPoint.y)<3)return;
    self.clickPending=NO;
    if (auto* core=[self core])core->fxCameraOrbit(self.surface,event.deltaX,event.deltaY);
}
- (void)rightMouseDragged:(NSEvent*)event {
    if (auto* core=[self core])core->fxCameraOrbit(self.surface,event.deltaX,event.deltaY);
}
- (void)otherMouseDragged:(NSEvent*)event {
    if (auto* core=[self core])core->fxCameraOrbit(self.surface,event.deltaX,event.deltaY);
}
- (void)scrollWheel:(NSEvent*)event {
    const CGFloat wheel=event.scrollingDeltaY*(event.hasPreciseScrollingDeltas?.12:1.0);
    if(auto* core=[self core])core->fxCameraZoom(self.surface,wheel);
}
- (void)mouseMoved:(NSEvent*)event {(void)event;}
- (void)mouseExited:(NSEvent*)event {(void)event;}
- (NSDragOperation)draggingEntered:(id<NSDraggingInfo>)sender {
    (void)sender;return NSDragOperationNone;
}
- (NSDragOperation)draggingUpdated:(id<NSDraggingInfo>)sender {
    (void)sender;return NSDragOperationNone;
}
- (BOOL)performDragOperation:(id<NSDraggingInfo>)sender {
    (void)sender;return NO;
}
- (void)keyDown:(NSEvent*)event {(void)event;}
- (void)keyUp:(NSEvent*)event {(void)event;}
@end

@implementation StudioGameView
// The hosted game never accepts Scene asset drops.
- (NSDragOperation)draggingEntered:(id<NSDraggingInfo>)sender { (void)sender; return NSDragOperationNone; }
- (NSDragOperation)draggingUpdated:(id<NSDraggingInfo>)sender { (void)sender; return NSDragOperationNone; }
- (BOOL)performDragOperation:(id<NSDraggingInfo>)sender { (void)sender; return NO; }
- (void)updateTrackingAreas {
    [super updateTrackingAreas];
    if(tracking_)[self removeTrackingArea:tracking_];
    tracking_=[[NSTrackingArea alloc] initWithRect:NSZeroRect options:
        NSTrackingMouseMoved|NSTrackingActiveInKeyWindow|NSTrackingInVisibleRect
        owner:self userInfo:nil];
    [self addTrackingArea:tracking_];
}
- (void)sendMouse:(NSEvent*)event type:(aiverse::platform::EventType)type {
    if(type==aiverse::platform::EventType::MouseDown)[self.window makeFirstResponder:self];
    const auto p=[self convertPoint:event.locationInWindow fromView:nil];
    aiverse::platform::Event e;e.type=type;e.x=p.x;e.y=p.y;e.button=int(event.buttonNumber);
    e.deltaX=event.deltaX;e.deltaY=event.deltaY;
    // Cocoa's scrolling accessors are only valid for scroll-wheel events.
    if(type==aiverse::platform::EventType::MouseWheel) {
        e.wheelX=event.scrollingDeltaX;e.wheelY=event.scrollingDeltaY;
    }
    e.mods=((event.modifierFlags & NSEventModifierFlagShift)?aiverse::platform::kModShift:0)|
        ((event.modifierFlags & NSEventModifierFlagControl)?aiverse::platform::kModCtrl:0)|
        ((event.modifierFlags & NSEventModifierFlagOption)?aiverse::platform::kModAlt:0)|
        ((event.modifierFlags & NSEventModifierFlagCommand)?aiverse::platform::kModSuper:0);
    if (auto* core=[self core]) core->gameEvent(e);
}
- (void)mouseDown:(NSEvent*)e { [self sendMouse:e type:aiverse::platform::EventType::MouseDown]; }
- (void)rightMouseDown:(NSEvent*)e { [self mouseDown:e]; }
- (void)otherMouseDown:(NSEvent*)e { [self mouseDown:e]; }
- (void)mouseUp:(NSEvent*)e { [self sendMouse:e type:aiverse::platform::EventType::MouseUp]; }
- (void)rightMouseUp:(NSEvent*)e { [self mouseUp:e]; }
- (void)otherMouseUp:(NSEvent*)e { [self mouseUp:e]; }
- (void)mouseMoved:(NSEvent*)e { [self sendMouse:e type:aiverse::platform::EventType::MouseMove]; }
- (void)mouseDragged:(NSEvent*)e { [self mouseMoved:e]; }
- (void)rightMouseDragged:(NSEvent*)e { [self mouseMoved:e]; }
- (void)otherMouseDragged:(NSEvent*)e { [self mouseMoved:e]; }
- (void)scrollWheel:(NSEvent*)e { [self sendMouse:e type:aiverse::platform::EventType::MouseWheel]; }
- (void)sendKey:(NSEvent*)event down:(BOOL)down {
    using namespace aiverse::platform;
    Event e;e.type=down?EventType::KeyDown:EventType::KeyUp;e.repeat=event.isARepeat;
    NSString* text=event.charactersIgnoringModifiers;
    e.key=studio::studioGameKey(text.UTF8String?text.UTF8String:"");
    switch(event.keyCode) {
        case 53:e.key=Key::Escape;break;case 36:e.key=Key::Enter;break;case 48:e.key=Key::Tab;break;
        case 51:e.key=Key::Backspace;break;case 117:e.key=Key::Delete;break;
        case 123:e.key=Key::Left;break;case 124:e.key=Key::Right;break;
        case 125:e.key=Key::Down;break;case 126:e.key=Key::Up;break;
        case 115:e.key=Key::Home;break;case 119:e.key=Key::End;break;
        case 116:e.key=Key::PageUp;break;case 121:e.key=Key::PageDown;break;
        case 122:e.key=Key::F1;break;case 120:e.key=Key::F2;break;
        case 99:e.key=Key::F3;break;case 118:e.key=Key::F4;break;
        case 96:e.key=Key::F5;break;case 97:e.key=Key::F6;break;
        case 98:e.key=Key::F7;break;case 100:e.key=Key::F8;break;
        case 101:e.key=Key::F9;break;case 109:e.key=Key::F10;break;
        case 103:e.key=Key::F11;break;case 111:e.key=Key::F12;break;
    }
    e.mods=((event.modifierFlags & NSEventModifierFlagShift)?kModShift:0)|
        ((event.modifierFlags & NSEventModifierFlagControl)?kModCtrl:0)|
        ((event.modifierFlags & NSEventModifierFlagOption)?kModAlt:0)|
        ((event.modifierFlags & NSEventModifierFlagCommand)?kModSuper:0);
    if (![self core]) return;
    [self core]->gameEvent(e);
    if(down && !(e.mods&(kModCtrl|kModSuper))) {
        NSString* chars=event.characters;
        for(NSUInteger i=0;i<chars.length;++i) {
            uint32_t c=[chars characterAtIndex:i];
            if(c>=0xd800 && c<=0xdbff && i+1<chars.length) {
                uint32_t low=[chars characterAtIndex:i+1];
                if(low>=0xdc00 && low<=0xdfff){c=0x10000+((c-0xd800)<<10)+(low-0xdc00);++i;}
            }
            if(c>=32 && !(c>=0xd800 && c<=0xdfff) && !(c>=0xf700 && c<=0xf8ff)) {
                Event input;input.type=EventType::Char;input.codepoint=c;[self core]->gameEvent(input);
            }
        }
    }
}
- (void)keyDown:(NSEvent*)e { [self sendKey:e down:YES]; }
- (void)keyUp:(NSEvent*)e { [self sendKey:e down:NO]; }
- (BOOL)resignFirstResponder {
    aiverse::platform::Event e;e.type=aiverse::platform::EventType::FocusLost;
    if (auto* core=[self core]) core->gameEvent(e);
    return [super resignFirstResponder];
}
@end

@implementation StudioShell
- (instancetype)initWithProject:(const std::string&)project dev:(BOOL)dev composite:(BOOL)composite listenPort:(int)listenPort {
    if ((self=[super init])) {
        // The Metal-under-HTML path requires WebKit to leave its page canvas unpainted.
        htmlComposite_=composite && [WKWebView instancesRespondToSelector:NSSelectorFromString(@"_setDrawsBackground:")];
        if(composite && !htmlComposite_) NSLog(@"[studio-web] Transparent page canvas unavailable; using native Scene input");
        const std::string root=aiverse::platform::resolveEngineDir(AIVERSE_STUDIO_SOURCE_DIR,nullptr);
        aiverse::platform::setEngineAssetsDir([[[NSBundle mainBundle] resourcePath] stringByAppendingPathComponent:@"engine"].UTF8String);
        platform_=std::make_unique<MacPlatform>(self);
        core_=std::make_unique<studio::StudioHost>(*platform_,root,htmlComposite_);
        if (listenPort && !core_->session().listenForAgent(listenPort))
            throw std::runtime_error("Could not start the authenticated Studio agent listener");
        const std::string exeDir=[[[NSBundle mainBundle] executablePath] stringByDeletingLastPathComponent].UTF8String;
        core_->session().setBuildTool(aiverse::editor::findBuildTool(exeDir,root));
        core_->session().setMcpTool(exeDir+"/aiversemcp");
        core_->setPendingProject(project);
        NSURL* bundled=[[NSBundle mainBundle] URLForResource:@"index" withExtension:@"html" subdirectory:@"studio-web"];
        dist_=bundled ? [[[bundled URLByDeletingLastPathComponent] path] stringByStandardizingPath]
                      : [ns(root+"/apps/studio-web/dist") stringByStandardizingPath];
        if (dev) devURL_=[NSURL URLWithString:@"http://127.0.0.1:4173/"];
    }
    return self;
}
- (studio::StudioHost*)core { return core_.get(); }
- (void)webView:(WKWebView*)webView runOpenPanelWithParameters:(WKOpenPanelParameters*)parameters
 initiatedByFrame:(WKFrameInfo*)frame completionHandler:(void (^)(NSArray<NSURL*>*))completionHandler {
    if (!frame.mainFrame || ![self trusted:frame.request.URL] || !webView.window) {
        completionHandler(nil);
        return;
    }
    NSOpenPanel* panel = [NSOpenPanel openPanel];
    panel.canChooseFiles = YES;
    panel.canChooseDirectories = NO;
    panel.allowsMultipleSelection = parameters.allowsMultipleSelection;
    [panel beginSheetModalForWindow:webView.window completionHandler:^(NSModalResponse response) {
        completionHandler(response == NSModalResponseOK ? panel.URLs : nil);
    }];
}
- (BOOL)trusted:(NSURL*)url {
    if (!url) return NO;
    // file:// navigation is deliberately disallowed. All production assets
    // must resolve through the read-only, same-origin app bundle scheme.
    if ([url.scheme isEqualToString:@"aiverse-studio"] &&
        [url.host isEqualToString:@"shell"])
        return [url.path isEqualToString:@"/index.html"] || [url.path isEqualToString:@"/"];
    return devURL_ && [url.scheme isEqualToString:@"http"] &&
           [url.host isEqualToString:@"127.0.0.1"] && url.port.integerValue==4173;
}
- (void)application:(NSApplication*)app openFiles:(NSArray<NSString*>*)files {
    for (NSString* file in files) {
        try {
            const std::string path=file.fileSystemRepresentation;
            std::string error;
            if (!aiverse::editor::isProjectFilePath(path))
                throw std::runtime_error("Select an AIVERSE project file.");
            const auto dir=aiverse::editor::resolveProjectDir(path,&error);
            if (dir.empty()) throw std::runtime_error(error);
            if (!mainWebReady_ && !hasFinderProject_) {
                core_->setPendingProject(dir);
                hasFinderProject_=YES;
            } else if (mainWebReady_ && core_->session().workspace().project.dir==dir) {
                [window_ makeKeyAndOrderFront:nil];
            } else {
                platform_->openProjectStudio(NSBundle.mainBundle.executablePath.fileSystemRepresentation,dir);
            }
        } catch (const std::exception& e) {
            NSAlert* alert=[[NSAlert alloc] init];
            alert.messageText=@"Could not open project";
            alert.informativeText=ns(e.what());
            [alert runModal];
            [app replyToOpenOrPrint:NSApplicationDelegateReplyFailure];
            return;
        }
    }
    [app replyToOpenOrPrint:NSApplicationDelegateReplySuccess];
}
- (void)applicationDidFinishLaunching:(NSNotification*)note {
    (void)note;
    window_=[[NSWindow alloc] initWithContentRect:NSMakeRect(0,0,1440,900)
        styleMask:(NSWindowStyleMaskTitled|NSWindowStyleMaskClosable|NSWindowStyleMaskMiniaturizable|NSWindowStyleMaskResizable|NSWindowStyleMaskFullSizeContentView)
        backing:NSBackingStoreBuffered defer:NO];
    window_.title=@"AIVERSE";
    window_.titleVisibility=NSWindowTitleHidden;
    window_.titlebarAppearsTransparent=YES;
    window_.delegate=self;
    [window_ center];

    // Built React/Vite uses ES modules. file:// has an opaque origin on WebKit,
    // so load it through a local WKURLSchemeHandler instead of loadFileURL.
    // The handler reads ONLY the immutable bundled studio-web folder.
    if (!devURL_) assets_=[[StudioAssetSchemeHandler alloc] initWithRoot:dist_];
    floatWindows_=[NSMutableDictionary dictionary];
    floatWebs_=[NSMutableDictionary dictionary];
    web_=[self makeWebViewWithFrame:window_.contentView.bounds];
    [window_.contentView addSubview:web_];

    viewport_=[[StudioMetalView alloc] initWithFrame:NSZeroRect];
    viewport_.shell=self;
    viewport_.hidden=YES;
    [window_.contentView addSubview:viewport_
        positioned:(htmlComposite_ ? NSWindowBelow : NSWindowAbove) relativeTo:web_];
    fxViewport_=[[StudioFxView alloc] initWithFrame:NSZeroRect];
    fxViewport_.surface=studio::StudioSurface::Fx;fxViewport_.shell=self;fxViewport_.hidden=YES;
    [window_.contentView addSubview:fxViewport_
        positioned:(htmlComposite_?NSWindowBelow:NSWindowAbove) relativeTo:web_];
    for(int i=0;i<int(studio::StudioSurface::Count)-int(studio::StudioSurface::Material);++i){
        NSView* view;
        const auto surface=static_cast<studio::StudioSurface>(i+int(studio::StudioSurface::Material));
        if(surface==studio::StudioSurface::Nature||surface==studio::StudioSurface::GpuEmitter||surface==studio::StudioSurface::EffectFile){
            StudioFxView* effect=[[StudioFxView alloc] initWithFrame:NSZeroRect];
            effect.surface=surface;effect.shell=self;view=effect;
        }else view=i>=3&&i<=4?[[StudioCharacterSurface alloc] initWithFrame:NSZeroRect]:[[NSView alloc] initWithFrame:NSZeroRect];
        view.wantsLayer=YES;view.layer=[CAMetalLayer layer];view.hidden=YES;
        if(i>=3&&i<=4){
            StudioCharacterSurface* character=(StudioCharacterSurface*)view;
            character.photo=[CALayer layer];character.photo.hidden=YES;
            view.layer.masksToBounds=YES;
            [view.layer addSublayer:character.photo];
            character.brushRing=[CALayer layer];character.brushRing.hidden=YES;
            character.brushRing.borderWidth=1.5;
            character.brushRing.borderColor=NSColor.whiteColor.CGColor;
            [view.layer addSublayer:character.brushRing];
        }
        assetViews_[i]=view;
        [window_.contentView addSubview:view positioned:NSWindowAbove relativeTo:web_];
    }
    gameViewport_=[[StudioGameView alloc] initWithFrame:NSZeroRect];
    gameViewport_.shell=self;gameViewport_.hidden=YES;
    gameChrome_=[[StudioDeviceChromeView alloc] initWithFrame:NSZeroRect];
    gameChrome_.autoresizingMask=NSViewWidthSizable|NSViewHeightSizable;
    [gameViewport_ addSubview:gameChrome_];
    [window_.contentView addSubview:gameViewport_ positioned:NSWindowAbove relativeTo:web_];
    static_cast<MacPlatform*>(platform_.get())->setLayers(
        (__bridge void*)viewport_.layer,(__bridge void*)gameViewport_.layer,
        (__bridge void*)fxViewport_.layer);
    StudioWindowChrome* chrome=[[StudioWindowChrome alloc]
        initWithFrame:NSMakeRect(0,window_.contentView.bounds.size.height-44,80,44)];
    chrome.autoresizingMask=NSViewMinYMargin;
    NSInteger buttonIndex=0;
    for(NSNumber* kind in @[@(NSWindowCloseButton),@(NSWindowMiniaturizeButton),@(NSWindowZoomButton)]) {
        NSWindowButton buttonKind=(NSWindowButton)kind.integerValue;
        [window_ standardWindowButton:buttonKind].hidden=YES;
        NSButton* button=[NSWindow standardWindowButton:buttonKind forStyleMask:window_.styleMask];
        button.target=window_;
        button.action=buttonKind==NSWindowCloseButton ? @selector(performClose:) :
            (buttonKind==NSWindowMiniaturizeButton ? @selector(performMiniaturize:) : @selector(performZoom:));
        button.frame=NSMakeRect(14+20*buttonIndex++,15,14,14);
        [chrome addSubview:button];
    }
    [window_.contentView addSubview:chrome positioned:NSWindowAbove relativeTo:nil];
    window_.acceptsMouseMovedEvents=YES;
    [self installEditMenu];
    [window_ makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];
    if (devURL_) [web_ loadRequest:[NSURLRequest requestWithURL:devURL_]];
    else {
        NSString* entry=[dist_ stringByAppendingPathComponent:@"index.html"];
        if (![[NSFileManager defaultManager] fileExistsAtPath:entry]) {
            NSLog(@"[studio-web] FATAL: expected bundled HTML5 entry at %@",entry);
        } else {
            NSLog(@"[studio-web] loading from bundled scheme, root=%@",dist_);
        }
        NSURL* url=[NSURL URLWithString:@"aiverse-studio://shell/index.html"];
        [web_ loadRequest:[NSURLRequest requestWithURL:url]];
    }
    timer_=[NSTimer timerWithTimeInterval:1.0/60 target:self selector:@selector(tick) userInfo:nil repeats:YES];
    [[NSRunLoop mainRunLoop] addTimer:timer_ forMode:NSRunLoopCommonModes];
}
// Edit menu: without it AppKit has no Cmd+X / Cmd+C / Cmd+V / Cmd+A key equivalents, so text could not be
// copied from the Console or pasted into fields. The actions go to the first responder (the focused WKWebView);
// the page sees the key first, so Studio's own shortcuts keep working. Undo stays with Studio (no menu item).
- (void)installEditMenu {
    NSMenu* bar=NSApp.mainMenu;
    if (!bar || bar.numberOfItems==0) { // the first item is always the application menu
        bar=[[NSMenu alloc] init];
        NSMenuItem* appItem=[[NSMenuItem alloc] init];
        NSMenu* appMenu=[[NSMenu alloc] init];
        [appMenu addItemWithTitle:@"Quit AIVERSE" action:@selector(terminate:) keyEquivalent:@"q"];
        appItem.submenu=appMenu;[bar addItem:appItem];
        NSApp.mainMenu=bar;
    }
    if ([bar itemWithTitle:@"Edit"]) return;
    NSMenuItem* root=[[NSMenuItem alloc] initWithTitle:@"Edit" action:nil keyEquivalent:@""];
    NSMenu* edit=[[NSMenu alloc] initWithTitle:@"Edit"];
    [edit addItemWithTitle:@"Cut" action:@selector(cut:) keyEquivalent:@"x"];
    [edit addItemWithTitle:@"Copy" action:@selector(copy:) keyEquivalent:@"c"];
    [edit addItemWithTitle:@"Paste" action:@selector(paste:) keyEquivalent:@"v"];
    [edit addItem:[NSMenuItem separatorItem]];
    [edit addItemWithTitle:@"Select All" action:@selector(selectAll:) keyEquivalent:@"a"];
    root.submenu=edit;
    [bar insertItem:root atIndex:1];
}
// Every Studio WebView (main window and floating panels) shares one bridge and one session.
- (WKWebView*)makeWebViewWithFrame:(NSRect)frame {
    WKWebViewConfiguration* cfg=[[WKWebViewConfiguration alloc] init];
    [cfg.userContentController addScriptMessageHandler:self name:@"aiverse"];
    if (assets_) [cfg setURLSchemeHandler:assets_ forURLScheme:@"aiverse-studio"];
    WKWebView* web=nil;
    if (htmlComposite_) {
        web=[[StudioTransparentWebView alloc] initWithFrame:frame configuration:cfg];
        [web setValue:@NO forKey:@"drawsBackground"];
        if (@available(macOS 12.0, *)) web.underPageBackgroundColor=[NSColor clearColor];
    } else web=[[StudioFileDropWebView alloc] initWithFrame:frame configuration:cfg];
    ((StudioFileDropWebView*)web).shell=self;
    [web registerForDraggedTypes:@[NSPasteboardTypeFileURL]];
    web.navigationDelegate=self;
    web.UIDelegate=self;
    web.autoresizingMask=NSViewWidthSizable|NSViewHeightSizable;
    return web;
}
- (void)evaluateInAllWebs:(NSString*)js {
    [web_ evaluateJavaScript:js completionHandler:nil];
    for (WKWebView* web in floatWebs_.allValues) [web evaluateJavaScript:js completionHandler:nil];
}
static NSString* studioFloatPanel(NSWindow* window,NSDictionary<NSString*,NSWindow*>* floats) {
    for (NSString* key in floats) if (floats[key]==window) return key;
    return nil;
}
- (void)sendFloatFrame:(NSWindow*)window {
    NSString* panel=studioFloatPanel(window,floatWindows_);
    if (!panel || closingFloats_) return;
    const NSRect f=window.frame;
    Json payload=Json::object();
    payload.set("window",Json::string(panel.UTF8String));
    payload.set("x",Json::number(double(f.origin.x),1));
    payload.set("y",Json::number(double(f.origin.y),1));
    payload.set("width",Json::number(double(f.size.width),1));
    payload.set("height",Json::number(double(f.size.height),1));
    core_->sendEventPayload("window.floatFrame",payload);
}
- (Json)windowRequest:(const std::string&)method params:(const Json&)params {
    Json result=Json::object();
    if (method=="studio.windowDrag") {
        // Title-bar drag from HTML: the WebView covers the transparent title bar.
        NSEvent* current=[NSApp currentEvent];
        NSEvent* press=(current && (current.type==NSEventTypeLeftMouseDown || current.type==NSEventTypeLeftMouseDragged))
            ? current : gStudioLastWebMouseDown;
        NSWindow* target=currentWeb_.window ?: window_;
        if (press && ([NSEvent pressedMouseButtons] & 1u)) [target performWindowDragWithEvent:press];
        return result;
    }
    if (method=="studio.windowZoom") { [(currentWeb_.window ?: window_) zoom:nil]; return result; }
    if (method=="studio.windowOpen") return [self openFloatingPanel:params];
    if (method=="studio.windowClose") {
        NSWindow* f=floatWindows_[ns(params.getStr("window"))];
        result.set("closed",Json::fromBool(f!=nil));
        if (f) dispatch_async(dispatch_get_main_queue(),^{ [f close]; });
        return result;
    }
    if (method=="studio.windowTitle") {
        // A floating window names itself after its active tab.
        const std::string title=params.getStr("title");
        NSWindow* w=currentWeb_.window;
        if (title.size()>64 || !w || w==window_) throw std::runtime_error("Invalid window title");
        w.title=ns(title).length ? ns(title) : @"AIVERSE";
        result.set("ok",Json::fromBool(true));
        return result;
    }
    throw std::runtime_error("Unknown window request");
}
// Open a floating Studio window (its own small dock) on any monitor. Frames are
// AppKit screen coordinates; a missing or off-screen frame opens it under the mouse.
- (Json)openFloatingPanel:(const Json&)params {
    Json result=Json::object();
    const std::string key=params.getStr("window"),title=params.getStr("title");
    static const std::regex validKey("^[a-z0-9-]{1,40}$"),validPanel("^[a-z][a-z0-9_]{0,31}$");
    if (!std::regex_match(key,validKey) || title.size()>64) throw std::runtime_error("Invalid floating window");
    std::string panels;
    const auto& list=params["panels"].items();
    for (size_t i=0;i<list.size() && i<16;++i) {
        const std::string panel=list[i].str();
        if (!std::regex_match(panel,validPanel)) throw std::runtime_error("Invalid floating panel");
        panels+=(panels.empty()?"":",")+panel;
    }
    NSString* nsKey=ns(key);
    if (NSWindow* existing=floatWindows_[nsKey]) {
        [existing makeKeyAndOrderFront:nil];
        result.set("opened",Json::fromBool(true));return result;
    }
    if (panels.empty()) throw std::runtime_error("A floating window needs a panel");
    const NSPoint mouse=[NSEvent mouseLocation];
    if (params.getBool("ifOutside")) {
        // Dropped over a Studio window: that window's dock handles it, nothing floats.
        BOOL inside=NSPointInRect(mouse,window_.frame);
        for (NSWindow* f in floatWindows_.allValues) inside=inside || NSPointInRect(mouse,f.frame);
        if (inside) { result.set("opened",Json::fromBool(false));return result; }
    }
    const bool big=panels.find("scene")!=std::string::npos || panels.find("game")!=std::string::npos;
    NSRect frame=NSMakeRect(mouse.x-60,mouse.y-(big?560:520)+18,big?900:420,big?560:520);
    if (params["x"].isNumber() && params["y"].isNumber() && params["width"].isNumber() && params["height"].isNumber()) {
        const NSRect asked=NSMakeRect(params["x"].toDouble(),params["y"].toDouble(),
            std::clamp(params["width"].toDouble(),220.0,6000.0),std::clamp(params["height"].toDouble(),160.0,4000.0));
        for (NSScreen* screen in NSScreen.screens)
            if (NSIntersectsRect(NSInsetRect(screen.visibleFrame,40,40),asked)) { frame=asked; break; }
    }
    NSWindow* window=[[NSWindow alloc] initWithContentRect:NSZeroRect
        styleMask:(NSWindowStyleMaskTitled|NSWindowStyleMaskClosable|NSWindowStyleMaskMiniaturizable|NSWindowStyleMaskResizable)
        backing:NSBackingStoreBuffered defer:NO];
    window.releasedWhenClosed=NO;
    [window setFrame:frame display:NO];
    window.title=ns(title.empty()?std::string("AIVERSE"):title);
    window.delegate=self;
    window.acceptsMouseMovedEvents=YES;
    window.tabbingMode=NSWindowTabbingModeDisallowed;
    window.appearance=window_.appearance ?: [NSAppearance appearanceNamed:NSAppearanceNameDarkAqua];
    WKWebView* web=[self makeWebViewWithFrame:window.contentView.bounds];
    [window.contentView addSubview:web];
    floatWindows_[nsKey]=window;floatWebs_[nsKey]=web;
    NSString* query=[NSString stringWithFormat:@"?window=%@&panels=%s",nsKey,panels.c_str()];
    NSURL* url=devURL_ ? [NSURL URLWithString:query relativeToURL:devURL_]
                       : [NSURL URLWithString:[@"aiverse-studio://shell/index.html" stringByAppendingString:query]];
    [web loadRequest:[NSURLRequest requestWithURL:url]];
    [window makeKeyAndOrderFront:nil];
    result.set("opened",Json::fromBool(true));
    return result;
}
- (void)windowDidMove:(NSNotification*)note {
    NSWindow* moved=note.object;
    [self sendFloatFrame:moved];
    // Dragging a floating window by its title bar over the main window docks its tabs back on release.
    if (moved==window_ || !studioFloatPanel(moved,floatWindows_) || dockWatch_) return;
    if (!([NSEvent pressedMouseButtons] & 1u)) return;
    __weak NSWindow* weakMoved=moved;
    dockWatch_=[NSTimer scheduledTimerWithTimeInterval:0.05 repeats:YES block:^(NSTimer* timer) {
        NSWindow* window=weakMoved;
        const BOOL over=window && NSPointInRect([NSEvent mouseLocation],window_.frame);
        if (over!=dockHover_) {
            dockHover_=over;
            Json payload=Json::object();
            payload.set("over",Json::fromBool(over));
            core_->sendEventPayload("window.dockHover",payload);
        }
        if ([NSEvent pressedMouseButtons] & 1u) return;
        [timer invalidate];dockWatch_=nil;
        if (dockHover_) {
            dockHover_=NO;
            Json payload=Json::object();payload.set("over",Json::fromBool(false));
            core_->sendEventPayload("window.dockHover",payload);
            if (window) [window close]; // window.floatClosed docks every tab back
        }
    }];
}
- (void)windowDidEndLiveResize:(NSNotification*)note { [self sendFloatFrame:note.object]; }
- (void)windowWillClose:(NSNotification*)note {
    NSWindow* window=note.object;
    if (window==window_) {
        // Floats belong to the main window; their list stays saved for the next launch.
        closingFloats_=YES;
        for (NSWindow* f in floatWindows_.allValues.copy) [f close];
        return;
    }
    NSString* panel=studioFloatPanel(window,floatWindows_);
    if (!panel) return;
    WKWebView* web=floatWebs_[panel];
    // A Scene/Game view hosted by this window goes back to the main window, hidden
    // until the main WebView reports its rect again.
    if (viewport_.window==window) {
        [viewport_ removeFromSuperview];viewport_.hidden=YES;
        [window_.contentView addSubview:viewport_ positioned:(htmlComposite_ ? NSWindowBelow : NSWindowAbove) relativeTo:web_];
        core_->surfaceDetached(studio::StudioSurface::Scene);
    }
    for(int i=0;i<int(studio::StudioSurface::Count)-int(studio::StudioSurface::Material);++i)if(assetViews_[i].window==window){
        NSView* view=assetViews_[i];[view removeFromSuperview];view.hidden=YES;
        [window_.contentView addSubview:view positioned:NSWindowAbove relativeTo:web_];
        core_->surfaceDetached(static_cast<studio::StudioSurface>(int(studio::StudioSurface::Material)+i));
    }
    if (fxViewport_.window==window) {
        [fxViewport_ removeFromSuperview];fxViewport_.hidden=YES;
        [window_.contentView addSubview:fxViewport_ positioned:
            (htmlComposite_?NSWindowBelow:NSWindowAbove) relativeTo:web_];
        core_->surfaceDetached(studio::StudioSurface::Fx);
    }
    if (gameViewport_.window==window) {
        [gameViewport_ removeFromSuperview];gameViewport_.hidden=YES;
        core_->surfaceDetached(studio::StudioSurface::Game);
        [window_.contentView addSubview:gameViewport_ positioned:NSWindowAbove relativeTo:web_];
    }
    [web.configuration.userContentController removeScriptMessageHandlerForName:@"aiverse"];
    [floatWindows_ removeObjectForKey:panel];[floatWebs_ removeObjectForKey:panel];
    if (dictationWeb_ == web) { core_->cancelWizardDictation(); dictationWeb_ = nil; }
    if (currentWeb_==web) currentWeb_=nil;
    if (!closingFloats_) core_->sendEvent("window.floatClosed",panel.UTF8String);
}
- (void)windowDidResignKey:(NSNotification*)note { (void)note; core_->gameSuspend(); }
- (void)tick {
    core_->tick();
    if (core_->projectLoading() || core_->session().workspace().project.dir.empty()) return;
    [self runWindShots];
    [self runSkyShots];
}
- (void)runSkyShots {
    auto& run=SkyShotRunner::get();
    if(run.dir.empty()||run.done)return;
    auto& session=core_->session();
    @autoreleasepool {
    try {
        if(!run.window) {
            run.window=[[NSWindow alloc] initWithContentRect:NSMakeRect(80,80,1600,900)
                styleMask:NSWindowStyleMaskTitled|NSWindowStyleMaskClosable backing:NSBackingStoreBuffered defer:NO];
            run.window.title=@"AIVERSE sky shots";run.window.releasedWhenClosed=NO;
            run.view=[[StudioMetalView alloc] initWithFrame:NSMakeRect(0,0,1600,900)];
            run.window.contentView=run.view;[run.window orderFront:nil];
            run.view.layer.contentsScale=1.0;
            run.scene=std::make_unique<studio::StudioScene>(aiverse::gfx::createMetalDevice((__bridge void*)run.view.layer));
            std::filesystem::create_directories(run.dir);
        }
        const auto& stages=SkyShotRunner::stages();
        if(run.stage>=int(stages.size())){run.done=true;[run.window orderOut:nil];NSLog(@"[sky-shots] finished: %s",run.dir.c_str());return;}
        const auto& st=stages[size_t(run.stage)];
        const auto& map=session.workspace().map;
        {   // Every frame: the scene frames the freshly loaded map on its first frames and would move the camera.
            studio::StudioScene::SkyShot shot;shot.elevationDeg=st.elevation;shot.azimuthDeg=st.azimuth;shot.coverage=st.coverage;
            shot.sunColor={st.r,st.g,st.b};run.scene->setSkyShot(shot);
            // High above the map centre, looking up past the horizon towards the sun: sky only, no map in frame.
            run.scene->setShotCamera(map.data.w*.5f,320.f,map.data.h*.5f,6.f,st.azimuth*0.0174533f+3.14159f,st.pitch);
        }
        ((CAMetalLayer*)run.view.layer).drawableSize=CGSizeMake(1600,900);
        run.scene->frame(1600,900,map,session.workspace().map.data.worldSpace,"","",Json{},0,
            float(session.workspace().project.rules.levelHeight.toDouble()),session.workspace().project.dir,session.workspace().project.rulesJson);
    } catch(const std::exception& e){NSLog(@"[sky-shots] failed: %s",e.what());run.done=true;return;}
    }
    ++run.frames;
    const auto& st=SkyShotRunner::stages()[size_t(run.stage)];
    if(!run.waiting&&run.frames>=90){
        run.waiting=true;*run.captured=false;
        const std::string path=run.dir+"/"+st.name+".ppm";auto flag=run.captured;
        run.scene->captureNextFrame([path,flag](int w,int h,std::vector<uint8_t> rgba){ writePpm(path,w,h,rgba); *flag=true; });
        return;
    }
    if(run.waiting&&*run.captured){NSLog(@"[sky-shots] %s captured",st.name);++run.stage;run.frames=0;run.waiting=false;}
}
- (void)runWindShots {
    auto& run=WindShotRunner::get();
    if(run.dir.empty()||run.done)return;
    auto& session=core_->session();
    @autoreleasepool {
    try {
        if(!run.window) {
            const NSRect frame=NSMakeRect(60,60,1280,800);
            run.window=[[NSWindow alloc] initWithContentRect:frame
                styleMask:NSWindowStyleMaskTitled|NSWindowStyleMaskClosable backing:NSBackingStoreBuffered defer:NO];
            run.window.title=@"AIVERSE wind shots";
            run.window.releasedWhenClosed=NO;
            run.view=[[StudioMetalView alloc] initWithFrame:NSMakeRect(0,0,1280,800)];
            run.window.contentView=run.view;
            [run.window orderFront:nil];
            run.view.layer.contentsScale=run.window.backingScaleFactor;
            run.scene=std::make_unique<studio::StudioScene>(aiverse::gfx::createMetalDevice((__bridge void*)run.view.layer));
        }
        const CGFloat scale=run.window.backingScaleFactor;
        const int w=int(run.view.bounds.size.width*scale), h=int(run.view.bounds.size.height*scale);
        ((CAMetalLayer*)run.view.layer).drawableSize=CGSizeMake(w,h);
        run.scene->setWindPreview(session.windPreview());
        run.scene->frame(w,h,session.workspace().map,session.workspace().map.data.worldSpace,"",
            "",Json{},0,float(session.workspace().project.rules.levelHeight.toDouble()),
            session.workspace().project.dir,session.workspace().project.rulesJson);
    } catch(const std::exception& e) {
        NSLog(@"[wind-shots] scene failed: %s",e.what());
        run.done=true;
        if(run.exitAfter)[NSApp terminate:nil];
        return;
    }
    }
    ++run.frames;
    if(run.stage<0) {
        if(run.frames<150)return;                 // let models load and upload
        if(run.hasCamera)run.scene->setShotCamera(run.camera[0],run.camera[1],run.camera[2],run.camera[3],run.camera[4],run.camera[5]);
        std::filesystem::create_directories(run.dir);
        run.stage=0; run.frames=0; run.waiting=false;
    }
    const auto& stages=WindShotRunner::stages();
    if(run.stage>=int(stages.size())) {
        run.done=true;
        NSLog(@"[wind-shots] finished: %s stats %s",run.dir.c_str(),run.scene->windStats().dump(-1).c_str());
        [run.window orderOut:nil];
        if(run.exitAfter)[NSApp terminate:nil];
        return;
    }
    const auto& st=stages[size_t(run.stage)];
    if(run.frames==1) {
        Json request=Json::object();
        request.set("id",Json::number(int64_t(0)));
        request.set("method",Json::string("studio.windSet"));
        request.set("params",Json::parse(st.patch));
        const auto reply=session.dispatch(request);
        if(reply.has("error"))NSLog(@"[wind-shots] %s: %s",st.name,reply.getStr("error").c_str());
    }
    if(!run.waiting && run.frames>=st.settle) {
        run.waiting=true; *run.captured=false;
        const std::string path=run.dir+"/"+st.name+".ppm";
        auto flag=run.captured;
        run.scene->captureNextFrame([path,flag](int w,int h,std::vector<uint8_t> rgba) { writePpm(path,w,h,rgba); *flag=true; });
        return;
    }
    if(run.waiting && *run.captured) {
        NSLog(@"[wind-shots] %s captured",st.name);
        ++run.stage; run.frames=0; run.waiting=false;
    }
}
- (BOOL)windowShouldClose:(NSWindow*)sender {
    if (sender!=window_) return YES; // a floating panel just docks back
    if (window_.attachedSheet || !core_->confirmClose()) return NO;
    closeApproved_=YES;
    return YES;
}
- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication*)sender {
    (void)sender;
    if (closeApproved_) return NSTerminateNow;
    if (window_.attachedSheet) return NSTerminateCancel;
    return core_->confirmClose() ? NSTerminateNow : NSTerminateCancel;
}
- (void)importDroppedFileURLs:(NSArray<NSURL*>*)urls atWindowPoint:(NSPoint)point fromWeb:(WKWebView*)web {
    if(urls.count==0 || urls.count>64 || core_->session().workspace().project.dir.empty() || !web)return;
    NSPoint local=[web convertPoint:point fromView:nil];
    const double x=double(local.x),y=web.isFlipped?double(local.y):
        double(web.bounds.size.height-local.y);
    if(!std::isfinite(x)||!std::isfinite(y))return;
    // The native handler reads a destination from the currently displayed
    // HTML Assets pane only. No browser-provided source filesystem paths.
    NSString* js=[NSString stringWithFormat:
        @"(function(){const el=document.elementFromPoint(%f,%f);"
        @"const root=el&&el.closest('[data-content-drop-root]');"
        @"if(!root)return null;"
        @"const dest=el.closest('[data-content-drop-target]');"
        @"return dest?dest.getAttribute('data-content-drop-target'):"
        @"root.getAttribute('data-content-drop-target');})()",
        x,y];
    NSArray<NSURL*>* safeURLs=[urls copy];
    __unsafe_unretained StudioShell* shell=self;
    [web evaluateJavaScript:js completionHandler:^(id result,NSError* error) {
        if(error || ![result isKindOfClass:[NSString class]])return;
        const std::string folder=[(NSString*)result UTF8String];
        std::vector<std::filesystem::path> sources;
        for(NSURL* url in safeURLs){
            if(!url.isFileURL){
                Json payload=Json::object();payload.set("message",Json::string("Only local files can be imported"));
                shell->core_->sendEventPayload("assets.importError",payload);return;
            }
            sources.emplace_back(url.path.UTF8String);
        }
        shell->core_->importDroppedFiles(sources,folder);
    }];
}
- (void)updateGameChrome:(const Json&)state {
    gameChrome_.frame=gameViewport_.bounds;
    try {
    const auto& device=state["device"];
    const auto& chrome=device["chrome"];
    const double width=device["width"].toDouble(),k=width>0 ? gameViewport_.bounds.size.width/width : 0;
    gameChrome_.active=k>0;
    if (k>0) {
        gameChrome_.corner=chrome["corner"].toDouble()*k;
        const auto& cut=chrome["cutout"];
        gameChrome_.cutout=cut.isObject() ? NSMakeRect(cut["x"].toDouble()*k,cut["y"].toDouble()*k,
            cut["width"].toDouble()*k,cut["height"].toDouble()*k) : NSZeroRect;
        gameChrome_.cutoutRadius=cut.isObject() ? cut["radius"].toDouble()*k : 0;
        const auto& in=chrome["insets"];
        gameChrome_.safe=NSEdgeInsetsMake(in["top"].toDouble()*k,in["left"].toDouble()*k,in["bottom"].toDouble()*k,in["right"].toDouble()*k);
        gameChrome_.showSafe=device.getBool("safeArea");
    }
    } catch (const std::exception&) { gameChrome_.active=NO; }
    gameChrome_.needsDisplay=YES;
}
// Moves a native view into the window of the WebView that hosts its panel, then over the panel's rect.
// Returns NO for a stale report from a window that no longer hosts the view.
- (BOOL)placeSurface:(studio::StudioSurface)surface rect:(const studio::ViewportRect&)rect out:(studio::NativeViewportPlacement&)out {
    WKWebView* host=currentWeb_ ?: web_;
    NSView* view=surface>=studio::StudioSurface::Material?assetViews_[int(surface)-int(studio::StudioSurface::Material)]:surface==studio::StudioSurface::Scene?(NSView*)viewport_:
        surface==studio::StudioSurface::Fx?(NSView*)fxViewport_:(NSView*)gameViewport_;
    if (view.superview!=host.superview) {
        if (!rect.visible) return NO;
        [view removeFromSuperview];
        [host.superview addSubview:view positioned:((htmlComposite_ && (view==viewport_ || view==fxViewport_)) ? NSWindowBelow : NSWindowAbove) relativeTo:host];
    }
    out=studio::viewportPlacement(rect,host.bounds.size.width,host.bounds.size.height,host.window.backingScaleFactor);
    if (view.hidden!=!out.visible) view.hidden=!out.visible;
    if (!out.visible) return YES;
    // Moving the view or resizing its drawable repaints the window: only on real change.
    const NSRect frame=NSMakeRect(out.x,out.y,out.width,out.height);
    if (!NSEqualRects(view.frame,frame)) view.frame=frame;
    CAMetalLayer* layer=(CAMetalLayer*)view.layer;
    const CGFloat scale=(view.window ?: window_).backingScaleFactor;
    if (layer.contentsScale!=scale) layer.contentsScale=scale;
    const CGSize size=CGSizeMake(out.pixelsW,out.pixelsH);
    if (!CGSizeEqualToSize(layer.drawableSize,size)) {
        // The Scene layer is drawn on the render thread: resize between its frames.
        if (view!=gameViewport_)
            core_->runOnSceneThread([&]{ layer.drawableSize=size; });
        else layer.drawableSize=size;
    }
    return YES;
}
- (void)webView:(WKWebView*)view didFailProvisionalNavigation:(WKNavigation*)navigation withError:(NSError*)error {
    (void)view; (void)navigation;
    NSLog(@"[studio-web] navigation failed: %@",error);
}
- (void)webView:(WKWebView*)view didFailNavigation:(WKNavigation*)navigation withError:(NSError*)error {
    (void)view; (void)navigation;
    NSLog(@"[studio-web] navigation error: %@",error);
}
- (void)webView:(WKWebView*)view didStartProvisionalNavigation:(WKNavigation*)navigation {
    (void)navigation;
    if (dictationWeb_ == view) { core_->cancelWizardDictation(); dictationWeb_ = nil; }
}
- (void)webViewWebContentProcessDidTerminate:(WKWebView*)view {
    if (dictationWeb_ == view) { core_->cancelWizardDictation(); dictationWeb_ = nil; }
    NSLog(@"[studio-web] WebKit content process terminated");
}
- (void)webView:(WKWebView*)view didFinishNavigation:(WKNavigation*)navigation {
    (void)navigation;
    NSLog(@"[studio-web] top-level navigation finished: %@",view.URL);
    [view evaluateJavaScript:@"document.documentElement.classList.add('character-composite');" completionHandler:nil];
    if (htmlComposite_) [view evaluateJavaScript:@"document.documentElement.classList.add('native-composite');" completionHandler:nil];
    if (view==web_) { mainWebReady_=YES; core_->webReady(); }
}
- (void)webView:(WKWebView*)view decidePolicyForNavigationAction:(WKNavigationAction*)action decisionHandler:(void (^)(WKNavigationActionPolicy))done {
    (void)view;
    done([self trusted:action.request.URL] ? WKNavigationActionPolicyAllow : WKNavigationActionPolicyCancel);
}
- (void)userContentController:(WKUserContentController*)controller didReceiveScriptMessage:(WKScriptMessage*)message {
    (void)controller;
    if (![message.name isEqualToString:@"aiverse"] || !message.frameInfo.isMainFrame ||
        ![self trusted:message.frameInfo.request.URL] || ![NSJSONSerialization isValidJSONObject:message.body]) return;
    NSError* error=nil;
    NSData* data=[NSJSONSerialization dataWithJSONObject:message.body options:0 error:&error];
    if (error || !data || data.length>1024*1024) return;
    currentWeb_=message.webView;
    core_->handleMessage(std::string((const char*)data.bytes,(size_t)data.length));
    currentWeb_=nil;
}
- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)app { (void)app; return YES; }
- (void)applicationWillTerminate:(NSNotification*)note {
    (void)note;
    [timer_ invalidate];
    timer_=nil;
    core_->shutdown(); // hosted game and Scene release GPU resources before the CAMetalLayers
    [web_.configuration.userContentController removeScriptMessageHandlerForName:@"aiverse"];
}
@end

// PlatformMac.mm (aiverse_runtime) owns main() and forwards to aiverseMain().
int aiverseMain(int argc,char** argv) {
    @autoreleasepool {
        std::string project;
        bool dev=false, composite=false;
        int listenPort=0;
        for(int i=1;i<argc;++i) {
            std::string arg(argv[i]);
            if(arg=="--dev")dev=true;
            else if(arg=="--html-composite")composite=true;
            else if(arg=="--listen" && i+1<argc) {
                listenPort=aiverse::editor::net::parseListenPort(argv[++i]);
                if(!listenPort){std::fprintf(stderr,"Invalid --listen port\n");return 2;}
            }
            else if(arg=="--project"&&i+1<argc)project=argv[++i];
            else { std::fprintf(stderr,"Usage: AiverseStudioHTML5 [--dev] [--html-composite] [--listen PORT] [--project /path/to/project]\n"); return 2; }
        }
        // Without --project, reopen the last project chosen in the Open panel.
        if(project.empty()) {
            NSString* last=[[NSUserDefaults standardUserDefaults] stringForKey:@"AiverseLastProject"];
            BOOL isDir=NO;
            if(last.length && [[NSFileManager defaultManager] fileExistsAtPath:last isDirectory:&isDir] && isDir)
                project=last.UTF8String;
        } else {
            [[NSUserDefaults standardUserDefaults] setObject:[NSString stringWithUTF8String:project.c_str()] forKey:@"AiverseLastProject"];
        }
        try {
            [NSApplication sharedApplication];
            [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
            StudioShell* shell=[[StudioShell alloc] initWithProject:project dev:dev composite:composite listenPort:listenPort];
            [NSApp setDelegate:shell];
            [NSApp run];
        } catch(const std::exception& e) {
            std::fprintf(stderr,"Studio startup failed: %s\n",e.what()); return 1;
        }
    }
    return 0;
}
