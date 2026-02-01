#pragma once

#include "../Common/esp_types.hpp"

#include <functional>
#include <cstdint>

#ifdef __OBJC__
#import <Cocoa/Cocoa.h>
#import <ScreenCaptureKit/ScreenCaptureKit.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float width;
    float height;
    float x;
    float y;
    int64_t window_number;
    bool is_active;
    std::array<char, 256> title;
} WindowInfo;

bool shim_get_window_info(WindowInfo* out_info);

void shim_set_window_title(const char* title);

void shim_set_dock_badge(const char* text);

void shim_move_mouse(float x, float y);
void shim_send_key_down(int keycode, const char* characters);
void shim_send_key_up(int keycode, const char* characters);
void shim_send_left_mouse_down(float x, float y);
void shim_send_left_mouse_up(float x, float y);
void shim_insert_text(const char* text);

float shim_get_titlebar_height(void);
float shim_get_content_height(void);

typedef void (*MouseMoveCallback)(float x, float y, void* ctx);
typedef void (*MouseButtonCallback)(bool down, void* ctx);
typedef void (*KeyCallback)(int keycode, const char* chars, bool down, void* ctx);

void shim_setup_event_monitors(
    MouseMoveCallback mouse_move,
    MouseButtonCallback left_mouse,
    MouseButtonCallback right_mouse,
    KeyCallback key_callback,
    void* context
);

typedef void (*CaptureCallback)(const uint8_t* data, size_t length, void* ctx);

void shim_capture_window(int64_t window_id, CaptureCallback callback, void* context);

typedef void* ESPViewHandle;

bool shim_init_esp_view(void);

ESPViewHandle shim_get_esp_box(int index);

void shim_update_esp_box(
    ESPViewHandle handle,
    float x, float y, float width, float height,
    float r, float g, float b, float a,
    float border_width,
    const char* text,
    bool hidden
);

void shim_set_esp_font(const char* font_name, int size);

// Get number of ESP boxes allocated
int shim_get_esp_box_count(void);

typedef void (*MainThreadBlock)(void* ctx);

void shim_dispatch_main_sync(MainThreadBlock block, void* context);

void shim_dispatch_main_async(MainThreadBlock block, void* context);

typedef void (*MenuCallback)(void* ctx);

void shim_add_menu_item(const char* menu_title, const char* item_title, MenuCallback callback, void* context);

#ifdef __cplusplus
}
#endif
