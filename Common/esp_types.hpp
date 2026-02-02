#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <semaphore.h>

constexpr int MAX_ESP_COUNT = 800;
constexpr int MAX_ESP_TEXT_LENGTH = 200;
constexpr int MAX_INPUT_COUNT = 200;
constexpr int MAX_FUNCTION_COUNT = 200;

enum class InputType : int {
    MOUSE_MOVE = 1,
    KEY_DOWN = 2,
    KEY_UP = 3,
    LEFT_MOUSE_DOWN = 4,
    LEFT_MOUSE_UP = 5,
    RIGHT_MOUSE_DOWN = 6,
    RIGHT_MOUSE_UP = 7,
    FIRST_RESPONDER_TEXT = 8
};

enum class FunctionAsyncType : int {
    MAIN = 1,
    SEPARATE = 2,
    CURRENT = 3,
    SYNC_MAIN = 4
};

struct ESPRect {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    
    ESPRect() = default;
    ESPRect(float x_, float y_, float w, float h) : x(x_), y(y_), width(w), height(h) {}
};

struct ESPColor {
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
    float a = 1.0f;
    
    ESPColor() = default;
    ESPColor(float r_, float g_, float b_, float a_ = 1.0f) : r(r_), g(g_), b(b_), a(a_) {}
};

enum class ESPShapeType : uint8_t {
    Box = 0,
    Circle = 1,
    Line = 2,
    Text = 3
};

struct ESPBoxData {
    ESPRect frame;
    ESPColor color;
    float border_width = 2.0f;
    char text[MAX_ESP_TEXT_LENGTH] = {0};
};

struct ESPCircleData {
    float center_x;
    float center_y;
    float radius;
    ESPColor color;
    float border_width;
    bool filled;
};

struct ESPShape {
    ESPShapeType type;

    union {
        ESPBoxData box;
        ESPCircleData circle;
    };
};

struct ESPFrame {
    uint32_t count = 0;
    ESPShape shapes[MAX_ESP_COUNT];
};

struct InputCommand {
    InputType type = InputType::MOUSE_MOVE;
    int keycode = 0;
    float x = 0.0f;
    float y = 0.0f;
    char characters[8] = {0};
    char text[200] = {0};
};

struct FunctionCall {
    int type = 0;
    uint64_t address = 0;
    char arguments[48] = {0};  // 8 * 6 bytes
    char return_bytes[8] = {0};
    FunctionAsyncType async_type = FunctionAsyncType::CURRENT;
};

struct ESPState {
    float window_w = 0.0f;
    float window_h = 0.0f;
    float window_x = 0.0f;
    float window_y = 0.0f;
    float titlebar_height = 0.0f;
    int64_t window_number = 0;
    std::array<char, 256> title;

    float mouse_x = 0.0f;
    float mouse_y = 0.0f;
    std::atomic<bool> left_mouse_down{false};
    std::atomic<bool> right_mouse_down{false};

    uint8_t keys_down[256] = {0};
    uint8_t key_codes_down[256] = {0};
    uint8_t keys_down_once[256] = {0};
    uint8_t key_codes_down_once[256] = {0};

    std::atomic<bool> app_is_active{false};
    std::atomic<bool> dylib_ready{false};
};

struct ESPCommands {
    std::atomic<uint32_t> input_head{0};  // Written by injector
    std::atomic<uint32_t> input_tail{0};  // Written by dylib
    InputCommand input_queue[MAX_INPUT_COUNT];

    std::atomic<uint32_t> func_head{0};
    std::atomic<uint32_t> func_tail{0};
    FunctionCall func_queue[MAX_FUNCTION_COUNT];

    std::atomic<bool> capture_requested{false};
    std::atomic<bool> capture_complete{false};
    
    // Cross process semaphore counters (atomic integers)
    // These work across processes via shared memory
    // Dylib waits on these, injector posts to them
    std::atomic<uint32_t> input_signal{0};
    std::atomic<uint32_t> capture_signal{0};
};

struct ESPRenderData {
    std::atomic<bool> enabled{false};
    std::atomic<uint32_t> active_frame{0};  // 0 or 1
    ESPFrame frames[2];

    char font_name[100] = "Helvetica";
    int font_size = 12;

    // Get the frame to write to (opposite of active)
    uint32_t write_index() const { return 1 - active_frame.load(); }

    // Get the frame to read from
    uint32_t read_index() const { return active_frame.load(); }

    void flip() { active_frame.store(write_index()); }
};

struct ESPBulkData {
    uint64_t capture_data_length = 0;
    uint8_t capture_data[4000000] = {0};  // 4MB

    uint8_t custom_data[4000000] = {0};   // 4MB

    char dock_badge_text[200] = "*";
};

struct ESPConfig {
    uint32_t esp_fps = 60;           // Target FPS for ESP updates
    uint32_t refresh_interval_sec = 5; // Auto-hide interval
    char window_title_text[200] = "Roblox";
};

struct SharedMemoryLayout {
    uint32_t magic = 0xE5B12345;
    uint32_t version = 1;

    ESPState state;
    ESPCommands commands;
    ESPRenderData render;
    ESPBulkData bulk;
    ESPConfig config;
};

namespace RingBuffer {
    template<uint32_t N>
    inline bool is_full(uint32_t head, uint32_t tail) {
        return ((head + 1) % N) == tail;
    }

    inline bool is_empty(uint32_t head, uint32_t tail) {
        return head == tail;
    }

    template<uint32_t N>
    inline uint32_t advance(uint32_t index) {
        return (index + 1) % N;
    }
}
