// Experimental AIVERSE Studio: real WKWebView + native Metal Renderer3D preview.
// Render surface is a sibling NSView over the DOM viewport bounds (no CPU frame streaming).
#import <AppKit/AppKit.h>
#import <WebKit/WebKit.h>
#include <regex>
#include <algorithm>
#import <QuartzCore/CAMetalLayer.h>
#import "StudioAssetSchemeHandler.h"
#include "apps/studio-native/StudioSession.h"
#include "apps/studio-native/StudioHumanLookDocument.h"
#include "apps/editor/core/BuildTool.h"
#include "apps/studio-native/StudioViewportLayout.h"
#include "apps/studio-native/mac/StudioMetalScene.h"
#include "apps/studio-native/mac/StudioGameSurface.h"
#include "apps/studio-native/StudioGameInput.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <exception>
#include <memory>
#include <map>
#include <set>
#include <string>
#include <vector>
#include <filesystem>
#include <fstream>
#include <iterator>

#ifndef AIVERSE_STUDIO_SOURCE_DIR
#define AIVERSE_STUDIO_SOURCE_DIR "."
#endif

@interface StudioWindowChrome : NSView
@end
@implementation StudioWindowChrome
- (BOOL)acceptsFirstMouse:(NSEvent*)event { (void)event; return YES; }
- (void)mouseDown:(NSEvent*)event {
    if(event.clickCount==2)[self.window performZoom:nil];
    else [self.window performWindowDragWithEvent:event];
}
@end
@class StudioHost;
// A native drop receiver is required because Finder drags local file URLs,
// not JavaScript-provided paths. These are trusted only after NSDraggingDestination
// resolves them and the drop falls inside the HTML5 Assets panel.
@interface StudioFileDropWebView : WKWebView <NSDraggingDestination>
@property(nonatomic, assign) StudioHost* studioHost;
@end
@interface StudioTransparentWebView : StudioFileDropWebView
@end
@implementation StudioTransparentWebView
- (BOOL)isOpaque { return NO; }
@end
@interface StudioMetalView : NSView {
    NSPoint press_;
    BOOL dragged_;
    BOOL moveDrag_;
    BOOL marquee_;             // plain LMB drag on empty space: rectangle selection
    NSTrackingArea* hoverTracking_;
    NSTimeInterval lastHover_;
}
@property(nonatomic, assign) StudioHost* host;
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
    if (self.cutout.size.width>0 && self.cutout.size.height>0) {
        [[NSColor blackColor] setFill];
        [[NSBezierPath bezierPathWithRoundedRect:self.cutout xRadius:self.cutoutRadius yRadius:self.cutoutRadius] fill];
    }
    if (!self.showSafe) return;
    const NSEdgeInsets in=self.safe;
    if (in.left<=0 && in.top<=0 && in.right<=0 && in.bottom<=0) return;
    const NSRect inner=NSMakeRect(in.left,in.top,b.size.width-in.left-in.right,b.size.height-in.top-in.bottom);
    NSBezierPath* shade=[NSBezierPath bezierPathWithRect:b];
    [shade appendBezierPath:[NSBezierPath bezierPathWithRect:inner]];
    shade.windingRule=NSWindingRuleEvenOdd;
    [[NSColor colorWithSRGBRed:1 green:70/255.0 blue:60/255.0 alpha:70/255.0] setFill];[shade fill];
    NSBezierPath* edge=[NSBezierPath bezierPathWithRect:NSInsetRect(inner,.75,.75)];
    edge.lineWidth=1.5;[[NSColor colorWithSRGBRed:1 green:210/255.0 blue:80/255.0 alpha:.9] setStroke];[edge stroke];
}
@end

@interface StudioHost : NSObject <NSApplicationDelegate, NSWindowDelegate, WKScriptMessageHandler, WKNavigationDelegate> {
    NSWindow* window_;
    WKWebView* web_;
    StudioMetalView* viewport_;
    StudioGameView* gameViewport_;
    StudioDeviceChromeView* gameChrome_;
    std::unique_ptr<aiverse::studio::StudioGameSurface> game_;
    aiverse::studio::NativeViewportPlacement gamePlacement_;
    double gameFrameTime_;
    NSTimer* timer_;
    NSString* dist_;
    NSURL* devURL_;
    StudioAssetSchemeHandler* assets_;
    std::unique_ptr<aiverse::studio::StudioSession> session_;
    std::unique_ptr<aiverse::studio::StudioMetalScene> preview_;
    aiverse::studio::NativeViewportPlacement placement_;
    BOOL readyEmitted_; // Objective-C object ivars are zero-initialized
    BOOL htmlComposite_;
    BOOL closeApproved_;
    unsigned lightingStatusTicks_;
    // Last Scene camera state pushed to the HTML orientation gizmo.
    double lastCameraYaw_, lastCameraPitch_, lastCameraDistance_;
    BOOL lastCameraOrtho_, cameraSent_;
    std::string lastActionCaption_; // last "viewport.action" caption sent to the web UI
    std::string hoverId_;           // Scene / Hierarchy hover highlight
    BOOL projectLoading_;           // a project opens on a background thread: no session access
    std::string pendingProject_;    // --project, opened once the WebView can show progress
    int lastLoadDone_, lastLoadTotal_;
    CFTimeInterval nextAgentPoll_;
    BOOL loadingShown_;
    // Floating panel windows (Unity-style undocked tabs), one panel per window.
    NSMutableDictionary<NSString*,NSWindow*>* floatWindows_;
    NSMutableDictionary<NSString*,WKWebView*>* floatWebs_;
    __unsafe_unretained WKWebView* currentWeb_; // sender of the bridge message being handled
    BOOL closingFloats_;            // main window closes: keep the float list for next launch
}
- (instancetype)initWithProject:(const std::string&)project dev:(BOOL)dev composite:(BOOL)composite;
- (void)orbitX:(CGFloat)dx y:(CGFloat)dy;
- (void)zoom:(CGFloat)amount;
- (void)panX:(CGFloat)dx y:(CGFloat)dy;
- (void)lookX:(CGFloat)dx y:(CGFloat)dy;
- (void)moveRight:(CGFloat)right up:(CGFloat)up forward:(CGFloat)forward;
- (void)focusSceneSelection;
- (void)resetSceneView;
- (void)publishSceneCamera;
- (std::vector<std::string>)materialSlotsFor:(const std::string&)asset;
- (BOOL)sceneGizmoAtX:(CGFloat)x y:(CGFloat)y width:(CGFloat)width height:(CGFloat)height;
- (BOOL)sceneMoveBeginAtX:(CGFloat)x y:(CGFloat)y width:(CGFloat)width height:(CGFloat)height;
- (void)sceneMoveUpdateAtX:(CGFloat)x y:(CGFloat)y width:(CGFloat)width height:(CGFloat)height invertSnap:(BOOL)invertSnap;
- (void)sceneMoveEnd;
- (void)setSceneTool:(int)tool;
- (int)sceneTool;
- (void)selectAtX:(CGFloat)x y:(CGFloat)y width:(CGFloat)width height:(CGFloat)height;
- (void)selectAtX:(CGFloat)x y:(CGFloat)y width:(CGFloat)width height:(CGFloat)height mode:(const char*)mode;
- (void)sceneMarqueeFrom:(NSPoint)a to:(NSPoint)b width:(CGFloat)width height:(CGFloat)height;
- (void)sceneMarqueeEndFrom:(NSPoint)a to:(NSPoint)b width:(CGFloat)width height:(CGFloat)height mode:(const char*)mode;
- (void)sceneHoverAtX:(CGFloat)x y:(CGFloat)y width:(CGFloat)width height:(CGFloat)height;
- (void)sceneSelectionAction:(const char*)method;
- (void)openProjectPath:(const std::string&)path requestId:(int64_t)requestId;
- (void)sendLoading:(const char*)phase message:(const std::string&)message done:(int)done total:(int)total;
- (void)placeMetalView:(NSView*)view x:(double)x y:(double)y width:(double)width height:(double)height pixelsW:(int)pixelsW pixelsH:(int)pixelsH;
- (BOOL)placeAsset:(const std::string&)asset atX:(CGFloat)x y:(CGFloat)y width:(CGFloat)width height:(CGFloat)height;
- (void)sendStudioEvent:(const char*)event message:(const char*)message;
- (void)sendStudioEventPayload:(const char*)event payload:(const aiverse::Json&)payload;
- (void)importDroppedFileURLs:(NSArray<NSURL*>*)urls atWindowPoint:(NSPoint)point;
- (void)importDroppedFileURLs:(NSArray<NSURL*>*)urls atWindowPoint:(NSPoint)point fromWeb:(WKWebView*)web;
- (WKWebView*)makeWebViewWithFrame:(NSRect)frame;
- (void)evaluateInAllWebs:(NSString*)js;
- (aiverse::Json)openFloatingPanel:(const aiverse::Json&)params;
- (void)handleBridgeMessage:(WKScriptMessage*)message;
- (void)sendStudioReply:(const aiverse::Json&)reply;
- (void)openProjectSheetForRequestId:(int64_t)requestId;
- (void)openProjectSheetForRequestId:(int64_t)requestId confirmedDiscard:(BOOL)confirmedDiscard;
- (void)sendProjectCancelledReply:(int64_t)requestId;
- (BOOL)confirmDiscardChanges;
- (void)gameEvent:(const aiverse::platform::Event&)event;
- (void)tickGame;
- (void)pollAgentRequests;
- (aiverse::Json)gameState;
- (void)saveMapSheetForRequestId:(int64_t)requestId;
- (void)openAssetImportForRequestId:(int64_t)requestId destination:(NSString*)destination;
@end

// Last left press inside the WebView: HTML asks the host to start a title-bar
// window drag asynchronously, after the press was already delivered.
static NSEvent* gStudioLastWebMouseDown=nil;
@implementation StudioFileDropWebView
- (void)mouseDown:(NSEvent*)event { gStudioLastWebMouseDown=event; [super mouseDown:event]; }
- (NSDragOperation)draggingEntered:(id<NSDraggingInfo>)sender {
    NSArray* urls=[[sender draggingPasteboard]
        readObjectsForClasses:@[[NSURL class]]
        options:@{NSPasteboardURLReadingFileURLsOnlyKey:@YES}];
    if(urls.count && self.studioHost)return NSDragOperationCopy;
    return [super draggingEntered:sender];
}
- (NSDragOperation)draggingUpdated:(id<NSDraggingInfo>)sender {
    NSArray* urls=[[sender draggingPasteboard]
        readObjectsForClasses:@[[NSURL class]]
        options:@{NSPasteboardURLReadingFileURLsOnlyKey:@YES}];
    if(urls.count && self.studioHost)return NSDragOperationCopy;
    return [super draggingUpdated:sender];
}
- (BOOL)performDragOperation:(id<NSDraggingInfo>)sender {
    NSArray<NSURL*>* urls=[[sender draggingPasteboard]
        readObjectsForClasses:@[[NSURL class]]
        options:@{NSPasteboardURLReadingFileURLsOnlyKey:@YES}];
    if(urls.count && self.studioHost){
        [self.studioHost importDroppedFileURLs:urls atWindowPoint:sender.draggingLocation fromWeb:self];
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
    [self.host sceneHoverAtX:point.x y:point.y width:self.bounds.size.width height:self.bounds.size.height];
}
- (void)mouseExited:(NSEvent*)event { (void)event; [self.host sceneHoverAtX:-1 y:-1 width:self.bounds.size.width height:self.bounds.size.height]; }
static NSString* const kStudioAssetDragPrefix=@"aiverse-asset:";
static NSString* studioDraggedAsset(id<NSDraggingInfo> sender) {
    NSString* text=[[sender draggingPasteboard] stringForType:NSPasteboardTypeString];
    return [text hasPrefix:kStudioAssetDragPrefix] ? [text substringFromIndex:kStudioAssetDragPrefix.length] : nil;
}
- (NSDragOperation)draggingEntered:(id<NSDraggingInfo>)sender { return studioDraggedAsset(sender) ? NSDragOperationCopy : NSDragOperationNone; }
- (NSDragOperation)draggingUpdated:(id<NSDraggingInfo>)sender { return studioDraggedAsset(sender) ? NSDragOperationCopy : NSDragOperationNone; }
- (BOOL)performDragOperation:(id<NSDraggingInfo>)sender {
    NSString* asset=studioDraggedAsset(sender);
    if (!asset.length || asset.length>1024) return NO;
    const NSPoint point=[self convertPoint:sender.draggingLocation fromView:nil];
    return [self.host placeAsset:asset.UTF8String atX:point.x y:point.y width:self.bounds.size.width height:self.bounds.size.height];
}
- (void)mouseDown:(NSEvent*)event {
    press_=[self convertPoint:event.locationInWindow fromView:nil];
    dragged_=NO;marquee_=NO;
    [self.window makeFirstResponder:self];
    // Plain LMB on a handle starts a Move / Rotate / Scale drag (Unity W/E/R tools).
    moveDrag_=!(event.modifierFlags & NSEventModifierFlagOption) &&
        [self.host sceneMoveBeginAtX:press_.x y:press_.y width:self.bounds.size.width height:self.bounds.size.height];
}
// Unity Scene view mapping, shared with the HTML composite path
// (apps/studio-web/src/panels/sceneCamera.ts): LMB select, Alt+LMB orbit,
// Alt+Cmd+LMB / MMB pan, RMB flythrough (WASD/QE), Alt+RMB zoom.
- (void)mouseDragged:(NSEvent*)event {
    if (moveDrag_) {
        const NSPoint point=[self convertPoint:event.locationInWindow fromView:nil];
        dragged_=YES;
        // Ctrl/Cmd held while dragging inverts the snap setting (Unity: Ctrl snaps).
        const BOOL invert=(event.modifierFlags & (NSEventModifierFlagControl|NSEventModifierFlagCommand))!=0;
        [self.host sceneMoveUpdateAtX:point.x y:point.y width:self.bounds.size.width height:self.bounds.size.height invertSnap:invert];
        return;
    }
    const NSEventModifierFlags flags=event.modifierFlags;
    // Hand tool (Q): plain LMB drag pans the view.
    if (!(flags & NSEventModifierFlagOption) && [self.host sceneTool]==0) { dragged_=YES; [self.host panX:event.deltaX y:event.deltaY]; return; }
    if (!(flags & NSEventModifierFlagOption)) {
        // Plain LMB drag on empty space: rectangle (marquee) selection.
        const NSPoint point=[self convertPoint:event.locationInWindow fromView:nil];
        if (!marquee_ && std::hypot(double(point.x-press_.x),double(point.y-press_.y))<5.0) return;
        marquee_=YES;dragged_=YES;
        [self.host sceneMarqueeFrom:press_ to:point width:self.bounds.size.width height:self.bounds.size.height];
        return;
    }
    dragged_=YES;
    if (flags & NSEventModifierFlagCommand) [self.host panX:event.deltaX y:event.deltaY];
    else [self.host orbitX:event.deltaX y:event.deltaY];
}
- (void)mouseUp:(NSEvent*)event {
    if (moveDrag_) { moveDrag_=NO; [self.host sceneMoveEnd]; return; }
    const NSPoint point=[self convertPoint:event.locationInWindow fromView:nil];
    // Shift adds to the selection, Cmd toggles (Unity / Finder convention).
    const char* mode=(event.modifierFlags & NSEventModifierFlagCommand)?"toggle":
        (event.modifierFlags & NSEventModifierFlagShift)?"add":"replace";
    if (marquee_) {
        marquee_=NO;
        [self.host sceneMarqueeEndFrom:press_ to:point width:self.bounds.size.width height:self.bounds.size.height mode:mode];
        return;
    }
    if (dragged_ || std::hypot(double(point.x-press_.x), double(point.y-press_.y)) >= 5.0) return;
    if ([self.host sceneGizmoAtX:point.x y:point.y width:self.bounds.size.width height:self.bounds.size.height]) return;
    [self.host selectAtX:point.x y:point.y width:self.bounds.size.width height:self.bounds.size.height mode:mode];
}
- (void)rightMouseDown:(NSEvent*)event { [self.window makeFirstResponder:self]; (void)event; }
- (void)rightMouseDragged:(NSEvent*)event {
    if (event.modifierFlags & NSEventModifierFlagOption)
        [self.host zoom:(event.deltaX+event.deltaY)*0.6];
    else [self.host lookX:event.deltaX y:event.deltaY];
}
- (void)otherMouseDragged:(NSEvent*)event { [self.host panX:event.deltaX y:event.deltaY]; }
- (void)keyDown:(NSEvent*)event {
    // Physical key codes (ANSI positions): shortcuts work with any keyboard layout.
    enum : unsigned short { kA=0,kS=1,kD=2,kF=3,kQ=12,kW=13,kE=14,kR=15,kLeft=123,kRight=124,kDown=125,kUp=126,kHome=115 };
    const unsigned short code=event.keyCode;
    const CGFloat scale=(event.modifierFlags & NSEventModifierFlagShift)?3.0:1.0;
    // Delete / ⌘D act on the whole Scene selection (one undo step).
    const NSEventModifierFlags mods=event.modifierFlags&(NSEventModifierFlagCommand|NSEventModifierFlagControl|NSEventModifierFlagOption|NSEventModifierFlagShift);
    if ((code==51 || code==117) && !(mods&~NSEventModifierFlagShift)) { [self.host sceneSelectionAction:"studio.entitiesDelete"]; return; }
    if (code==kD && mods==NSEventModifierFlagCommand) { [self.host sceneSelectionAction:"studio.entitiesDuplicate"]; return; }
    if (event.modifierFlags & (NSEventModifierFlagCommand|NSEventModifierFlagControl|NSEventModifierFlagOption)) {
        [super keyDown:event]; return;
    }
    // WASD/QE fly only while the right button is held; arrows always move.
    const BOOL flying=([NSEvent pressedMouseButtons] & (1u<<1))!=0;
    switch(code) {
        case kUp: [self.host moveRight:0 up:0 forward:scale]; return;
        case kDown: [self.host moveRight:0 up:0 forward:-scale]; return;
        case kLeft: [self.host moveRight:-scale up:0 forward:0]; return;
        case kRight: [self.host moveRight:scale up:0 forward:0]; return;
        case kHome: [self.host resetSceneView]; return;
        case kF: if (!flying) { [self.host focusSceneSelection]; return; } break;
        default: break;
    }
    if (!flying) {
        // Q/W/E/R pick the Hand / Move / Rotate / Scale tool (physical keys).
        switch(code) {
            case kQ: [self.host setSceneTool:0]; return;
            case kW: [self.host setSceneTool:1]; return;
            case kE: [self.host setSceneTool:2]; return;
            case kR: [self.host setSceneTool:3]; return;
            default: break;
        }
        [super keyDown:event]; return;
    }
    switch(code) {
        case kW: [self.host moveRight:0 up:0 forward:scale]; break;
        case kS: [self.host moveRight:0 up:0 forward:-scale]; break;
        case kA: [self.host moveRight:-scale up:0 forward:0]; break;
        case kD: [self.host moveRight:scale up:0 forward:0]; break;
        case kQ: [self.host moveRight:0 up:-scale forward:0]; break;
        case kE: [self.host moveRight:0 up:scale forward:0]; break;
        default: [super keyDown:event]; break;
    }
}

- (void)scrollWheel:(NSEvent*)event {
    const CGFloat wheel = event.scrollingDeltaY * (event.hasPreciseScrollingDeltas ? 0.12 : 1.0);
    [self.host zoom:wheel];
}
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
    [self.host gameEvent:e];
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
    e.key=aiverse::studio::studioGameKey(text.UTF8String?text.UTF8String:"");
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
    [self.host gameEvent:e];
    if(down && !(e.mods&(kModCtrl|kModSuper))) {
        NSString* chars=event.characters;
        for(NSUInteger i=0;i<chars.length;++i) {
            uint32_t c=[chars characterAtIndex:i];
            if(c>=0xd800 && c<=0xdbff && i+1<chars.length) {
                uint32_t low=[chars characterAtIndex:i+1];
                if(low>=0xdc00 && low<=0xdfff){c=0x10000+((c-0xd800)<<10)+(low-0xdc00);++i;}
            }
            if(c>=32 && !(c>=0xd800 && c<=0xdfff) && !(c>=0xf700 && c<=0xf8ff)) {
                Event input;input.type=EventType::Char;input.codepoint=c;[self.host gameEvent:input];
            }
        }
    }
}
- (void)keyDown:(NSEvent*)e { [self sendKey:e down:YES]; }
- (void)keyUp:(NSEvent*)e { [self sendKey:e down:NO]; }
- (BOOL)resignFirstResponder {
    aiverse::platform::Event e;e.type=aiverse::platform::EventType::FocusLost;[self.host gameEvent:e];
    return [super resignFirstResponder];
}
@end

// Developer automation for scene-only wind-mask screenshots (no desktop or
// WebView capture): AIVERSE_STUDIO_WIND_SHOTS=<dir> walks preview states,
// grabs the Metal Scene drawable as PPM files, then quits when
// AIVERSE_STUDIO_SHOTS_EXIT=1. Camera: AIVERSE_STUDIO_SHOT_CAMERA="x y z dist yaw pitch".
namespace {
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
    std::unique_ptr<aiverse::studio::StudioMetalScene> scene;
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
}

@implementation StudioHost
- (instancetype)initWithProject:(const std::string&)project dev:(BOOL)dev composite:(BOOL)composite {
    if ((self=[super init])) {
        htmlComposite_=composite;
        std::string root=AIVERSE_STUDIO_SOURCE_DIR;
        // The project opens asynchronously after the UI loads (progress dialog).
        session_=std::make_unique<aiverse::studio::StudioSession>(root,std::string());
#ifdef AIVERSE_STUDIO_BUILD_TOOL
        session_->setBuildTool(AIVERSE_STUDIO_BUILD_TOOL);
#else
        session_->setBuildTool(aiverse::editor::findBuildTool(
            [[[NSBundle mainBundle] executablePath] stringByDeletingLastPathComponent].UTF8String,root));
#endif
        pendingProject_=project;
        __unsafe_unretained StudioHost* host=self;
        session_->setMaterialSlotProvider([host](const std::string& asset) {
            return [host materialSlotsFor:asset];
        });
        NSURL* bundled=[[NSBundle mainBundle] URLForResource:@"index" withExtension:@"html" subdirectory:@"studio-web"];
        dist_=bundled ? [[[bundled URLByDeletingLastPathComponent] path] stringByStandardizingPath]
                      : [[NSString stringWithUTF8String:(root+"/apps/studio-web/dist").c_str()] stringByStandardizingPath];
        if (dev) devURL_=[NSURL URLWithString:@"http://127.0.0.1:4173/"];
    }
    return self;
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
    viewport_.host=self;
    viewport_.hidden=YES;
    [window_.contentView addSubview:viewport_
        positioned:(htmlComposite_ ? NSWindowBelow : NSWindowAbove) relativeTo:web_];
    gameViewport_=[[StudioGameView alloc] initWithFrame:NSZeroRect];
    gameViewport_.host=self;gameViewport_.hidden=YES;
    gameChrome_=[[StudioDeviceChromeView alloc] initWithFrame:NSZeroRect];
    gameChrome_.autoresizingMask=NSViewWidthSizable|NSViewHeightSizable;
    [gameViewport_ addSubview:gameChrome_];
    [window_.contentView addSubview:gameViewport_ positioned:NSWindowAbove relativeTo:web_];
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
// Every Studio WebView (main window and floating panels) shares one bridge and one session.
- (WKWebView*)makeWebViewWithFrame:(NSRect)frame {
    WKWebViewConfiguration* cfg=[[WKWebViewConfiguration alloc] init];
    [cfg.userContentController addScriptMessageHandler:self name:@"aiverse"];
    if (assets_) [cfg setURLSchemeHandler:assets_ forURLScheme:@"aiverse-studio"];
    WKWebView* web=nil;
    if (htmlComposite_) {
        web=[[StudioTransparentWebView alloc] initWithFrame:frame configuration:cfg];
        if (@available(macOS 12.0, *)) web.underPageBackgroundColor=[NSColor clearColor];
    } else web=[[StudioFileDropWebView alloc] initWithFrame:frame configuration:cfg];
    ((StudioFileDropWebView*)web).studioHost=self;
    [web registerForDraggedTypes:@[NSPasteboardTypeFileURL]];
    web.navigationDelegate=self;
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
    aiverse::Json payload=aiverse::Json::object();
    payload.set("window",aiverse::Json::string(panel.UTF8String));
    payload.set("x",aiverse::Json::number(double(f.origin.x),1));
    payload.set("y",aiverse::Json::number(double(f.origin.y),1));
    payload.set("width",aiverse::Json::number(double(f.size.width),1));
    payload.set("height",aiverse::Json::number(double(f.size.height),1));
    [self sendStudioEventPayload:"window.floatFrame" payload:payload];
}
// Open a floating Studio window (its own small dock) on any monitor. Frames are
// AppKit screen coordinates; a missing or off-screen frame opens it under the mouse.
- (aiverse::Json)openFloatingPanel:(const aiverse::Json&)params {
    aiverse::Json result=aiverse::Json::object();
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
    NSString* nsKey=[NSString stringWithUTF8String:key.c_str()];
    if (NSWindow* existing=floatWindows_[nsKey]) {
        [existing makeKeyAndOrderFront:nil];
        result.set("opened",aiverse::Json::fromBool(true));return result;
    }
    if (panels.empty()) throw std::runtime_error("A floating window needs a panel");
    const NSPoint mouse=[NSEvent mouseLocation];
    if (params.getBool("ifOutside")) {
        // Dropped over a Studio window: that window's dock handles it, nothing floats.
        BOOL inside=NSPointInRect(mouse,window_.frame);
        for (NSWindow* f in floatWindows_.allValues) inside=inside || NSPointInRect(mouse,f.frame);
        if (inside) { result.set("opened",aiverse::Json::fromBool(false));return result; }
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
    window.title=[NSString stringWithUTF8String:(title.empty()?std::string("AIVERSE"):title).c_str()];
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
    result.set("opened",aiverse::Json::fromBool(true));
    return result;
}
- (void)windowDidMove:(NSNotification*)note { [self sendFloatFrame:note.object]; }
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
        [viewport_ removeFromSuperview];viewport_.hidden=YES;placement_={};readyEmitted_=NO;
        [window_.contentView addSubview:viewport_ positioned:(htmlComposite_ ? NSWindowBelow : NSWindowAbove) relativeTo:web_];
    }
    if (gameViewport_.window==window) {
        [gameViewport_ removeFromSuperview];gameViewport_.hidden=YES;gamePlacement_={};
        if (game_) game_->suspend();
        [window_.contentView addSubview:gameViewport_ positioned:NSWindowAbove relativeTo:web_];
    }
    [web.configuration.userContentController removeScriptMessageHandlerForName:@"aiverse"];
    [floatWindows_ removeObjectForKey:panel];[floatWebs_ removeObjectForKey:panel];
    if (currentWeb_==web) currentWeb_=nil;
    if (!closingFloats_) [self sendStudioEvent:"window.floatClosed" message:panel.UTF8String];
}
- (aiverse::Json)gameState {
    if(game_)return game_->state();
    auto out=aiverse::Json::parse(R"({"paused":false,"canControl":false,"canStep":false,"active":false,"loading":false,"progress":0,"inMatch":false,"tick":0,"error":""})");
    out.set("device",aiverse::studio::StudioGameDevice{}.json());return out;
}
- (void)gameEvent:(const aiverse::platform::Event&)event {
    if(game_ && !gameViewport_.hidden)game_->event(event);
}
- (void)windowDidResignKey:(NSNotification*)note { (void)note;if(game_)game_->suspend(); }
- (void)tickGame {
    const double now=CACurrentMediaTime(),dt=gameFrameTime_>0?now-gameFrameTime_:0;
    gameFrameTime_=now;
    if(!game_)return;
    NSWindow* gameWindow=gameViewport_.window ?: window_;
    if(!gamePlacement_.visible || gameViewport_.hidden || gameWindow.isMiniaturized || !gameWindow.visible) {
        game_->suspend();return;
    }
    game_->frame(gamePlacement_.pixelsW,gamePlacement_.pixelsH,
        float(gamePlacement_.width),float(gamePlacement_.height),dt,
        gameWindow.isKeyWindow && gameWindow.firstResponder==gameViewport_);
}
- (void)tick {
    if (projectLoading_) return; // the background thread owns the session right now
    [self tickGame];
    // An empty Workspace owns an internal default MapDoc, NOT a loaded project.
    // Never render that provisional map as if it belonged to the user.
    if (session_->workspace().project.dir.empty()) return;
    [self runWindShots];
    [self pollAgentRequests];
    NSWindow* sceneWindow=viewport_.window ?: window_;
    if (!placement_.visible || viewport_.hidden || sceneWindow.isMiniaturized || !sceneWindow.visible) {
        session_->setFacialPreviewGpuStatus(
            false,"Native Scene viewport hidden or not drawable");
        return;
    }
    @autoreleasepool {
        try {
            if (!preview_) {
                preview_=std::make_unique<aiverse::studio::StudioMetalScene>((__bridge void*)viewport_.layer);
                // Default host: the Metal view covers HTML, so the renderer draws the
                // orientation gizmo itself; composite mode uses the HTML gizmo.
                preview_->setOrientationGizmo(!htmlComposite_);
            }
            // StudioSession/renderer are owned and called on the main thread.
            {
                const auto ids=session_->selectedObjectIds();
                preview_->setEditorState(std::set<std::string>(ids.begin(),ids.end()),session_->hiddenIds(),session_->lockedIds(),hoverId_);
            }
            preview_->setWindPreview(session_->windPreview());
            preview_->frame(placement_.pixelsW, placement_.pixelsH, session_->workspace().map,
                session_->workspace().project.isFps(), session_->selectedObjectId(),
                session_->previewMaterialName(), session_->previewMaterialFactors(),
                session_->previewMaterialRevision(),
                float(session_->workspace().project.rules.levelHeight.toDouble()),
                session_->workspace().project.dir,
                session_->workspace().project.rulesJson,
                &session_->facialPreviewBinding());
            session_->setFacialPreviewGpuStatus(
                preview_->facialReady(),preview_->facialError());
            if((lightingStatusTicks_ % 30)==0)session_->setFacialPreviewClips(preview_->facialClips(),preview_->facialBaseHeight());
            if((lightingStatusTicks_++ % 60)==0)
                [self sendStudioEvent:"lighting.status" message:preview_->lightingStatus().c_str()];
            {
                // Scene model streaming progress (Unreal-style toast / status bar).
                const auto& load=preview_->modelLoadProgress();
                if (load.total>0 && load.done<load.total) {
                    if (load.done!=lastLoadDone_ || load.total!=lastLoadTotal_) {
                        lastLoadDone_=load.done;lastLoadTotal_=load.total;loadingShown_=YES;
                        [self sendLoading:"models" message:load.current done:load.done total:load.total];
                    }
                } else if (loadingShown_) {
                    loadingShown_=NO;lastLoadDone_=lastLoadTotal_=-1;
                    [self sendLoading:"done" message:"" done:load.total total:load.total];
                }
            }
            if (!readyEmitted_) {
                [self sendStudioEvent:"viewport.ready" message:"Metal Renderer3D"];
                readyEmitted_=YES;
                cameraSent_=NO;
            }
            [self publishSceneCamera];
        } catch (const std::exception& e) {
            NSLog(@"AIVERSE native Scene failed: %s",e.what());
            [self sendStudioEvent:"viewport.error" message:e.what()];
            readyEmitted_=NO;
            viewport_.hidden=YES;
            placement_.visible=false;
            preview_.reset();
        }
    }
}
// Agent snapshot handshake (MCP studio_character_snapshot): the tool writes
// <project>/.aiverse/agent-requests/character-snapshot.json {id, view}; the
// next shown character frame is saved as .aiverse/agent-snapshots/<id>.png.
- (void)pollAgentRequests {
    const CFTimeInterval now=CACurrentMediaTime();
    if (now<nextAgentPoll_) return;
    nextAgentPoll_=now+0.4;
    namespace fs=std::filesystem;
    const fs::path root=session_->workspace().project.dir;
    const fs::path request=root/".aiverse"/"agent-requests"/"character-snapshot.json";
    std::error_code ec;
    if (!fs::is_regular_file(request,ec) || fs::file_size(request,ec)>4096) return;
    std::string bytes;{std::ifstream in(request,std::ios::binary);bytes.assign(std::istreambuf_iterator<char>(in),{});}
    fs::remove(request,ec);
    try {
        const auto req=aiverse::Json::parse(bytes);
        const std::string id=req.getStr("id");
        if (id.empty()||id.size()>40||id.find_first_not_of("0123456789abcdefghijklmnopqrstuvwxyz-")!=std::string::npos) return;
        const fs::path out=root/".aiverse"/"agent-snapshots";
        fs::create_directories(out,ec);
        if (!preview_ || !session_->facialPreviewBinding().bound()) {
            std::ofstream err(out/(id+".error"));err<<"Покажите персонажа во вьюпорте Character Creator";return;
        }
        const std::string view=req.getStr("view");
        if (view=="face"||view=="torso"||view=="full") {
            aiverse::Json r=aiverse::Json::object(),pr=aiverse::Json::object();pr.set("view",aiverse::Json::string(view));
            r.set("id",aiverse::Json::number(int64_t(0)));r.set("method",aiverse::Json::string("studio.humanCamera"));r.set("params",pr);
            (void)session_->dispatch(r);
        }
        preview_->captureFacialThumbnail((out/(id+".png")).string(),false);
    } catch (const std::exception&) {}
}
- (void)runWindShots {
    auto& run=WindShotRunner::get();
    if(run.dir.empty()||run.done)return;
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
            run.scene=std::make_unique<aiverse::studio::StudioMetalScene>((__bridge void*)run.view.layer);
        }
        const CGFloat scale=run.window.backingScaleFactor;
        const int w=int(run.view.bounds.size.width*scale), h=int(run.view.bounds.size.height*scale);
        ((CAMetalLayer*)run.view.layer).drawableSize=CGSizeMake(w,h);
        run.scene->setWindPreview(session_->windPreview());
        run.scene->frame(w,h,session_->workspace().map,session_->workspace().project.isFps(),"",
            "",aiverse::Json{},0,float(session_->workspace().project.rules.levelHeight.toDouble()),
            session_->workspace().project.dir,session_->workspace().project.rulesJson);
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
        aiverse::Json request=aiverse::Json::object();
        request.set("id",aiverse::Json::number(int64_t(0)));
        request.set("method",aiverse::Json::string("studio.windSet"));
        request.set("params",aiverse::Json::parse(st.patch));
        const auto reply=session_->dispatch(request);
        if(reply.has("error"))NSLog(@"[wind-shots] %s: %s",st.name,reply.getStr("error").c_str());
    }
    if(!run.waiting && run.frames>=st.settle) {
        run.waiting=true; *run.captured=false;
        const std::string path=run.dir+"/"+st.name+".ppm";
        auto flag=run.captured;
        run.scene->captureNextFrame([path,flag](int w,int h,std::vector<uint8_t> rgba) {
            if(FILE* f=std::fopen(path.c_str(),"wb")) {
                std::fprintf(f,"P6\n%d %d\n255\n",w,h);
                for(size_t i=0;i+3<rgba.size();i+=4)std::fwrite(&rgba[i],1,3,f);
                std::fclose(f);
            }
            *flag=true;
        });
        return;
    }
    if(run.waiting && *run.captured) {
        NSLog(@"[wind-shots] %s captured",st.name);
        ++run.stage; run.frames=0; run.waiting=false;
    }
}
- (void)orbitX:(CGFloat)dx y:(CGFloat)dy {
    if (preview_ && std::isfinite(double(dx)) && std::isfinite(double(dy)))
        preview_->orbit(float(dx),float(dy));
}
- (void)zoom:(CGFloat)amount {
    if (preview_ && std::isfinite(double(amount))) preview_->zoom(float(amount));
}
- (void)lookX:(CGFloat)dx y:(CGFloat)dy {
    if (preview_ && std::isfinite(double(dx)) && std::isfinite(double(dy)))
        preview_->look(float(dx),float(dy));
}
- (void)panX:(CGFloat)dx y:(CGFloat)dy {
    if (preview_ && std::isfinite(double(dx)) && std::isfinite(double(dy)))
        preview_->pan(float(dx),float(dy));
}
- (void)moveRight:(CGFloat)right up:(CGFloat)up forward:(CGFloat)forward {
    if (preview_ && std::isfinite(double(right)) && std::isfinite(double(up)) &&
        std::isfinite(double(forward)))
        preview_->move(float(right),float(up),float(forward));
}
- (void)focusSceneSelection {
    if (projectLoading_) return;
    if (preview_ && session_) preview_->focusSelected(session_->workspace().map,
        session_->workspace().project.isFps(),session_->selectedObjectId());
}
- (void)resetSceneView { if (preview_) preview_->resetView(); }
- (BOOL)sceneMoveBeginAtX:(CGFloat)x y:(CGFloat)y width:(CGFloat)width height:(CGFloat)height {
    if (projectLoading_) return NO;
    return preview_ && preview_->beginTransformDrag(float(x),float(y),float(width),float(height));
}
- (void)sceneMoveUpdateAtX:(CGFloat)x y:(CGFloat)y width:(CGFloat)width height:(CGFloat)height invertSnap:(BOOL)invertSnap {
    if (!preview_) return;
    preview_->updateTransformDrag(float(x),float(y),float(width),float(height),invertSnap);
    const std::string caption=preview_->transformCaption();
    if (caption!=lastActionCaption_) { lastActionCaption_=caption; [self sendStudioEvent:"viewport.action" message:caption.c_str()]; }
}
- (void)setSceneTool:(int)tool {
    if (!preview_ || tool<0 || tool>3) return;
    preview_->setTransformTool(aiverse::studio::StudioMetalScene::TransformTool(tool));
    static const char* names[4]={"view","move","rotate","scale"};
    [self sendStudioEvent:"viewport.tool" message:names[tool]];
}
- (int)sceneTool { return preview_ ? int(preview_->transformTool()) : 1; }
- (void)sceneMoveEnd {
    if (projectLoading_) { if (preview_) preview_->clearTransformPreview(); return; }
    if (!preview_ || !session_) return;
    const auto commit=preview_->endTransformDrag();
    lastActionCaption_.clear();
    [self sendStudioEvent:"viewport.action" message:""];
    using Tool=aiverse::studio::StudioMetalScene::TransformTool;
    const bool unchanged=!commit ||
        (commit->tool==Tool::Move && std::abs(commit->move.x)+std::abs(commit->move.y)+std::abs(commit->move.z)<1e-4f) ||
        (commit->tool==Tool::Rotate && (!std::isfinite(commit->degrees) || std::abs(commit->degrees)<1e-3f)) ||
        (commit->tool==Tool::Scale && (!std::isfinite(commit->factor) || std::abs(commit->factor-1.0f)<1e-4f));
    if (unchanged) { preview_->clearTransformPreview(); return; }
    // Read current Transform values through the same inspector API the UI uses, then
    // write ONE undoable studio.setProperties (y promotes legacy units; lights use height).
    auto request=[&](const char* method,aiverse::Json params) {
        aiverse::Json r=aiverse::Json::object();
        r.set("id",aiverse::Json::number(int64_t(0)));r.set("method",aiverse::Json::string(method));r.set("params",std::move(params));
        return session_->dispatch(r);
    };
    aiverse::Json params=aiverse::Json::object();params.set("entityId",aiverse::Json::string(commit->id));
    const auto inspected=request("studio.inspect",params);
    std::map<std::string,double> current;
    for (const auto& component:inspected["result"]["components"].items()) {
        if (component.getStr("id")!="Transform") continue;
        for (const auto& field:component["properties"].items())
            if (field.getBool("editable",true) && field["value"].isNumber()) current[field.getStr("key")]=field["value"].toDouble();
    }
    aiverse::Json values=aiverse::Json::object();
    auto add=[&](std::initializer_list<const char*> keys,auto apply,int digits){
        for (const char* key:keys) if (current.count(key)) { values.set(key,aiverse::Json::number(apply(current[key]),digits)); return; }
    };
    if (commit->tool==Tool::Move) {
        const float d[3]={commit->move.x,commit->move.y,commit->move.z};
        if (std::abs(d[0])>1e-5f) add({"x"},[&](double v){return v+d[0];},4);
        if (std::abs(d[1])>1e-5f) add({"y","height"},[&](double v){return v+d[1];},4);
        if (std::abs(d[2])>1e-5f) add({"z"},[&](double v){return v+d[2];},4);
    } else if (commit->tool==Tool::Rotate && commit->axis>=0 && commit->axis<3) {
        static const char* keys[3]={"rotationX","rotationY","rotationZ"};
        add({keys[commit->axis]},[&](double v){return std::remainder(v+double(commit->degrees),360.0);},3);
    } else if (commit->tool==Tool::Scale) {
        static const char* keys[3]={"scaleX","scaleY","scaleZ"};
        for (int a=0;a<3;++a) if (commit->axis==3 || commit->axis==a || (commit->axis>=4 && commit->axis-4!=a))
            add({keys[a]},[&](double v){return std::clamp(v*double(commit->factor),0.001,10000.0);},4);
    }
    if (!values.members().empty()) {
        aiverse::Json set=aiverse::Json::object();
        set.set("entityId",aiverse::Json::string(commit->id));set.set("componentId",aiverse::Json::string("Transform"));
        set.set("values",values);
        const auto outcome=request("studio.setProperties",set);
        if (outcome.has("error")) NSLog(@"AIVERSE transform: %s",outcome.getStr("error").c_str());
    }
    preview_->clearTransformPreview();
    [self sendStudioEvent:"document.changed" message:""];
}
- (BOOL)sceneGizmoAtX:(CGFloat)x y:(CGFloat)y width:(CGFloat)width height:(CGFloat)height {
    if (!preview_) return NO;
    const int hit=preview_->orientationGizmoHit(float(x),float(y),float(width),float(height));
    if (hit<0) return NO;
    preview_->activateOrientationGizmo(hit);
    return YES;
}
- (std::vector<std::string>)materialSlotsFor:(const std::string&)asset {
    return preview_ ? preview_->modelMaterialSlots(asset) : std::vector<std::string>{};
}
- (void)publishSceneCamera {
    if (!preview_ || !htmlComposite_) return;
    const auto camera=preview_->cameraState();
    if (cameraSent_ && std::abs(camera.yaw-lastCameraYaw_)<1e-4 &&
        std::abs(camera.pitch-lastCameraPitch_)<1e-4 &&
        std::abs(camera.distance-lastCameraDistance_)<=1e-3*std::max(1.0,lastCameraDistance_) &&
        bool(lastCameraOrtho_)==camera.ortho) return;
    lastCameraYaw_=camera.yaw;lastCameraPitch_=camera.pitch;
    lastCameraDistance_=camera.distance;lastCameraOrtho_=camera.ortho;cameraSent_=YES;
    aiverse::Json payload=aiverse::Json::object();
    payload.set("yaw",aiverse::Json::number(double(camera.yaw),5));
    payload.set("pitch",aiverse::Json::number(double(camera.pitch),5));
    payload.set("distance",aiverse::Json::number(double(camera.distance),4));
    payload.set("ortho",aiverse::Json::fromBool(camera.ortho));
    [self sendStudioEventPayload:"viewport.camera" payload:payload];
}
- (void)selectAtX:(CGFloat)x y:(CGFloat)y width:(CGFloat)width height:(CGFloat)height {
    [self selectAtX:x y:y width:width height:height mode:"replace"];
}
- (void)sceneMarqueeFrom:(NSPoint)a to:(NSPoint)b width:(CGFloat)width height:(CGFloat)height {
    if (!preview_ || width<2 || height<2) return;
    preview_->setMarquee(true,float(a.x/width),float(a.y/height),float(b.x/width),float(b.y/height));
}
- (void)sceneMarqueeEndFrom:(NSPoint)a to:(NSPoint)b width:(CGFloat)width height:(CGFloat)height mode:(const char*)mode {
    if (projectLoading_) { if (preview_) preview_->setMarquee(false); return; }
    if (!preview_ || !session_) return;
    preview_->setMarquee(false);
    const auto ids=preview_->idsInRect(float(a.x),float(a.y),float(b.x),float(b.y),float(width),float(height),
        session_->workspace().map,session_->workspace().project.isFps());
    aiverse::Json request=aiverse::Json::object(),params=aiverse::Json::object(),list=aiverse::Json::array();
    for (const auto& id:ids) list.push(aiverse::Json::string(id));
    params.set("ids",list);params.set("mode",aiverse::Json::string(mode));
    request.set("id",aiverse::Json::number(int64_t(0)));request.set("method",aiverse::Json::string("studio.select"));request.set("params",params);
    if (!session_->dispatch(request).has("error")) [self sendStudioEvent:"selection.changed" message:""];
}
- (void)sceneHoverAtX:(CGFloat)x y:(CGFloat)y width:(CGFloat)width height:(CGFloat)height {
    if (projectLoading_) return;
    if (!preview_ || !session_ || preview_->transformDragActive()) return;
    const std::string id=x<0||y<0?std::string():preview_->pick(float(x),float(y),float(width),float(height),
        session_->workspace().map,session_->workspace().project.isFps());
    if (id==hoverId_) return;
    hoverId_=id;
    [self sendStudioEvent:"viewport.hover" message:id.c_str()];
}
- (void)sendLoading:(const char*)phase message:(const std::string&)message done:(int)done total:(int)total {
    aiverse::Json payload=aiverse::Json::object();
    payload.set("phase",aiverse::Json::string(phase));
    payload.set("message",aiverse::Json::string(message));
    payload.set("done",aiverse::Json::number(int64_t(done)));
    payload.set("total",aiverse::Json::number(int64_t(total)));
    payload.set("active",aiverse::Json::fromBool(std::string(phase)!="done"));
    [self sendStudioEventPayload:"project.loading" payload:payload];
}
// Opens a project on a background queue (the UI shows progress), then resumes the
// Scene, which streams models in with a per-frame budget. requestId<0: --project.
- (void)openProjectPath:(const std::string&)path requestId:(int64_t)requestId {
    if (projectLoading_) return;
    projectLoading_=YES;
    if (game_) game_->stop();
    const std::string name=std::filesystem::path(path).filename().string();
    [self sendLoading:"project" message:"Opening "+name done:0 total:0];
    aiverse::studio::StudioSession* session=session_.get();
    const std::string projectPath=path;
    __unsafe_unretained StudioHost* host=self;
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED,0),^{
        __block aiverse::Json snapshot;__block std::string error;
        try { snapshot=session->openProject(projectPath); }
        catch (const std::exception& e) { error=e.what(); }
        dispatch_async(dispatch_get_main_queue(),^{
            host->projectLoading_=NO;
            if (host->preview_) host->preview_->resetCamera();
            host->lastLoadDone_=host->lastLoadTotal_=-1;
            if (requestId>=0) {
                aiverse::Json reply=aiverse::Json::object();
                reply.set("id",aiverse::Json::number(requestId));
                if (!error.empty()) reply.set("error",aiverse::Json::string(error));
                else {
                    aiverse::Json payload=aiverse::Json::object();
                    payload.set("cancelled",aiverse::Json::fromBool(false));
                    payload.set("snapshot",snapshot);
                    reply.set("result",payload);
                }
                [host sendStudioReply:reply];
            }
            if (!error.empty()) {
                NSLog(@"AIVERSE open project failed: %s",error.c_str());
                [host sendLoading:"error" message:error done:0 total:0];
            } else {
                [[NSUserDefaults standardUserDefaults] setObject:[NSString stringWithUTF8String:projectPath.c_str()] forKey:@"AiverseLastProject"];
                // Scene models stream in next; the tick reports their progress.
                [host sendLoading:"scene" message:"Preparing scene" done:0 total:0];
                host->loadingShown_=YES;
            }
            [host sendStudioEvent:"workspace.changed" message:""];
        });
    });
}
- (void)sceneSelectionAction:(const char*)method {
    if (projectLoading_) return;
    if (!session_) return;
    aiverse::Json request=aiverse::Json::object(),params=aiverse::Json::object(),list=aiverse::Json::array();
    for (const auto& id:session_->selectedObjectIds()) list.push(aiverse::Json::string(id));
    if (list.size()==0) return;
    params.set("entityIds",list);
    request.set("id",aiverse::Json::number(int64_t(0)));request.set("method",aiverse::Json::string(method));request.set("params",params);
    const auto outcome=session_->dispatch(request);
    if (outcome.has("error")) { NSLog(@"AIVERSE %s: %s",method,outcome.getStr("error").c_str()); return; }
    [self sendStudioEvent:"document.changed" message:""];
    [self sendStudioEvent:"selection.changed" message:""];
}
- (BOOL)placeAsset:(const std::string&)asset atX:(CGFloat)x y:(CGFloat)y width:(CGFloat)width height:(CGFloat)height {
    if (projectLoading_) return NO;
    if (!preview_ || !session_) return NO;
    aiverse::render::float3 at{};
    if (!preview_->groundPoint(float(x),float(y),float(width),float(height),at)) return NO;
    aiverse::Json request=aiverse::Json::object(),params=aiverse::Json::object();
    params.set("asset",aiverse::Json::string(asset));
    params.set("x",aiverse::Json::number(double(at.x),3));params.set("z",aiverse::Json::number(double(at.z),3));
    request.set("id",aiverse::Json::number(int64_t(0)));request.set("method",aiverse::Json::string("studio.assetPlace"));request.set("params",params);
    const auto outcome=session_->dispatch(request);
    if (outcome.has("error")) {
        aiverse::Json payload=aiverse::Json::object();
        payload.set("message",aiverse::Json::string("Place in Scene: "+outcome.getStr("error")));
        [self sendStudioEventPayload:"assets.importError" payload:payload];
        return NO;
    }
    [self sendStudioEvent:"document.changed" message:""];
    [self sendStudioEvent:"selection.changed" message:""];
    return YES;
}
- (void)selectAtX:(CGFloat)x y:(CGFloat)y width:(CGFloat)width height:(CGFloat)height mode:(const char*)mode {
    if (projectLoading_) return;
    if (!preview_) return;
    if (session_->sculptBrush().regionPick) {
        // Face blend region choice: report where on the head the click landed.
        const auto& face=session_->facialPreviewBinding().face();
        aiverse::vivid::HumanFaceHit hit;
        aiverse::Json payload=aiverse::Json::object();
        std::string pickError;
        if (face && preview_->sculptPick(float(x),float(y),float(width),float(height),*face,hit,&pickError)) {
            aiverse::Json p=aiverse::Json::array();
            for(float v:{hit.point.x,hit.point.y,hit.point.z})p.push(aiverse::Json::number(double(v),5));
            payload.set("hit",aiverse::Json::fromBool(true));payload.set("point",std::move(p));
        } else {
            payload.set("hit",aiverse::Json::fromBool(false));
            if (!pickError.empty()) payload.set("error",aiverse::Json::string(pickError));
        }
        [self sendStudioEventPayload:"human.facePick" payload:payload];
        return;
    }
    if (session_->sculptBrush().armed) {
        // Human Creator sculpt: a click on the character is a brush stroke.
        const auto& face=session_->facialPreviewBinding().face();
        aiverse::vivid::HumanFaceHit hit;
        aiverse::Json payload=aiverse::Json::object();
        std::string pickError;
        if (face && preview_->sculptPick(float(x),float(y),float(width),float(height),*face,hit,&pickError)) {
            aiverse::Json request=aiverse::Json::object(),params=aiverse::Json::object(),p=aiverse::Json::array(),n=aiverse::Json::array();
            for(float v:{hit.point.x,hit.point.y,hit.point.z})p.push(aiverse::Json::number(double(v),6));
            for(float v:{hit.normal.x,hit.normal.y,hit.normal.z})n.push(aiverse::Json::number(double(v),6));
            params.set("point",std::move(p));params.set("normal",std::move(n));
            request.set("id",aiverse::Json::number(int64_t(0)));
            request.set("method",aiverse::Json::string("studio.humanSculptStroke"));
            request.set("params",std::move(params));
            const aiverse::Json outcome=session_->dispatch(request);
            payload.set("hit",aiverse::Json::fromBool(true));
            if (outcome.has("error")) payload.set("error",outcome["error"]);
        } else {
            payload.set("hit",aiverse::Json::fromBool(false));
            if (!pickError.empty()) payload.set("error",aiverse::Json::string(pickError));
        }
        [self sendStudioEventPayload:"human.sculpt" payload:payload];
        return;
    }
    const std::string objectId=preview_->pick(float(x),float(y),float(width),float(height),
        session_->workspace().map,session_->workspace().project.isFps());
    // A modifier click on empty space keeps the current selection.
    if (objectId.empty() && std::string(mode)!="replace") return;
    aiverse::Json request=aiverse::Json::object();
    request.set("id",aiverse::Json::number(int64_t(0)));
    request.set("method",aiverse::Json::string("studio.select"));
    aiverse::Json params=aiverse::Json::object();
    if(!objectId.empty()) params.set("id",aiverse::Json::string(objectId));
    else params.set("id",aiverse::Json());
    params.set("mode",aiverse::Json::string(mode));
    request.set("params",std::move(params));
    const aiverse::Json outcome=session_->dispatch(request);
    if (!outcome.has("error")) [self sendStudioEvent:"selection.changed" message:""];
}
- (BOOL)confirmDiscardChanges {
    const auto& ws=session_->workspace();
    if (!ws.map.dirty && !ws.project.rulesDirty && !ws.project.manifestDirty) return YES;
    if (window_.attachedSheet) return NO;
    NSAlert* alert=[[NSAlert alloc] init];
    alert.messageText=@"Discard unsaved changes?";
    alert.informativeText=@"Save the current document first to keep your changes.";
    [alert addButtonWithTitle:@"Cancel"];
    [alert addButtonWithTitle:@"Discard Changes"];
    return [alert runModal]==NSAlertSecondButtonReturn;
}
- (BOOL)windowShouldClose:(NSWindow*)sender {
    if (sender!=window_) return YES; // a floating panel just docks back
    if (window_.attachedSheet || ![self confirmDiscardChanges]) return NO;
    closeApproved_=YES;
    return YES;
}
- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication*)sender {
    (void)sender;
    if (closeApproved_) return NSTerminateNow;
    if (window_.attachedSheet) return NSTerminateCancel;
    return [self confirmDiscardChanges] ? NSTerminateNow : NSTerminateCancel;
}
- (void)saveMapSheetForRequestId:(int64_t)requestId {
    NSSavePanel* panel=[NSSavePanel savePanel];
    panel.canCreateDirectories=YES;
    panel.nameFieldStringValue=@"scene.json";
    const std::string directory=session_->workspace().project.dir+"/maps";
    panel.directoryURL=[NSURL fileURLWithPath:[NSString stringWithUTF8String:directory.c_str()]];
    [panel beginSheetModalForWindow:window_ completionHandler:^(NSModalResponse result) {
        aiverse::Json request=aiverse::Json::object();
        request.set("id",aiverse::Json::number(requestId));
        request.set("method",aiverse::Json::string("studio.snapshot"));
        aiverse::Json reply=session_->dispatch(request);
        if (result==NSModalResponseOK && panel.URL.path.length) {
            try {
                reply.set("result",session_->saveMapAs(panel.URL.path.UTF8String));
                [self sendStudioEvent:"document.changed" message:""];
            } catch (const std::exception& error) {
                reply=aiverse::Json::object();reply.set("id",aiverse::Json::number(requestId));
                reply.set("error",aiverse::Json::string(error.what()));
            }
        }
        [self sendStudioReply:reply];
    }];
}
- (void)sendStudioReply:(const aiverse::Json&)reply {
    const std::string output=reply.dump(-1);
    NSString* json=[[NSString alloc] initWithBytes:output.data() length:output.size() encoding:NSUTF8StringEncoding];
    if (!json) return;
    NSString* js=[NSString stringWithFormat:@"window.__aiverseReceive && window.__aiverseReceive(%@);",json];
    if (currentWeb_) [currentWeb_ evaluateJavaScript:js completionHandler:nil];
    else [self evaluateInAllWebs:js];
}
- (void)sendProjectCancelledReply:(int64_t)requestId {
    aiverse::Json reply=aiverse::Json::object();
    reply.set("id",aiverse::Json::number(requestId));
    aiverse::Json payload=aiverse::Json::object();
    payload.set("cancelled",aiverse::Json::fromBool(true));
    payload.set("snapshot",aiverse::Json());
    reply.set("result",std::move(payload));
    [self sendStudioReply:reply];
}
- (void)openProjectSheetForRequestId:(int64_t)requestId {
    [self openProjectSheetForRequestId:requestId confirmedDiscard:NO];
}
- (void)openProjectSheetForRequestId:(int64_t)requestId confirmedDiscard:(BOOL)confirmedDiscard {
    const auto& ws=session_->workspace();
    if (!confirmedDiscard && (ws.map.dirty || ws.project.rulesDirty || ws.project.manifestDirty)) {
        NSAlert* alert=[[NSAlert alloc] init];
        alert.messageText=@"Discard unsaved AIVERSE changes?";
        alert.informativeText=@"Opening another project discards unsaved map and project changes.";
        [alert addButtonWithTitle:@"Discard and Open"];
        [alert addButtonWithTitle:@"Cancel"];
        [alert beginSheetModalForWindow:window_ completionHandler:^(NSModalResponse code) {
            if (code == NSAlertFirstButtonReturn)
                [self openProjectSheetForRequestId:requestId confirmedDiscard:YES];
            else [self sendProjectCancelledReply:requestId];
        }];
        return;
    }
    NSOpenPanel* panel=[NSOpenPanel openPanel];
    panel.canChooseFiles=YES;
    panel.canChooseDirectories=YES;
    panel.allowsMultipleSelection=NO;
    panel.canCreateDirectories=NO;
    panel.prompt=@"Open Project";
    [panel beginSheetModalForWindow:window_ completionHandler:^(NSModalResponse result) {
        aiverse::Json reply=aiverse::Json::object();
        reply.set("id",aiverse::Json::number(requestId));
        aiverse::Json payload=aiverse::Json::object();
        if (result==NSModalResponseOK && panel.URL.path.length) {
            [self openProjectPath:panel.URL.path.UTF8String requestId:requestId];
            return; // replies when the background load finishes
        } else {
            payload.set("cancelled",aiverse::Json::fromBool(true));
            payload.set("snapshot",aiverse::Json());
        }
        if (!reply.has("error")) reply.set("result",std::move(payload));
        [self sendStudioReply:reply];
    }];
}
- (void)openAssetImportForRequestId:(int64_t)requestId destination:(NSString*)destination {
    if(session_->workspace().project.dir.empty()){
        aiverse::Json response=aiverse::Json::object();
        response.set("id",aiverse::Json::number(requestId));
        response.set("error",aiverse::Json::string("Open a project before importing assets"));
        [self sendStudioReply:response];
        return;
    }
    NSOpenPanel* panel=[NSOpenPanel openPanel];
    panel.canChooseFiles=YES;
    panel.canChooseDirectories=YES;
    panel.allowsMultipleSelection=YES;
    panel.canCreateDirectories=NO;
    panel.prompt=@"Import into content";
    panel.message=@"Select files or folders to copy into the project's content directory.";
    panel.directoryURL=[NSURL fileURLWithPath:
        [NSString stringWithUTF8String:session_->workspace().project.dir.c_str()] isDirectory:YES];
    NSString* safeDestination=[destination copy];
    [panel beginSheetModalForWindow:window_ completionHandler:^(NSModalResponse code) {
        aiverse::Json reply=aiverse::Json::object();
        reply.set("id",aiverse::Json::number(requestId));
        if(code==NSModalResponseOK && panel.URLs.count){
            try {
                std::vector<std::filesystem::path> sources;
                if(panel.URLs.count>64)throw std::runtime_error("Select at most 64 assets");
                for(NSURL* url in panel.URLs){
                    if(!url.isFileURL)throw std::runtime_error("Expected a local file");
                    sources.emplace_back(url.path.UTF8String);
                }
                reply.set("result",session_->previewExternalAssets(sources,
                    safeDestination.UTF8String ?: ""));
            }catch(const std::exception& error){
                reply.set("error",aiverse::Json::string(error.what()));
            }
        }else{
            aiverse::Json cancelled=aiverse::Json::object();
            cancelled.set("cancelled",aiverse::Json::fromBool(true));
            reply.set("result",std::move(cancelled));
        }
        [self sendStudioReply:reply];
    }];
}
- (void)importDroppedFileURLs:(NSArray<NSURL*>*)urls atWindowPoint:(NSPoint)point {
    [self importDroppedFileURLs:urls atWindowPoint:point fromWeb:web_];
}
- (void)importDroppedFileURLs:(NSArray<NSURL*>*)urls atWindowPoint:(NSPoint)point fromWeb:(WKWebView*)web {
    if(urls.count==0 || urls.count>64 || session_->workspace().project.dir.empty() || !web)return;
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
    [web evaluateJavaScript:js completionHandler:^(id result,NSError* error) {
        if(error || ![result isKindOfClass:[NSString class]])return;
        const std::string folder=[(NSString*)result UTF8String];
        try{
            std::vector<std::filesystem::path> sources;
            sources.reserve(safeURLs.count);
            for(NSURL* url in safeURLs){
                if(!url.isFileURL)throw std::runtime_error("Only local files can be imported");
                sources.emplace_back(url.path.UTF8String);
            }
            const auto preview=session_->previewExternalAssets(sources,folder);
            [self sendStudioEventPayload:"assets.importPreview" payload:preview];
        }catch(const std::exception& ex){
            aiverse::Json payload=aiverse::Json::object();
            payload.set("message",aiverse::Json::string(ex.what()));
            [self sendStudioEventPayload:"assets.importError" payload:payload];
        }
    }];
}
- (void)sendStudioEventPayload:(const char*)event payload:(const aiverse::Json&)payload {
    aiverse::Json packet=aiverse::Json::object();
    packet.set("event",aiverse::Json::string(event));
    packet.set("payload",payload);
    const std::string bytes=packet.dump(-1);
    NSString* text=[[NSString alloc]initWithBytes:bytes.data()
        length:bytes.size() encoding:NSUTF8StringEncoding];
    if(text)[self evaluateInAllWebs:[NSString stringWithFormat:
        @"window.__aiverseReceive && window.__aiverseReceive(%@);",text]];
}
- (void)sendStudioEvent:(const char*)event message:(const char*)message {
    aiverse::Json packet=aiverse::Json::object();
    packet.set("event",aiverse::Json::string(event));
    aiverse::Json payload=aiverse::Json::object();
    payload.set("message",aiverse::Json::string(message ? message : ""));
    packet.set("payload",std::move(payload));
    const std::string output=packet.dump(-1);
    NSString* json=[[NSString alloc] initWithBytes:output.data() length:output.size() encoding:NSUTF8StringEncoding];
    if (json) [self evaluateInAllWebs:[NSString stringWithFormat:@"window.__aiverseReceive && window.__aiverseReceive(%@);",json]];
}
- (void)updateGameChrome {
    gameChrome_.frame=gameViewport_.bounds;
    try {
        const aiverse::Json state=[self gameState];
        const auto& device=state["device"];
        const auto& chrome=device["chrome"];
        const double width=device["width"].toDouble(),k=width>0 ? gamePlacement_.width/width : 0;
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
// Moves a native view into the window of the WebView that hosts its panel.
// Returns NO for a stale report from a window that no longer hosts the view.
- (BOOL)adoptNativeView:(NSView*)view forWeb:(WKWebView*)web visible:(BOOL)visible {
    if (view.superview==web.superview) return YES;
    if (!visible) return NO;
    [view removeFromSuperview];
    [web.superview addSubview:view positioned:((htmlComposite_ && view==viewport_) ? NSWindowBelow : NSWindowAbove) relativeTo:web];
    return YES;
}
- (void)applyViewport:(const aiverse::Json&)params {
    WKWebView* host=currentWeb_ ?: web_;
    if(params.getStr("id")=="game") {
        if(![self adoptNativeView:gameViewport_ forWeb:host visible:params.getBool("visible")])return;
        aiverse::studio::ViewportRect rect;
        rect.x=params["x"].toDouble();rect.y=params["y"].toDouble();
        rect.width=params["width"].toDouble();rect.height=params["height"].toDouble();
        rect.dpr=params["dpr"].toDouble(1);rect.visible=params.getBool("visible");
        gamePlacement_=aiverse::studio::viewportPlacement(rect,host.bounds.size.width,
            host.bounds.size.height,host.window.backingScaleFactor);
        if(gameViewport_.hidden!=!gamePlacement_.visible)gameViewport_.hidden=!gamePlacement_.visible;
        if(gamePlacement_.visible) {
            [self placeMetalView:gameViewport_ x:gamePlacement_.x y:gamePlacement_.y
                width:gamePlacement_.width height:gamePlacement_.height pixelsW:gamePlacement_.pixelsW pixelsH:gamePlacement_.pixelsH];
            [self updateGameChrome];
        }
        else if(game_)game_->suspend();
        return;
    }
    aiverse::studio::ViewportRect rect;
    if (params.getStr("id") != "scene") { if (viewport_.superview==host.superview) { viewport_.hidden=YES; placement_={}; } return; }
    if (![self adoptNativeView:viewport_ forWeb:host visible:params.getBool("visible")]) return;
    rect.x=params["x"].toDouble(); rect.y=params["y"].toDouble();
    rect.width=params["width"].toDouble(); rect.height=params["height"].toDouble();
    rect.dpr=params["dpr"].toDouble(1);
    rect.visible=params.getBool("visible");
    NSRect webBounds=host.bounds;
    placement_=aiverse::studio::viewportPlacement(rect,
        webBounds.size.width,webBounds.size.height,host.window.backingScaleFactor);
    if (viewport_.hidden!=!placement_.visible) viewport_.hidden=!placement_.visible;
    if (!placement_.visible) readyEmitted_=NO;
    if (placement_.visible) [self placeMetalView:viewport_ x:placement_.x y:placement_.y
        width:placement_.width height:placement_.height pixelsW:placement_.pixelsW pixelsH:placement_.pixelsH];
}
// Moving the view or resizing its drawable repaints the window: only on real change.
- (void)placeMetalView:(NSView*)view x:(double)x y:(double)y width:(double)width height:(double)height
               pixelsW:(int)pixelsW pixelsH:(int)pixelsH {
    const NSRect frame=NSMakeRect(x,y,width,height);
    if (!NSEqualRects(view.frame,frame)) view.frame=frame;
    CAMetalLayer* layer=(CAMetalLayer*)view.layer;
    const CGFloat scale=(view.window ?: window_).backingScaleFactor;
    if (layer.contentsScale!=scale) layer.contentsScale=scale;
    const CGSize size=CGSizeMake(pixelsW,pixelsH);
    if (!CGSizeEqualToSize(layer.drawableSize,size)) layer.drawableSize=size;
}
- (void)webView:(WKWebView*)view didFailProvisionalNavigation:(WKNavigation*)navigation withError:(NSError*)error {
    (void)view; (void)navigation;
    NSLog(@"[studio-web] navigation failed: %@",error);
}
- (void)webView:(WKWebView*)view didFailNavigation:(WKNavigation*)navigation withError:(NSError*)error {
    (void)view; (void)navigation;
    NSLog(@"[studio-web] navigation error: %@",error);
}
- (void)webViewWebContentProcessDidTerminate:(WKWebView*)view {
    (void)view;
    NSLog(@"[studio-web] WebKit content process terminated");
}
- (void)webView:(WKWebView*)view didFinishNavigation:(WKNavigation*)navigation {
    (void)navigation;
    NSLog(@"[studio-web] top-level navigation finished: %@",view.URL);
    if (htmlComposite_) [view evaluateJavaScript:@"document.documentElement.classList.add('native-composite');" completionHandler:nil];
    if (!pendingProject_.empty()) {
        const std::string project=pendingProject_;pendingProject_.clear();
        [self openProjectPath:project requestId:-1];
    }
}
- (void)webView:(WKWebView*)view decidePolicyForNavigationAction:(WKNavigationAction*)action decisionHandler:(void (^)(WKNavigationActionPolicy))done {
    (void)view;
    done([self trusted:action.request.URL] ? WKNavigationActionPolicyAllow : WKNavigationActionPolicyCancel);
}
- (void)userContentController:(WKUserContentController*)controller didReceiveScriptMessage:(WKScriptMessage*)message {
    (void)controller;
    currentWeb_=message.webView;
    [self handleBridgeMessage:message];
    currentWeb_=nil;
}
- (void)handleBridgeMessage:(WKScriptMessage*)message {
    if (![message.name isEqualToString:@"aiverse"] || !message.frameInfo.isMainFrame ||
        ![self trusted:message.frameInfo.request.URL] || ![NSJSONSerialization isValidJSONObject:message.body]) return;
    NSError* error=nil;
    NSData* data=[NSJSONSerialization dataWithJSONObject:message.body options:0 error:&error];
    if (error || !data || data.length>1024*1024) return;
    try {
        std::string bytes((const char*)data.bytes,(size_t)data.length);
        const aiverse::Json req=aiverse::Json::parse(bytes);
        if (req.getStr("method")=="studio.windowDrag") {
            // Title-bar drag from HTML: the WebView covers the transparent title bar.
            NSEvent* current=[NSApp currentEvent];
            NSEvent* press=(current && (current.type==NSEventTypeLeftMouseDown || current.type==NSEventTypeLeftMouseDragged))
                ? current : gStudioLastWebMouseDown;
            NSWindow* target=currentWeb_.window ?: window_;
            if (press && ([NSEvent pressedMouseButtons] & 1u)) [target performWindowDragWithEvent:press];
            return;
        }
        if (req.getStr("method")=="studio.buildReveal" && req["id"].isNumber()) {
            // Show a build folder (or the last build log) in Finder; the path is the session's, never JS-provided.
            aiverse::Json reply=aiverse::Json::object();reply.set("id",req["id"]);
            try {
                const auto& ws=session_->workspace();
                if (ws.project.dir.empty()) throw std::runtime_error("Open a project first");
                const std::string target=req["params"].getStr("target");
                static const std::regex valid("^(macos|ios|android|web|windows|linux)?$");
                if (!std::regex_match(target,valid)) throw std::runtime_error("Unknown build target");
                std::filesystem::path path=std::filesystem::path(ws.project.dir)/"Builds";
                if (!target.empty()) path/=target;
                if (req["params"].getBool("log")) path=const_cast<aiverse::editor::Workspace&>(ws).lastLogFile();
                std::error_code ec;
                if (path.empty() || !std::filesystem::exists(path,ec)) throw std::runtime_error("Nothing built there yet");
                NSURL* url=[NSURL fileURLWithPath:[NSString stringWithUTF8String:path.string().c_str()]];
                [[NSWorkspace sharedWorkspace] activateFileViewerSelectingURLs:@[url]];
                aiverse::Json result=aiverse::Json::object();result.set("ok",aiverse::Json::fromBool(true));
                reply.set("result",std::move(result));
            } catch (const std::exception& e) { reply.set("error",aiverse::Json::string(e.what())); }
            [self sendStudioReply:reply];
            return;
        }
        if (req.getStr("method")=="studio.windowZoom") { [(currentWeb_.window ?: window_) zoom:nil]; return; }
        if (req["id"].isNumber() && (req.getStr("method")=="studio.windowOpen" || req.getStr("method")=="studio.windowClose" ||
                                     req.getStr("method")=="studio.windowBroadcast" || req.getStr("method")=="studio.windowTitle")) {
            aiverse::Json reply=aiverse::Json::object();
            reply.set("id",req["id"]);
            try {
                const std::string method=req.getStr("method");
                const auto& params=req["params"];
                aiverse::Json result=aiverse::Json::object();
                if (method=="studio.windowOpen") result=[self openFloatingPanel:params];
                else if (method=="studio.windowClose") {
                    NSWindow* f=floatWindows_[[NSString stringWithUTF8String:params.getStr("window").c_str()]];
                    result.set("closed",aiverse::Json::fromBool(f!=nil));
                    if (f) dispatch_async(dispatch_get_main_queue(),^{ [f close]; });
                } else if (method=="studio.windowTitle") {
                    // A floating window names itself after its active tab.
                    const std::string title=params.getStr("title");
                    NSWindow* w=currentWeb_.window;
                    if (title.size()>64 || !w || w==window_) throw std::runtime_error("Invalid window title");
                    w.title=[NSString stringWithUTF8String:title.c_str()] ?: @"AIVERSE";
                    result.set("ok",aiverse::Json::fromBool(true));
                } else {
                    // UI-only notifications between Studio windows (no Workspace access).
                    const std::string event=params.getStr("event");
                    if (event.rfind("ui.",0)!=0 || event.size()>64) throw std::runtime_error("Invalid window event");
                    [self sendStudioEventPayload:event.c_str() payload:params["payload"]];
                    result.set("ok",aiverse::Json::fromBool(true));
                }
                reply.set("result",std::move(result));
            } catch (const std::exception& e) { reply.set("error",aiverse::Json::string(e.what())); }
            [self sendStudioReply:reply];
            return;
        }
        if (req.getStr("method")=="studio.webDiagnostic") {
            const std::string note=req["params"].getStr("message");
            if (note.size()<=1200) NSLog(@"[studio-web] JS: %s",note.c_str());
            return; // diagnostics never touch Workspace or the filesystem
        }
        if (req.getStr("method")=="studio.humanMakeupAlign" && req["id"].isNumber()) {
            // Makeup layers placed from the shown face (needs the GPU preview's CPU copy).
            aiverse::Json reply=aiverse::Json::object();
            reply.set("id",req["id"]);
            try {
                if (!preview_) throw std::runtime_error("Покажите персонажа во вьюпорте");
                aiverse::Json result=aiverse::Json::object();
                result.set("values",preview_->alignMakeup(session_->facialPreviewBinding()));
                reply.set("result",std::move(result));
            } catch (const std::exception& ex) { reply.set("error",aiverse::Json::string(ex.what())); }
            [self sendStudioReply:reply];
            return;
        }
        if (req.getStr("method")=="studio.humanLookCapture" && req["id"].isNumber()) {
            // Preset card picture: the next character frame is cropped around
            // the head and written beside the look as <name>.png.
            aiverse::Json reply=aiverse::Json::object();
            reply.set("id",req["id"]);
            try {
                if (!preview_ || !session_->facialPreviewBinding().bound())
                    throw std::runtime_error("Покажите персонажа во вьюпорте, чтобы снять превью");
                const auto path=aiverse::studio::studioHumanLookThumbnailFile(
                    session_->workspace().project.dir,req["params"].getStr("name"),true);
                preview_->captureFacialThumbnail(path.string());
                aiverse::Json result=aiverse::Json::object();
                result.set("ok",aiverse::Json::fromBool(true));
                reply.set("result",std::move(result));
            } catch (const std::exception& ex) {
                reply.set("error",aiverse::Json::string(ex.what()));
            }
            [self sendStudioReply:reply];
            return;
        }
        if (req.getStr("method")=="studio.viewportInput" && req["id"].isNumber()) {
            aiverse::Json reply=aiverse::Json::object();
            reply.set("id",req["id"]);
            const auto& input=req["params"];
            const std::string kind=input.getStr("kind");
            aiverse::Json result=aiverse::Json::object();
            // Pointer/keyboard navigation comes from HTML only in composite mode (the
            // Metal NSView handles it otherwise); toolbar commands work in both.
            const bool toolbarCommand=kind=="focus" || kind=="reset" || kind=="view" || kind=="projection" || kind=="tool" || kind=="hover";
            if ((!htmlComposite_ && !toolbarCommand) || !placement_.visible || !preview_ || viewport_.hidden) {
                reply.set("error",aiverse::Json::string("Viewport input unavailable"));
            } else if (kind=="orbit" && input["dx"].isNumber() && input["dy"].isNumber()) {
                const double dx=input["dx"].toDouble(),dy=input["dy"].toDouble();
                if (!std::isfinite(dx) || !std::isfinite(dy) || std::abs(dx)>1000 || std::abs(dy)>1000)
                    reply.set("error",aiverse::Json::string("Invalid orbit delta"));
                else { [self orbitX:dx y:dy]; result.set("ok",aiverse::Json::fromBool(true)); }
            } else if (kind=="look" && input["dx"].isNumber() && input["dy"].isNumber()) {
                const double dx=input["dx"].toDouble(),dy=input["dy"].toDouble();
                if (!std::isfinite(dx)||!std::isfinite(dy)||std::abs(dx)>1000||std::abs(dy)>1000)
                    reply.set("error",aiverse::Json::string("Invalid look delta"));
                else { [self lookX:dx y:dy]; result.set("ok",aiverse::Json::fromBool(true)); }
            } else if (kind=="pan" && input["dx"].isNumber() && input["dy"].isNumber()) {
                const double dx=input["dx"].toDouble(),dy=input["dy"].toDouble();
                if (!std::isfinite(dx)||!std::isfinite(dy)||std::abs(dx)>1000||std::abs(dy)>1000)
                    reply.set("error",aiverse::Json::string("Invalid pan delta"));
                else { [self panX:dx y:dy]; result.set("ok",aiverse::Json::fromBool(true)); }
            } else if (kind=="move" && input["right"].isNumber() &&
                       input["up"].isNumber() && input["forward"].isNumber()) {
                const double right=input["right"].toDouble(),up=input["up"].toDouble(),
                             forward=input["forward"].toDouble();
                if (!std::isfinite(right)||!std::isfinite(up)||!std::isfinite(forward)||
                    std::abs(right)>50||std::abs(up)>50||std::abs(forward)>50)
                    reply.set("error",aiverse::Json::string("Invalid camera movement"));
                else { [self moveRight:right up:up forward:forward];
                       result.set("ok",aiverse::Json::fromBool(true)); }
            } else if (kind=="focus") {
                [self focusSceneSelection]; result.set("ok",aiverse::Json::fromBool(true));
            } else if (kind=="reset") {
                [self resetSceneView]; result.set("ok",aiverse::Json::fromBool(true));
            } else if (kind=="view" && input["yaw"].isNumber() && input["pitch"].isNumber()) {
                const double yaw=input["yaw"].toDouble(),pitch=input["pitch"].toDouble();
                if (!std::isfinite(yaw)||!std::isfinite(pitch)||std::abs(yaw)>100||std::abs(pitch)>1.5708)
                    reply.set("error",aiverse::Json::string("Invalid camera orientation"));
                else { preview_->setOrientation(float(yaw),float(pitch));
                       result.set("ok",aiverse::Json::fromBool(true)); }
            } else if (kind=="handle" && input["phase"].isString() && input["x"].isNumber() && input["y"].isNumber() &&
                       input["width"].isNumber() && input["height"].isNumber()) {
                const double x=input["x"].toDouble(),y=input["y"].toDouble(),w=input["width"].toDouble(),h=input["height"].toDouble();
                const std::string phase=input.getStr("phase");
                if (!std::isfinite(x)||!std::isfinite(y)||!std::isfinite(w)||!std::isfinite(h)||w<2||h<2||w>20000||h>20000)
                    reply.set("error",aiverse::Json::string("Invalid move coordinates"));
                else if (phase=="down") result.set("hit",aiverse::Json::fromBool([self sceneMoveBeginAtX:x y:y width:w height:h]));
                else if (phase=="drag") { [self sceneMoveUpdateAtX:x y:y width:w height:h invertSnap:input["invertSnap"].boolean(false)]; result.set("ok",aiverse::Json::fromBool(true)); }
                else if (phase=="up") { [self sceneMoveEnd]; result.set("ok",aiverse::Json::fromBool(true)); }
                else reply.set("error",aiverse::Json::string("Invalid move phase"));
            } else if (kind=="hover") {
                // Hierarchy row hover highlights the object in the Scene.
                const std::string id=input["id"].isString()?input["id"].str():"";
                if (id.size()>256) reply.set("error",aiverse::Json::string("Invalid hover id"));
                else { hoverId_=id; result.set("ok",aiverse::Json::fromBool(true)); }
            } else if (kind=="tool") {
                // Scene toolbar: tool, Global/Local, snapping, gizmo visibility.
                static const char* names[4]={"view","move","rotate","scale"};
                const std::string tool=input.getStr("tool");
                for (int i=0;i<4;++i) if (tool==names[i]) preview_->setTransformTool(aiverse::studio::StudioMetalScene::TransformTool(i));
                if (input["local"].boolean(false)==input["local"].boolean(true)) preview_->setLocalSpace(input["local"].boolean());
                if (input["gizmos"].boolean(false)==input["gizmos"].boolean(true)) preview_->setGizmosVisible(input["gizmos"].boolean());
                if (input["snap"].boolean(false)==input["snap"].boolean(true))
                    preview_->setSnap(input["snap"].boolean(),float(input["snapMove"].toDouble(0)),
                        float(input["snapRotate"].toDouble(0)),float(input["snapScale"].toDouble(0)));
                result.set("tool",aiverse::Json::string(names[int(preview_->transformTool())]));
                result.set("ok",aiverse::Json::fromBool(true));
            } else if (kind=="projection" &&
                       input["ortho"].boolean(false)==input["ortho"].boolean(true)) { // a real JSON bool
                preview_->setOrthographic(input["ortho"].boolean());
                result.set("ok",aiverse::Json::fromBool(true));
            } else if (kind=="zoom" && input["amount"].isNumber()) {
                const double wheel=input["amount"].toDouble();
                if (!std::isfinite(wheel) || std::abs(wheel)>1000)
                    reply.set("error",aiverse::Json::string("Invalid zoom delta"));
                else { [self zoom:wheel]; result.set("ok",aiverse::Json::fromBool(true)); }
            } else if (kind=="pick" && input["x"].isNumber() && input["y"].isNumber() &&
                       input["width"].isNumber() && input["height"].isNumber()) {
                const double x=input["x"].toDouble(),y=input["y"].toDouble();
                const double w=input["width"].toDouble(),h=input["height"].toDouble();
                if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(w) || !std::isfinite(h) ||
                    w<2 || h<2 || w>20000 || h>20000 || x<0 || y<0 || x>w || y>h)
                    reply.set("error",aiverse::Json::string("Invalid pick coordinates"));
                else { [self selectAtX:x y:y width:w height:h]; result.set("ok",aiverse::Json::fromBool(true)); }
            } else reply.set("error",aiverse::Json::string("Unsupported viewport input"));
            if (!reply.has("error")) reply.set("result",std::move(result));
            [self sendStudioReply:reply];
            return;
        }
        const auto gameMethod=req.getStr("method");
        if((gameMethod=="studio.gameEntities" || gameMethod=="studio.gameInspect") && req["id"].isNumber()) {
            auto reply=aiverse::Json::object();reply.set("id",req["id"]);
            try {
                const auto& args=req["params"];
                if(gameMethod=="studio.gameEntities") {
                    if(!args["offset"].isNumber() || !args["limit"].isNumber() || !args["query"].isString() ||
                       args["offset"].toDouble()<0 || args["offset"].toDouble()>10000000 ||
                       args["limit"].toDouble()<1 || args["limit"].toDouble()>100)
                        throw std::runtime_error("Invalid runtime page");
                    const auto offset=int(args["offset"].integer()),limit=int(args["limit"].integer());
                    if(game_)reply.set("result",game_->entities(offset,limit,args.getStr("query")));
                    else {aiverse::studio::StudioRuntimeInspect empty;reply.set("result",empty.entities(nullptr,"0",offset,limit,args.getStr("query")));}
                }else {
                    if(!game_)throw std::runtime_error("No game is running");
                    reply.set("result",game_->inspect(args.getStr("epoch"),args.getStr("ref")));
                }
            }catch(const std::exception& e){reply.set("error",aiverse::Json::string(e.what()));}
            [self sendStudioReply:reply];return;
        }
        if(gameMethod=="studio.gameDevices" && req["id"].isNumber()) {
            auto reply=aiverse::Json::object();reply.set("id",req["id"]);
            reply.set("result",aiverse::studio::studioGameDevices());[self sendStudioReply:reply];return;
        }
        if((gameMethod=="studio.gameStart" || gameMethod=="studio.gameStop" || gameMethod=="studio.gameState" || gameMethod=="studio.gameControl" || gameMethod=="studio.gameDevice") && req["id"].isNumber()) {
            aiverse::Json reply=aiverse::Json::object();reply.set("id",req["id"]);
            try {
                if(gameMethod=="studio.gameDevice") {
                    const auto profile=aiverse::studio::StudioGameDevice::parse(req["params"]);
                    if(!game_)game_=std::make_unique<aiverse::studio::StudioGameSurface>((__bridge void*)gameViewport_.layer);
                    game_->setDevice(profile);
                    [self updateGameChrome];
                }else if(gameMethod=="studio.gameStart") {
                    const auto entry=req["params"].getStr("entry");
                    if(entry!="map" && entry!="menu")throw std::runtime_error("Invalid game entry");
                    const auto plan=aiverse::studio::studioGameLaunch(session_->workspace(),entry=="map");
                    if(!game_)game_=std::make_unique<aiverse::studio::StudioGameSurface>((__bridge void*)gameViewport_.layer);
                    game_->start(plan,std::string(AIVERSE_STUDIO_SOURCE_DIR)+"/engine/assets");
                    gameFrameTime_=0;
                }else if(gameMethod=="studio.gameStop" && game_)game_->stop();
                else if(gameMethod=="studio.gameControl") {
                    if(!game_)throw std::runtime_error("No game is running");
                    game_->control(req["params"].getStr("action"));
                }
                reply.set("result",[self gameState]);
            }catch(const std::exception& e){reply.set("error",aiverse::Json::string(e.what()));}
            [self sendStudioReply:reply];return;
        }
        if (req.getStr("method")=="studio.openProject" && req["id"].isNumber()) {
            // Path is chosen by AppKit, never accepted from untrusted JavaScript.
            [self openProjectSheetForRequestId:req["id"].integer()];
            return;
        }
        const std::string documentMethod=req.getStr("method");
        if (req["id"].isNumber() && !session_->workspace().project.dir.empty()) {
            if (documentMethod=="studio.saveAs" ||
                (documentMethod=="studio.save" && session_->workspace().map.path.empty())) {
                [self saveMapSheetForRequestId:req["id"].integer()];return;
            }
            if ((documentMethod=="studio.openMap" || documentMethod=="studio.newMap") && ![self confirmDiscardChanges]) {
                auto cancelled=req;cancelled.set("method",aiverse::Json::string("studio.snapshot"));
                [self sendStudioReply:session_->dispatch(cancelled)];return;
            }
        }
        if (req.getStr("method")=="studio.assetImportPreview" && req["id"].isNumber()) {
            // The browser can choose a verified relative DESTINATION only.
            // The local SOURCE paths come exclusively from a native NSOpenPanel.
            const std::string destination=req["params"].getStr("destination");
            if(destination.size()>512){
                aiverse::Json reply=aiverse::Json::object();
                reply.set("id",req["id"]);
                reply.set("error",aiverse::Json::string("Invalid import destination"));
                [self sendStudioReply:reply];
            }else{
                NSString* folder=[NSString stringWithUTF8String:destination.c_str()];
                [self openAssetImportForRequestId:req["id"].integer() destination:folder];
            }
            return;
        }
        if(game_ && (documentMethod=="studio.openMap" || documentMethod=="studio.newMap"))game_->stop();
        aiverse::Json reply;
        if (projectLoading_ && req.getStr("method")!="studio.viewportRect") {
            aiverse::Json busy=aiverse::Json::object();
            busy.set("id",req["id"]);busy.set("error",aiverse::Json::string("Project is loading"));
            [self sendStudioReply:busy];
            return;
        }
        if (req.getStr("method")=="studio.viewportRect" && req["id"].isNumber()) {
            [self applyViewport:req["params"]];
            reply=aiverse::Json::object();
            reply.set("id",req["id"]);
            aiverse::Json result=aiverse::Json::object();
            result.set("ok",aiverse::Json::fromBool(true));
            reply.set("result",std::move(result));
        } else {
            if (preview_ && (req.getStr("method")=="studio.wind" || req.getStr("method")=="studio.windSet"))
                session_->setWindSceneStats(preview_->windStats());
            reply=session_->dispatch(req);
        }
        [self sendStudioReply:reply];
        const std::string method=req.getStr("method");
        if (!reply.has("error") && method=="studio.select" && floatWebs_.count)
            [self sendStudioEvent:"selection.changed" message:""];
        if (!reply.has("error") && preview_ && (method=="studio.openMap" || method=="studio.newMap"))
            preview_->resetCamera();
        if (!reply.has("error") && method=="studio.materialPreview") {
            [self sendStudioEvent:"material.preview" message:req["params"].getStr("name").c_str()];
        }
        if (!reply.has("error") &&
            (method=="studio.openMap" || method=="studio.newMap" ||
             method=="studio.entityCreate" || method=="studio.entityDelete" ||
             method=="studio.entityDuplicate" || method=="studio.setProperty" || method=="studio.save" ||
             method=="studio.undo" || method=="studio.redo" ||
             method=="studio.nereidFlowSet" || method=="studio.renderLightingSet" || method=="studio.lightingSet" || method=="studio.lightAdd" ||
             method=="studio.lightUpdate" || method=="studio.lightDelete" ||
             method=="studio.materialPatch" || method=="studio.materialCreate" || method=="studio.setProperties" ||
             method=="studio.componentAdd" || method=="studio.componentRemove" || method=="studio.entityMove" ||
             method=="studio.entitiesDelete" || method=="studio.entitiesDuplicate" || method=="studio.assetPlace" || method=="studio.entityFlags" || method=="studio.rulesSet")) {
            [self sendStudioEvent:"document.changed" message:""];
        }
    } catch (const std::exception& e) { NSLog(@"AIVERSE bridge exception: %s",e.what()); }
}
- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)app { (void)app; return YES; }
- (void)applicationWillTerminate:(NSNotification*)note {
    (void)note;
    [timer_ invalidate];
    timer_=nil;
    game_.reset(); // hosted client/device retire before native surface
    preview_.reset(); // release GPU resources before CAMetalLayer
    [web_.configuration.userContentController removeScriptMessageHandlerForName:@"aiverse"];
}
@end

// PlatformMac.mm (aiverse_runtime) owns main() and forwards to aiverseMain().
int aiverseMain(int argc,char** argv) {
    @autoreleasepool {
        std::string project;
        bool dev=false, composite=false;
        for(int i=1;i<argc;++i) {
            std::string arg(argv[i]);
            if(arg=="--dev")dev=true;
            else if(arg=="--html-composite")composite=true;
            else if(arg=="--project"&&i+1<argc)project=argv[++i];
            else { std::fprintf(stderr,"Usage: AiverseStudioHTML5 [--dev] [--html-composite] [--project /path/to/project]\n"); return 2; }
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
            StudioHost* host=[[StudioHost alloc] initWithProject:project dev:dev composite:composite];
            [NSApp setDelegate:host];
            [NSApp run];
        } catch(const std::exception& e) {
            std::fprintf(stderr,"Studio startup failed: %s\n",e.what()); return 1;
        }
    }
    return 0;
}
