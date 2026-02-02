#pragma once

#include "esp_ipc.hpp"

#include <string>
#include <vector>
#include <optional>

class ESPController {
public:
    explicit ESPController(const std::string& shared_mem_path);
    ~ESPController();

    ESPController(const ESPController&) = delete;
    ESPController& operator=(const ESPController&) = delete;
    ESPController(ESPController&&) = default;
    ESPController& operator=(ESPController&&) = default;

    bool is_connected() const;
    bool wait_for_dylib(uint32_t timeout_ms = 5000);

    void enable_esp();
    void disable_esp();
    bool is_esp_enabled() const;
    
    // Begin a new frame - call before adding shapes
    void begin_frame();

    // Add shapes (they go into the buffer in order)
    void add_box(float x, float y, float width, float height,
                 const ESPColor& color, const std::string& text = "",
                 float border_width = 2.0f);

    void add_circle(float center_x, float center_y, float radius,
                    const ESPColor& color, float border_width = 2.0f,
                    bool filled = false);

    // End frame and flip buffers - shapes become visible
    void end_frame();

    void clear_esp();

    bool move_mouse(float x, float y);
    bool click_mouse(float x, float y, bool left = true);
    bool press_key(int keycode, const std::string& characters = "");
    bool release_key(int keycode, const std::string& characters = "");
    bool type_text(const std::string& text);

    bool is_key_down(uint8_t key) const;
    bool was_key_pressed(uint8_t key);
    bool is_key_code_down(uint8_t keycode) const;
    bool was_key_code_pressed(uint8_t keycode);

    bool is_left_mouse_down_raw() const;
    bool is_right_mouse_down_raw() const;
    // mouse state to avoid titlebar clicks
    bool is_left_mouse_down() const;
    bool is_right_mouse_down() const;
    bool is_mouse_in_content() const;

    float mouse_x() const;
    float mouse_y() const;

    float window_width() const;
    float window_height() const;
    float window_x() const;
    float window_y() const;
    int64_t window_number() const;
    bool is_app_active() const;
    std::array<char, 256> window_title() const;
    float titlebar_height() const;

    bool capture_window(const std::string& output_path, uint32_t timeout_ms = 5000);
    std::vector<uint8_t> capture_window_to_memory(uint32_t timeout_ms = 5000);

    void set_esp_fps(uint32_t fps);
    void set_font(const std::string& name, int size);
    void set_dock_badge(const std::string& text);
    void set_window_title(const std::string& text);

    SharedMemoryLayout* raw() { return m_shm.get(); }
    const SharedMemoryLayout* raw() const { return m_shm.get(); }

private:
    SharedMemory m_shm;

    // Queue an input command (returns false if queue full)
    bool queue_input(const InputCommand& cmd);
};

inline ESPController::ESPController(const std::string& shared_mem_path)
    : m_shm(SharedMemory::open(shared_mem_path))
{
}

inline ESPController::~ESPController() {
    if (is_connected()) {
        clear_esp();
        m_shm.render().enabled = false;
    }
}

inline bool ESPController::is_connected() const {
    return m_shm.get() && m_shm->magic == 0xE5B12345;
}

inline bool ESPController::wait_for_dylib(uint32_t timeout_ms) {
    uint32_t elapsed = 0;
    while (elapsed < timeout_ms) {
        if (m_shm.state().dylib_ready.load()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        elapsed += 10;
    }
    return false;
}

inline void ESPController::enable_esp() {
    m_shm.render().enabled = true;
}

inline void ESPController::disable_esp() {
    clear_esp();
    m_shm.render().enabled = false;
}

inline bool ESPController::is_esp_enabled() const {
    return m_shm.render().enabled.load();
}

inline void ESPController::begin_frame() {
    // Clear the write buffer
    auto& frame = m_shm.render().frames[m_shm.render().write_index()];
    frame.count = 0;
}

inline void ESPController::add_box(float x, float y, float width, float height,
                                   const ESPColor& color, const std::string& text,
                                   float border_width)
{
    auto& frame = m_shm.render().frames[m_shm.render().write_index()];
    if (frame.count >= MAX_ESP_COUNT) return;

    auto& shape = frame.shapes[frame.count++];
    shape.type = ESPShapeType::Box;
    shape.box.frame = ESPRect(x, y, width, height);
    shape.box.color = color;
    shape.box.border_width = border_width;
    std::strncpy(shape.box.text, text.c_str(), MAX_ESP_TEXT_LENGTH - 1);
    shape.box.text[MAX_ESP_TEXT_LENGTH - 1] = '\0';
}

inline void ESPController::add_circle(float center_x, float center_y, float radius,
                                      const ESPColor& color, float border_width, bool filled)
{
    auto& frame = m_shm.render().frames[m_shm.render().write_index()];
    if (frame.count >= MAX_ESP_COUNT) return;

    auto& shape = frame.shapes[frame.count++];
    shape.type = ESPShapeType::Circle;
    shape.circle.center_x = center_x;
    shape.circle.center_y = center_y;
    shape.circle.radius = radius;
    shape.circle.color = color;
    shape.circle.border_width = border_width;
    shape.circle.filled = filled;
}

inline void ESPController::end_frame() {
    m_shm.render().flip();
}

inline void ESPController::clear_esp() {
    auto& frame = m_shm.render().frames[m_shm.render().write_index()];
    frame.count = 0;
    m_shm.render().flip();
}

inline bool ESPController::queue_input(const InputCommand& cmd) {
    auto& cmds = m_shm.commands();

    uint32_t head = cmds.input_head.load(std::memory_order_relaxed);
    uint32_t next_head = RingBuffer::advance<MAX_INPUT_COUNT>(head);

    // Check if full
    if (next_head == cmds.input_tail.load(std::memory_order_acquire)) {
        return false; // Queue full
    }

    // Write command
    cmds.input_queue[head] = cmd;

    // Publish
    cmds.input_head.store(next_head, std::memory_order_release);

    // Signal the dylib via shared memory atomic
    SharedSemaphore::post(&cmds.input_signal);

    return true;
}

inline bool ESPController::move_mouse(float x, float y) {
    InputCommand cmd{};
    cmd.type = InputType::MOUSE_MOVE;
    cmd.x = x;
    cmd.y = y;
    return queue_input(cmd);
}

inline bool ESPController::click_mouse(float x, float y, bool left) {
    InputCommand down{};
    down.type = left ? InputType::LEFT_MOUSE_DOWN : InputType::RIGHT_MOUSE_DOWN;
    down.x = x;
    down.y = y;

    InputCommand up{};
    up.type = left ? InputType::LEFT_MOUSE_UP : InputType::RIGHT_MOUSE_UP;
    up.x = x;
    up.y = y;

    return queue_input(down) && queue_input(up);
}

inline bool ESPController::press_key(int keycode, const std::string& characters) {
    InputCommand cmd{};
    cmd.type = InputType::KEY_DOWN;
    cmd.keycode = keycode;
    std::strncpy(cmd.characters, characters.c_str(), sizeof(cmd.characters) - 1);
    return queue_input(cmd);
}

inline bool ESPController::release_key(int keycode, const std::string& characters) {
    InputCommand cmd{};
    cmd.type = InputType::KEY_UP;
    cmd.keycode = keycode;
    std::strncpy(cmd.characters, characters.c_str(), sizeof(cmd.characters) - 1);
    return queue_input(cmd);
}

inline bool ESPController::type_text(const std::string& text) {
    InputCommand cmd{};
    cmd.type = InputType::FIRST_RESPONDER_TEXT;
    std::strncpy(cmd.text, text.c_str(), sizeof(cmd.text) - 1);
    return queue_input(cmd);
}

inline bool ESPController::is_key_down(uint8_t key) const {
    return m_shm.state().keys_down[key];
}

inline bool ESPController::was_key_pressed(uint8_t key) {
    auto& state = m_shm.state();
    if (state.keys_down_once[key]) {
        state.keys_down_once[key] = false;
        return true;
    }
    return false;
}

inline bool ESPController::is_key_code_down(uint8_t keycode) const {
    return m_shm.state().key_codes_down[keycode];
}

inline bool ESPController::was_key_code_pressed(uint8_t keycode) {
    auto& state = m_shm.state();
    if (state.key_codes_down_once[keycode]) {
        state.key_codes_down_once[keycode] = false;
        return true;
    }
    return false;
}

inline bool ESPController::is_left_mouse_down_raw() const {
    return m_shm.state().left_mouse_down.load();
}

inline bool ESPController::is_right_mouse_down_raw() const {
    return m_shm.state().right_mouse_down.load();
}

inline bool ESPController::is_mouse_in_content() const {
    const auto& state = m_shm.state();
    return state.mouse_x >= 0 && state.mouse_x < state.window_w &&
           state.mouse_y >= 0 && state.mouse_y < state.window_h;
}

inline bool ESPController::is_left_mouse_down() const {
    return is_left_mouse_down_raw() && is_mouse_in_content();
}

inline bool ESPController::is_right_mouse_down() const {
    return is_right_mouse_down_raw() && is_mouse_in_content();
}

inline float ESPController::mouse_x() const {
    return m_shm.state().mouse_x;
}

inline float ESPController::mouse_y() const {
    return m_shm.state().mouse_y;
}

inline float ESPController::window_width() const {
    return m_shm.state().window_w;
}

inline float ESPController::window_height() const {
    return m_shm.state().window_h;
}

inline float ESPController::window_x() const {
    return m_shm.state().window_x;
}

inline float ESPController::window_y() const {
    return m_shm.state().window_y;
}

inline int64_t ESPController::window_number() const {
    return m_shm.state().window_number;
}

inline bool ESPController::is_app_active() const {
    return m_shm.state().app_is_active.load();
}

inline std::array<char, 256> ESPController::window_title() const {
    return m_shm.state().title;
}

inline float ESPController::titlebar_height() const {
    return m_shm.state().titlebar_height;
}

inline bool ESPController::capture_window(const std::string& output_path, uint32_t timeout_ms) {
    auto data = capture_window_to_memory(timeout_ms);
    if (data.empty()) return false;

    FILE* f = fopen(output_path.c_str(), "wb");
    if (!f) return false;

    fwrite(data.data(), 1, data.size(), f);
    fclose(f);
    return true;
}

inline std::vector<uint8_t> ESPController::capture_window_to_memory(uint32_t timeout_ms) {
    auto& cmds = m_shm.commands();
    auto& bulk = m_shm.bulk();

    cmds.capture_complete = false;
    cmds.capture_requested = true;

    SharedSemaphore::post(&cmds.capture_signal);

    uint32_t elapsed = 0;
    while (elapsed < timeout_ms) {
        if (cmds.capture_complete.load()) {
            cmds.capture_requested = false;

            if (bulk.capture_data_length > 0) {
                return std::vector<uint8_t>(
                    bulk.capture_data,
                    bulk.capture_data + bulk.capture_data_length
                );
            }
            return {};
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        elapsed += 10;
    }

    cmds.capture_requested = false;
    return {};
}

inline void ESPController::set_esp_fps(uint32_t fps) {
    m_shm.config().esp_fps = fps;
}

inline void ESPController::set_font(const std::string& name, int size) {
    std::strncpy(m_shm.render().font_name, name.c_str(), sizeof(m_shm.render().font_name) - 1);
    m_shm.render().font_size = size;
}

inline void ESPController::set_dock_badge(const std::string& text) {
    std::strncpy(m_shm.bulk().dock_badge_text, text.c_str(), sizeof(m_shm.bulk().dock_badge_text) - 1);
}

inline void ESPController::set_window_title(const std::string& text) {
    std::strncpy(m_shm.config().window_title_text, text.c_str(), sizeof(m_shm.config().window_title_text) - 1);
}