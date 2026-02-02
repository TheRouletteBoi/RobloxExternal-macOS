#include "objc_shims.h"
#include "../Common/esp_ipc.hpp"

#include <thread>
#include <atomic>
#include <chrono>
#include <memory>
#include <cstring>
#include <csignal>
#include <print>

#include <libproc.h>

class ESPManager {
public:
    ESPManager(const std::string& shm_path);
    ~ESPManager();

    ESPManager(const ESPManager&) = delete;
    ESPManager& operator=(const ESPManager&) = delete;
    
    void run();
    void stop();
    
private:
    SharedMemory m_shm;

    std::atomic<bool> m_running{true};

    std::thread m_worker_thread;
    std::thread m_capture_thread;
    std::thread m_esp_thread;

    void setup_event_monitors();
    void setup_menu();
    bool init_esp_view();

    void worker_loop();      // Handles input commands
    void capture_loop();     // Handles screen capture
    void esp_render_loop();  // Updates ESP boxes

    void process_input(const InputCommand& cmd);

    static void on_mouse_move(float x, float y, void* ctx);
    static void on_left_mouse(bool down, void* ctx);
    static void on_right_mouse(bool down, void* ctx);
    static void on_key(int keycode, const char* chars, bool down, void* ctx);

    static void on_reset_esp(void* ctx);
    static void on_set_low_priority(void* ctx);
    static void on_set_high_priority(void* ctx);
};

ESPManager::ESPManager(const std::string& shm_path)
    : m_shm(SharedMemory::create(shm_path))
{
    std::println("ESP Manager: Initializing...");

    SharedSemaphore::init(&m_shm.commands().input_signal);
    SharedSemaphore::init(&m_shm.commands().capture_signal);

    setup_event_monitors();

    setup_menu();

    std::println("ESP Manager: Initialized successfully");
}

ESPManager::~ESPManager() {
    stop();
}

void ESPManager::run() {
    std::println("ESP Manager: Starting threads...");

    m_worker_thread = std::thread([this]() { worker_loop(); });

    m_capture_thread = std::thread([this]() { capture_loop(); });

    m_esp_thread = std::thread([this]() { esp_render_loop(); });

    m_shm.state().dylib_ready = true;

    std::println("ESP Manager: Running");
}

void ESPManager::stop() {
    if (!m_running.exchange(false)) return;

    std::println("ESP Manager: Stopping...");

    // Wake up waiting threads by posting to their signals
    SharedSemaphore::post(&m_shm.commands().input_signal);
    SharedSemaphore::post(&m_shm.commands().capture_signal);

    if (m_worker_thread.joinable()) m_worker_thread.join();
    if (m_capture_thread.joinable()) m_capture_thread.join();
    if (m_esp_thread.joinable()) m_esp_thread.join();

    std::println("ESP Manager: Stopped");
}

void ESPManager::setup_event_monitors() {
    shim_setup_event_monitors(
        on_mouse_move,
        on_left_mouse,
        on_right_mouse,
        on_key,
        this
    );
}

void ESPManager::setup_menu() {
    shim_add_menu_item("Tools", "Reset ESP Settings", on_reset_esp, this);
    shim_add_menu_item("Tools", "Set Low Priority", on_set_low_priority, this);
    shim_add_menu_item("Tools", "Set High Priority", on_set_high_priority, this);
}

bool ESPManager::init_esp_view() {
    return shim_init_esp_view();
}

void ESPManager::worker_loop() {
    std::println("Worker thread: Started");

    while (m_running) {
        // Wait for signal from injector (or timeout for periodic tasks)
        bool signaled = SharedSemaphore::wait_for(&m_shm.commands().input_signal, 100);

        if (!m_running) break;

        WindowInfo info;
        if (shim_get_window_info(&info)) {
            auto& state = m_shm.state();
            state.window_w = info.width;
            state.window_h = info.height;
            state.window_x = info.x;
            state.window_y = info.y;
            state.titlebar_height = info.titlebar_height;
            state.window_number = info.window_number;
            state.app_is_active = info.is_active;
            state.title = info.title;
        }

        shim_set_dock_badge(m_shm.bulk().dock_badge_text);
        shim_set_window_title(m_shm.config().window_title_text);

        if (signaled) {
            auto& cmds = m_shm.commands();

            while (true) {
                uint32_t tail = cmds.input_tail.load(std::memory_order_relaxed);
                uint32_t head = cmds.input_head.load(std::memory_order_acquire);
                
                if (RingBuffer::is_empty(head, tail)) {
                    break;
                }

                const InputCommand& cmd = cmds.input_queue[tail];

                process_input(cmd);

                cmds.input_tail.store(
                    RingBuffer::advance<MAX_INPUT_COUNT>(tail),
                    std::memory_order_release
                );
            }
        }
    }
    
    std::println("Worker thread: Exited");
}

void ESPManager::process_input(const InputCommand& cmd) {
    // Only process if app is active
    if (!m_shm.state().app_is_active.load())
        return;

    float content_height = shim_get_content_height();
    float titlebar_height = shim_get_titlebar_height();

    switch (cmd.type) {
        case InputType::MOUSE_MOVE: {
            float y = content_height - cmd.y + titlebar_height;
            shim_move_mouse(cmd.x, y);
            break;
        }
        case InputType::KEY_DOWN:
            shim_send_key_down(cmd.keycode, cmd.characters);
            break;
        case InputType::KEY_UP:
            shim_send_key_up(cmd.keycode, cmd.characters);
            break;
        case InputType::LEFT_MOUSE_DOWN: {
            float y = content_height - cmd.y + titlebar_height;
            shim_send_left_mouse_down(cmd.x, y);
            break;
        }
        case InputType::LEFT_MOUSE_UP: {
            float y = content_height - cmd.y + titlebar_height;
            shim_send_left_mouse_up(cmd.x, y);
            break;
        }
        case InputType::FIRST_RESPONDER_TEXT:
            shim_insert_text(cmd.text);
            break;
    }
}

struct CaptureContext {
    ESPBulkData* bulk;
    ESPCommands* cmds;
};

static void capture_callback(const uint8_t* data, size_t length, void* ctx) {
    auto* capture_ctx = static_cast<CaptureContext*>(ctx);
    
    if (data && length > 0 && length <= sizeof(capture_ctx->bulk->capture_data)) {
        std::memcpy(capture_ctx->bulk->capture_data, data, length);
        capture_ctx->bulk->capture_data_length = length;
        capture_ctx->cmds->capture_complete = true;
    } else {
        capture_ctx->bulk->capture_data_length = 0;
        capture_ctx->cmds->capture_complete = true;
    }
    
    delete capture_ctx;
}

void ESPManager::capture_loop() {
    std::println("Capture thread: Started");
    
    while (m_running) {
        SharedSemaphore::wait(&m_shm.commands().capture_signal);
        
        if (!m_running) break;
        
        auto& cmds = m_shm.commands();
        
        if (cmds.capture_requested.load() && !cmds.capture_complete.load()) {
            int64_t window_num = m_shm.state().window_number;
            
            if (window_num > 0) {
                auto* ctx = new CaptureContext{&m_shm.bulk(), &cmds};
                shim_capture_window(window_num, capture_callback, ctx);
            } else {
                cmds.capture_complete = true;
                m_shm.bulk().capture_data_length = 0;
            }
        }
    }
    
    std::println("Capture thread: Exited");
}

void ESPManager::esp_render_loop() {
    std::println("ESP render thread: Started");

    bool esp_initialized = false;
    bool was_enabled = false;

    while (m_running) {
        auto& render = m_shm.render();

        // Calculate sleep time based on FPS
        uint32_t fps = m_shm.config().esp_fps;
        if (fps == 0) fps = 60;
        uint32_t sleep_us = 1000000 / fps;

        bool is_enabled = render.enabled.load();

        if (is_enabled && !esp_initialized) {
            esp_initialized = init_esp_view();
            if (esp_initialized) {
                shim_set_esp_font(render.font_name, render.font_size);
            }
        }

        // Hide all shapes when transitioning from enabled to disabled
        if (was_enabled && !is_enabled && esp_initialized) {
            // Render with 0 shapes to hide everything
            shim_render_esp_shapes(nullptr, 0, render.font_name, render.font_size);
        }
        was_enabled = is_enabled;

        if (is_enabled && esp_initialized) {
            uint32_t frame_index = render.read_index();
            const ESPFrame& frame = render.frames[frame_index];

            shim_render_esp_shapes(
                frame.shapes,
                frame.count,
                render.font_name,
                render.font_size
            );
        }

        std::this_thread::sleep_for(std::chrono::microseconds(sleep_us));
    }
    
    std::println("ESP render thread: Exited");
}

void ESPManager::on_mouse_move(float x, float y, void* ctx) {
    auto* self = static_cast<ESPManager*>(ctx);
    self->m_shm.state().mouse_x = x;
    self->m_shm.state().mouse_y = y;
}

void ESPManager::on_left_mouse(bool down, void* ctx) {
    auto* self = static_cast<ESPManager*>(ctx);
    self->m_shm.state().left_mouse_down = down;
}

void ESPManager::on_right_mouse(bool down, void* ctx) {
    auto* self = static_cast<ESPManager*>(ctx);
    self->m_shm.state().right_mouse_down = down;
}

void ESPManager::on_key(int keycode, const char* chars, bool down, void* ctx) {
    auto* self = static_cast<ESPManager*>(ctx);
    auto& state = self->m_shm.state();
    
    if (down) {
        state.key_codes_down[keycode] = true;
        state.key_codes_down_once[keycode] = true;
        if (chars && chars[0]) {
            uint8_t c = static_cast<uint8_t>(chars[0]);
            state.keys_down[c] = true;
            state.keys_down_once[c] = true;
        }
    } else {
        state.key_codes_down[keycode] = false;
        if (chars && chars[0]) {
            state.keys_down[static_cast<uint8_t>(chars[0])] = false;
        }
    }
}

void ESPManager::on_reset_esp(void* ctx) {
    auto* self = static_cast<ESPManager*>(ctx);
    self->m_shm.render().enabled = false;
    self->m_shm.config().esp_fps = 60;
    std::println("ESP settings reset");
}

void ESPManager::on_set_low_priority(void* ctx) {
    setpriority(PRIO_DARWIN_PROCESS, getpid(), PRIO_DARWIN_BG);
    std::println("Process priority set to low");
}

void ESPManager::on_set_high_priority(void* ctx) {
    setpriority(PRIO_DARWIN_PROCESS, getpid(), PRIO_MAX);
    std::println("Process priority set to high");
}

static bool is_roblox_player() {
    char path[PROC_PIDPATHINFO_MAXSIZE] = {};

    if (proc_pidpath(getpid(), path, sizeof(path)) <= 0)
        return false;

    if (strstr(path, "RobloxCrashHandler") || strstr(path, "RobloxPlayerInstaller")) {
        return false;
    }

    return strstr(path, "/RobloxPlayer") != nullptr;
}

static std::unique_ptr<ESPManager> g_manager;

extern "C" {

__attribute__((constructor))
void esp_initialize() {

    if (!is_roblox_player())
        return;

    std::println("\n========================================");
    std::println("        RobloxEspCPP DYLIB LOADED");
    std::println("========================================");
    std::println(" App Name    : RobloxEspCPP (DYLIB)");
    std::println(" Author      : TheRouLetteBoi");
    std::println(" Repository  : https://github.com/TheRouletteBoi/RobloxEspCPP");
    std::println(" License     : MIT");
    std::println("========================================\n");
    
    try {
        g_manager = std::make_unique<ESPManager>("/tmp/esp_shared_memory");
        g_manager->run();

        
        std::println("ESP initialized - PID: {}", getpid());
    } catch (const std::exception& e) {
        std::println("ESP initialization failed: {}", e.what());
    }
}

__attribute__((destructor))
void esp_cleanup() {
    std::println("\n========================================");
    std::println("    RobloxEspCPP DYLIB UNLOADING");
    std::println("========================================\n");
    
    g_manager.reset();
}

} // extern "C"
