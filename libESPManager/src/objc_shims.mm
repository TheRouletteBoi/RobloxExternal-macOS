#import "objc_shims.h"
#import <Cocoa/Cocoa.h>
#import <ScreenCaptureKit/ScreenCaptureKit.h>
#import <dispatch/dispatch.h>

@interface ESPTextField : NSTextField
@end

@implementation ESPTextField
- (NSView *)hitTest:(NSPoint)point {
    return nil;
}
@end

@interface ESPCircleView : NSView
@property (nonatomic, assign) float radius;
@property (nonatomic, strong) NSColor* strokeColor;
@property (nonatomic, assign) float borderWidth;
@property (nonatomic, assign) BOOL filled;
@end

@implementation ESPCircleView
- (instancetype)initWithFrame:(NSRect)frameRect {
    self = [super initWithFrame:frameRect];
    if (self) {
        self.wantsLayer = YES;
        self.layer.backgroundColor = [[NSColor clearColor] CGColor];
        _radius = 50.0f;
        _strokeColor = [NSColor blueColor];
        _borderWidth = 2.0f;
        _filled = NO;
    }
    return self;
}

- (NSView *)hitTest:(NSPoint)point {
    return nil;
}

- (void)drawRect:(NSRect)dirtyRect {
    [super drawRect:dirtyRect];

    NSBezierPath* path = [NSBezierPath bezierPathWithOvalInRect:self.bounds];

    if (self.filled) {
        [self.strokeColor setFill];
        [path fill];
    }

    [self.strokeColor setStroke];
    path.lineWidth = self.borderWidth;
    [path stroke];
}
@end

@interface ScreenCaptureHelper : NSObject
+ (void)captureWindow:(CGWindowID)windowID callback:(CaptureCallback)callback context:(void*)context;
@end

@implementation ScreenCaptureHelper

+ (void)captureWindow:(CGWindowID)windowID callback:(CaptureCallback)callback context:(void*)context {
    if (@available(macOS 12.3, *)) {
        [SCShareableContent getShareableContentWithCompletionHandler:^(SCShareableContent * _Nullable content, NSError * _Nullable error) {
            if (error || !content) {
                callback(nullptr, 0, context);
                return;
            }

            SCWindow* targetWindow = nil;
            for (SCWindow* window in content.windows) {
                if (window.windowID == windowID) {
                    targetWindow = window;
                    break;
                }
            }

            if (!targetWindow) {
                callback(nullptr, 0, context);
                return;
            }

            SCContentFilter* filter = [[SCContentFilter alloc] initWithDesktopIndependentWindow:targetWindow];
            SCStreamConfiguration* config = [[SCStreamConfiguration alloc] init];
            config.width = (size_t)targetWindow.frame.size.width * 2;
            config.height = (size_t)targetWindow.frame.size.height * 2;
            config.minimumFrameInterval = CMTimeMake(1, 1);
            config.captureResolution = SCCaptureResolutionAutomatic;
            config.showsCursor = NO;

            [SCScreenshotManager captureImageWithFilter:filter
                                          configuration:config
                                      completionHandler:^(CGImageRef _Nullable image, NSError * _Nullable error) {
                if (error || !image) {
                    callback(nullptr, 0, context);
                    return;
                }

                NSBitmapImageRep* bitmapRep = [[NSBitmapImageRep alloc] initWithCGImage:image];
                NSData* pngData = [bitmapRep representationUsingType:NSBitmapImageFileTypePNG properties:@{}];

                if (pngData) {
                    callback((const uint8_t*)pngData.bytes, pngData.length, context);
                } else {
                    callback(nullptr, 0, context);
                }
            }];
        }];
    } else {
        callback(nullptr, 0, context);
    }
}

@end

static NSView* g_espView = nil;
static NSMutableArray<ESPTextField*>* g_boxPool = nil;
static NSMutableArray<ESPCircleView*>* g_circlePool = nil;

extern "C" {

static void ensure_app_ready_impl(dispatch_block_t block, int attempts) {
    if (NSApp && NSApp.windows.count > 0 && [NSApp mainMenu]) {
        block();
        return;
    }

    if (attempts >= 50) {
        return;
    }

    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(0.1 * NSEC_PER_SEC)),
                  dispatch_get_main_queue(), ^{
        ensure_app_ready_impl(block, attempts + 1);
    });
}

static void ensure_app_ready(dispatch_block_t block) {
    dispatch_async(dispatch_get_main_queue(), ^{
        if (NSApp && NSApp.windows.count > 0 && [NSApp mainMenu]) {
            block();
            return;
        }

        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(0.1 * NSEC_PER_SEC)),
                      dispatch_get_main_queue(), ^{
            ensure_app_ready_impl(block, 0);
        });
    });
}

bool shim_get_window_info(WindowInfo* out_info) {
    if (!out_info) return false;

    __block bool success = false;

    dispatch_block_t block = ^{
        if (!NSApp || NSApp.windows.count == 0) return;

        NSWindow* window = NSApp.windows[0];
        NSView* contentView = window.contentView;

        out_info->width = contentView.frame.size.width;
        out_info->height = contentView.frame.size.height;
        out_info->x = window.frame.origin.x;
        out_info->y = window.frame.origin.y;
        out_info->titlebar_height = window.frame.size.height - [window contentRectForFrameRect:window.frame].size.height;
        out_info->window_number = window.windowNumber;
        out_info->is_active = NSApp.isActive;

        NSString* windowTitle = window.title;
        if (windowTitle) {
            const char* utf8 = [windowTitle UTF8String];
            if (utf8) {
                strncpy(out_info->title.data(), utf8, out_info->title.size() - 1);
                out_info->title[out_info->title.size() - 1] = '\0';
            } else {
                out_info->title[0] = '\0';
            }
        } else {
            out_info->title[0] = '\0';
        }

        success = true;
    };

    if ([NSThread isMainThread]) {
        block();
    } else {
        dispatch_sync(dispatch_get_main_queue(), block);
    }

    return success;
}

void shim_set_dock_badge(const char* text) {
    if (!text) return;

    NSString* nsText = [NSString stringWithUTF8String:text];

    dispatch_block_t block = ^{
        [NSApp.dockTile setBadgeLabel:nsText];
    };

    if ([NSThread isMainThread]) {
        block();
    } else {
        dispatch_async(dispatch_get_main_queue(), block);
    }
}

static float g_cachedTitlebarHeight = 0;
static float g_cachedContentHeight = 0;

float shim_get_titlebar_height(void) {
    if (g_cachedTitlebarHeight > 0) return g_cachedTitlebarHeight;

    __block float height = 0;
    dispatch_block_t block = ^{
        if (NSApp && NSApp.windows.count > 0) {
            NSWindow* w = NSApp.windows[0];
            height = w.frame.size.height - [w contentRectForFrameRect:w.frame].size.height;
        }
    };

    if ([NSThread isMainThread]) {
        block();
    } else {
        dispatch_sync(dispatch_get_main_queue(), block);
    }

    g_cachedTitlebarHeight = height;
    return height;
}

float shim_get_content_height(void) {
    __block float height = 0;
    dispatch_block_t block = ^{
        if (NSApp && NSApp.windows.count > 0) {
            height = NSApp.windows[0].contentView.frame.size.height;
        }
    };

    if ([NSThread isMainThread]) {
        block();
    } else {
        dispatch_sync(dispatch_get_main_queue(), block);
    }
    return height;
}

void shim_move_mouse(float x, float y) {
    dispatch_block_t block = ^{
        if (!NSApp || NSApp.windows.count == 0) return;

        NSWindow* window = NSApp.windows[0];
        NSScreen* screen = [NSScreen mainScreen];

        CGPoint point;
        point.x = window.frame.origin.x + x;
        point.y = screen.frame.size.height - window.frame.origin.y - window.frame.size.height + y;

        CGWarpMouseCursorPosition(point);
    };

    if ([NSThread isMainThread]) {
        block();
    } else {
        dispatch_async(dispatch_get_main_queue(), block);
    }
}

void shim_send_key_down(int keycode, const char* characters) {
    NSString* chars = characters ? [NSString stringWithUTF8String:characters] : @"";

    dispatch_block_t block = ^{
        if (!NSApp || NSApp.windows.count == 0) return;
        NSWindow* window = NSApp.windows[0];

        NSEvent* event = [NSEvent keyEventWithType:NSEventTypeKeyDown
                                          location:CGPointZero
                                     modifierFlags:0
                                         timestamp:0
                                      windowNumber:window.windowNumber
                                           context:nil
                                        characters:chars
                       charactersIgnoringModifiers:chars
                                         isARepeat:NO
                                           keyCode:keycode];
        [window sendEvent:event];
    };

    if ([NSThread isMainThread]) {
        block();
    } else {
        dispatch_async(dispatch_get_main_queue(), block);
    }
}

void shim_send_key_up(int keycode, const char* characters) {
    NSString* chars = characters ? [NSString stringWithUTF8String:characters] : @"";

    dispatch_block_t block = ^{
        if (!NSApp || NSApp.windows.count == 0) return;
        NSWindow* window = NSApp.windows[0];

        NSEvent* event = [NSEvent keyEventWithType:NSEventTypeKeyUp
                                          location:CGPointZero
                                     modifierFlags:0
                                         timestamp:0
                                      windowNumber:window.windowNumber
                                           context:nil
                                        characters:chars
                       charactersIgnoringModifiers:chars
                                         isARepeat:NO
                                           keyCode:keycode];
        [window sendEvent:event];
    };

    if ([NSThread isMainThread]) {
        block();
    } else {
        dispatch_async(dispatch_get_main_queue(), block);
    }
}

void shim_send_left_mouse_down(float x, float y) {
    dispatch_block_t block = ^{
        if (!NSApp || NSApp.windows.count == 0) return;
        NSWindow* window = NSApp.windows[0];

        CGPoint point = CGPointMake(x, y);
        NSEvent* event = [NSEvent mouseEventWithType:NSEventTypeLeftMouseDown
                                            location:point
                                       modifierFlags:0
                                           timestamp:0
                                        windowNumber:window.windowNumber
                                             context:nil
                                         eventNumber:0
                                          clickCount:1
                                            pressure:1];
        [window sendEvent:event];
    };

    if ([NSThread isMainThread]) {
        block();
    } else {
        dispatch_async(dispatch_get_main_queue(), block);
    }
}

void shim_send_left_mouse_up(float x, float y) {
    dispatch_block_t block = ^{
        if (!NSApp || NSApp.windows.count == 0) return;
        NSWindow* window = NSApp.windows[0];

        CGPoint point = CGPointMake(x, y);
        NSEvent* event = [NSEvent mouseEventWithType:NSEventTypeLeftMouseUp
                                            location:point
                                       modifierFlags:0
                                           timestamp:0
                                        windowNumber:window.windowNumber
                                             context:nil
                                         eventNumber:0
                                          clickCount:1
                                            pressure:1];
        [window sendEvent:event];
    };

    if ([NSThread isMainThread]) {
        block();
    } else {
        dispatch_async(dispatch_get_main_queue(), block);
    }
}

void shim_insert_text(const char* text) {
    if (!text) return;
    NSString* nsText = [NSString stringWithUTF8String:text];

    dispatch_block_t block = ^{
        if (!NSApp || NSApp.windows.count == 0) return;
        NSWindow* window = NSApp.windows[0];
        id firstResponder = [window firstResponder];
        if ([firstResponder respondsToSelector:@selector(insertText:)]) {
            [firstResponder insertText:nsText];
        }
    };

    if ([NSThread isMainThread]) {
        block();
    } else {
        dispatch_async(dispatch_get_main_queue(), block);
    }
}

static void* g_eventContext = nullptr;
static MouseMoveCallback g_mouseMoveCallback = nullptr;
static MouseButtonCallback g_leftMouseCallback = nullptr;
static MouseButtonCallback g_rightMouseCallback = nullptr;
static KeyCallback g_keyCallback = nullptr;

void shim_setup_event_monitors(
    MouseMoveCallback mouse_move,
    MouseButtonCallback left_mouse,
    MouseButtonCallback right_mouse,
    KeyCallback key_callback,
    void* context)
{
    g_eventContext = context;
    g_mouseMoveCallback = mouse_move;
    g_leftMouseCallback = left_mouse;
    g_rightMouseCallback = right_mouse;
    g_keyCallback = key_callback;

    [NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskMouseMoved
                                          handler:^NSEvent * _Nullable(NSEvent * event) {
        if (g_mouseMoveCallback) {
            g_mouseMoveCallback(event.locationInWindow.x, event.locationInWindow.y, g_eventContext);
        }
        return event;
    }];

    [NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskLeftMouseDown
                                          handler:^NSEvent * _Nullable(NSEvent * event) {
        if (g_leftMouseCallback) {
            g_leftMouseCallback(true, g_eventContext);
        }
        return event;
    }];

    [NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskLeftMouseUp
                                          handler:^NSEvent * _Nullable(NSEvent * event) {
        if (g_leftMouseCallback) {
            g_leftMouseCallback(false, g_eventContext);
        }
        return event;
    }];

    [NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskRightMouseDown
                                          handler:^NSEvent * _Nullable(NSEvent * event) {
        if (g_rightMouseCallback) {
            g_rightMouseCallback(true, g_eventContext);
        }
        return event;
    }];

    [NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskRightMouseUp
                                          handler:^NSEvent * _Nullable(NSEvent * event) {
        if (g_rightMouseCallback) {
            g_rightMouseCallback(false, g_eventContext);
        }
        return event;
    }];

    [NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskKeyDown
                                          handler:^NSEvent * _Nullable(NSEvent * event) {
        if (g_keyCallback) {
            const char* chars = [event.characters UTF8String];
            g_keyCallback(event.keyCode, chars ? chars : "", true, g_eventContext);
        }
        return event;
    }];

    [NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskKeyUp
                                          handler:^NSEvent * _Nullable(NSEvent * event) {
        if (g_keyCallback) {
            const char* chars = [event.characters UTF8String];
            g_keyCallback(event.keyCode, chars ? chars : "", false, g_eventContext);
        }
        return event;
    }];
}

void shim_capture_window(int64_t window_id, CaptureCallback callback, void* context) {
    [ScreenCaptureHelper captureWindow:(CGWindowID)window_id callback:callback context:context];
}

bool shim_init_esp_view(void) {
    __block bool success = false;

    dispatch_block_t block = ^{
        if (!NSApp || NSApp.windows.count == 0) return;
        if (g_espView != nil) { success = true; return; }

        NSWindow* window = NSApp.windows[0];
        NSView* contentView = window.contentView;
        if (contentView.subviews.count == 0) return;

        NSView* ogreView = contentView.subviews[0];

        g_espView = [[NSView alloc] init];
        g_espView.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
        g_espView.frame = ogreView.frame;
        [ogreView addSubview:g_espView];

        // Initialize empty pools (they grow dynamically)
        g_boxPool = [[NSMutableArray alloc] init];
        g_circlePool = [[NSMutableArray alloc] init];

        success = true;
    };

    if ([NSThread isMainThread]) {
        block();
    } else {
        dispatch_sync(dispatch_get_main_queue(), block);
    }

    return success;
}

ESPViewHandle shim_get_esp_box(int index) {
    if (index < 0 || index >= MAX_ESP_COUNT || !g_boxPool) return nullptr;
    if (index >= g_boxPool.count) return nullptr;
    return (__bridge void*)g_boxPool[index];
}

void shim_update_esp_box(
    ESPViewHandle handle,
    float x, float y, float width, float height,
    float r, float g, float b, float a,
    float border_width,
    const char* text,
    bool hidden)
{
    if (!handle) return;

    ESPTextField* box = (__bridge ESPTextField*)handle;
    NSString* nsText = text ? [NSString stringWithUTF8String:text] : @"";

    dispatch_block_t block = ^{
        box.frame = NSMakeRect(x, y, width, height);
        box.hidden = hidden;
        box.stringValue = nsText;
        box.layer.borderWidth = border_width;

        CGColorRef color = CGColorCreateSRGB(r, g, b, a);
        box.layer.borderColor = color;
        box.textColor = [NSColor colorWithRed:r green:g blue:b alpha:a];
        CGColorRelease(color);
    };

    if ([NSThread isMainThread]) {
        block();
    } else {
        dispatch_async(dispatch_get_main_queue(), block);
    }
}

void shim_set_esp_font(const char* font_name, int size) {
    if (!g_boxPool || !font_name) return;

    NSString* nsFontName = [NSString stringWithUTF8String:font_name];
    NSFont* font = [NSFont fontWithName:nsFontName size:size];
    if (!font) font = [NSFont systemFontOfSize:size];

    dispatch_block_t block = ^{
        for (ESPTextField* box in g_boxPool) {
            [box setFont:font];
        }
    };

    if ([NSThread isMainThread]) {
        block();
    } else {
        dispatch_async(dispatch_get_main_queue(), block);
    }
}

int shim_get_esp_box_count(void) {
    return g_boxPool ? (int)g_boxPool.count : 0;
}

void shim_dispatch_main_sync(MainThreadBlock block, void* context) {
    if ([NSThread isMainThread]) {
        block(context);
    } else {
        dispatch_sync(dispatch_get_main_queue(), ^{
            block(context);
        });
    }
}

void shim_dispatch_main_async(MainThreadBlock block, void* context) {
    dispatch_async(dispatch_get_main_queue(), ^{
        block(context);
    });
}

@interface MenuItemTarget : NSObject
@property (nonatomic, assign) MenuCallback callback;
@property (nonatomic, assign) void* context;
- (void)menuItemClicked:(id)sender;
@end

@implementation MenuItemTarget
- (void)menuItemClicked:(id)sender {
    if (self.callback) {
        self.callback(self.context);
    }
}
@end

static NSMutableArray<MenuItemTarget*>* g_menuTargets = nil;

void shim_set_window_title(const char* title) {
    if (!title) return;

    NSString* nsTitle = [NSString stringWithUTF8String:title];

    ensure_app_ready(^{
        if (NSApp && NSApp.windows.count > 0) {
            NSApp.windows[0].title = nsTitle;
        }
    });
}

void shim_add_menu_item(const char* menu_title, const char* item_title, MenuCallback callback, void* context) {
    if (!menu_title || !item_title) return;

    NSString* nsMenuTitle = [NSString stringWithUTF8String:menu_title];
    NSString* nsItemTitle = [NSString stringWithUTF8String:item_title];

    ensure_app_ready(^{
        if (!g_menuTargets) {
            g_menuTargets = [[NSMutableArray alloc] init];
        }

        NSMenu* mainMenu = [NSApp mainMenu];
        if (!mainMenu) return;

        NSMenuItem* menuRoot = [mainMenu itemWithTitle:nsMenuTitle];
        NSMenu* submenu = nil;

        if (!menuRoot) {
            menuRoot = [[NSMenuItem alloc] initWithTitle:nsMenuTitle action:nil keyEquivalent:@""];
            submenu = [[NSMenu alloc] initWithTitle:nsMenuTitle];
            [mainMenu addItem:menuRoot];
            [mainMenu setSubmenu:submenu forItem:menuRoot];
        } else {
            submenu = menuRoot.submenu;
        }

        MenuItemTarget* target = [[MenuItemTarget alloc] init];
        target.callback = callback;
        target.context = context;
        [g_menuTargets addObject:target];

        NSMenuItem* item = [[NSMenuItem alloc] initWithTitle:nsItemTitle
                                                      action:@selector(menuItemClicked:)
                                               keyEquivalent:@""];
        item.target = target;
        [submenu addItem:item];
    });
}

void shim_render_esp_shapes(const ESPShape* shapes, uint32_t count, const char* font_name, int font_size) {
    if (!g_espView)
        return;

    dispatch_async(dispatch_get_main_queue(), ^{
        uint32_t box_idx = 0;
        uint32_t circle_idx = 0;

        if (shapes && count > 0) {
            for (uint32_t i = 0; i < count; i++) {
                const ESPShape& shape = shapes[i];

                switch (shape.type) {
                    case ESPShapeType::Box: {
                        while (box_idx >= g_boxPool.count) {
                            ESPTextField* box = [[ESPTextField alloc] init];
                            box.drawsBackground = NO;
                            box.editable = NO;
                            box.selectable = NO;
                            box.alignment = NSTextAlignmentCenter;
                            box.wantsLayer = YES;
                            box.layer.cornerRadius = 3;
                            box.bezeled = NO;
                            box.enabled = NO;
                            [box setFont:[NSFont fontWithName:[NSString stringWithUTF8String:font_name] size:font_size]];
                            [g_espView addSubview:box];
                            [g_boxPool addObject:box];
                        }

                        ESPTextField* box = g_boxPool[box_idx++];
                        box.hidden = NO;
                        box.frame = NSMakeRect(shape.box.frame.x, shape.box.frame.y,
                                              shape.box.frame.width, shape.box.frame.height);
                        box.stringValue = [NSString stringWithUTF8String:shape.box.text];
                        box.layer.borderWidth = shape.box.border_width;

                        CGColorRef color = CGColorCreateSRGB(
                            shape.box.color.r, shape.box.color.g,
                            shape.box.color.b, shape.box.color.a
                        );
                        box.layer.borderColor = color;
                        box.textColor = [NSColor colorWithRed:shape.box.color.r
                                                        green:shape.box.color.g
                                                         blue:shape.box.color.b
                                                        alpha:shape.box.color.a];
                        CGColorRelease(color);
                        break;
                    }

                    case ESPShapeType::Circle: {
                        while (circle_idx >= g_circlePool.count) {
                            ESPCircleView* circle = [[ESPCircleView alloc] initWithFrame:NSMakeRect(0, 0, 100, 100)];
                            [g_espView addSubview:circle];
                            [g_circlePool addObject:circle];
                        }

                        ESPCircleView* circle = g_circlePool[circle_idx++];
                        circle.hidden = NO;

                        float diameter = shape.circle.radius * 2.0f;
                        circle.frame = NSMakeRect(
                            shape.circle.center_x - shape.circle.radius,
                            shape.circle.center_y - shape.circle.radius,
                            diameter, diameter
                        );

                        circle.radius = shape.circle.radius;
                        circle.strokeColor = [NSColor colorWithRed:shape.circle.color.r
                                                             green:shape.circle.color.g
                                                              blue:shape.circle.color.b
                                                             alpha:shape.circle.color.a];
                        circle.borderWidth = shape.circle.border_width;
                        circle.filled = shape.circle.filled;
                        [circle setNeedsDisplay:YES];
                        break;
                    }
                }
            }
        }

        for (uint32_t i = box_idx; i < g_boxPool.count; i++) {
            g_boxPool[i].hidden = YES;
        }
        for (uint32_t i = circle_idx; i < g_circlePool.count; i++) {
            g_circlePool[i].hidden = YES;
        }
    });
}

} // extern "C"
