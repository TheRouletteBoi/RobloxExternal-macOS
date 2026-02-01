#include "process/process.hpp"
#include "memory/memory.hpp"
#include "macho/macho.hpp"
#include "scanner/scanner.hpp"
#include "roblox.hpp"
#include "esp_controller.hpp"
#include "games.hpp"

#include <print>
#include <thread>
#include <chrono>
#include <atomic>
#include <cmath>
#include <csignal>

namespace config {
    constexpr const char* APP_NAME = "RobloxPlayer";
    constexpr const char* SHM_PATH = "/tmp/esp_shared_memory";
    constexpr const char* DYLIB_NAME = "libESPManager.dylib";
}

class Application {
public:
    Application(task_t task, ESPController& esp)
        : m_task(task), m_esp(esp), m_game(task), m_running(true),
          m_profileFactory(games::create_default_factory())
    {
        m_genericProfile = std::make_unique<games::GenericProfile>();
    }

    void run() {
        std::println("=== Roblox ESP Application ===\n");

        std::println("Waiting for game...");
        m_game = roblox::GameContext::wait_for_game(m_task, 60);

        std::println("Game: {:#X}", m_game.game().address());

        if (!m_game) {
            std::println("Failed to find game after 60 seconds");
            return;
        }

        std::println("Game found!");
        m_game.print_info();

        detect_game_profile();

        start_character_refresh_thread();
        start_anti_afk_thread();

        m_esp.enable_esp();
        main_loop();
        m_esp.clear_esp();
        m_esp.disable_esp();

        m_running = false;
    }

private:
    task_t m_task;
    ESPController& m_esp;
    roblox::GameContext m_game;
    std::atomic<bool> m_running;

    games::GameProfileFactory m_profileFactory;
    std::unique_ptr<games::GenericProfile> m_genericProfile;
    games::GameProfile* m_activeProfile = nullptr;

    games::AimSettings m_aimSettings;

    void detect_game_profile() {
        uint64_t place_id = m_game.place_id();
        std::println("\nDetecting game profile for PlaceId: {}", place_id);

        m_activeProfile = m_profileFactory.detect_game(m_game);

        if (m_activeProfile) {
            std::println("Detected game: {}", m_activeProfile->name());
            m_activeProfile->initialize(m_game);
        } else {
            std::println("No specific profile found, using Generic");
            m_activeProfile = m_genericProfile.get();
            m_genericProfile->initialize(m_game);
        }

        m_aimSettings.aim_key = m_activeProfile->default_aim_key();
        std::println("Aim key: {}",
            m_aimSettings.aim_key == games::AimKey::LeftMouse ? "Left Mouse" :
            m_aimSettings.aim_key == games::AimKey::RightMouse ? "Right Mouse" : "E Key");

        update_profile_screen_size();
    }

    void update_profile_screen_size() {
        float w = m_esp.window_width();
        float h = m_esp.window_height();

        m_activeProfile->set_screen_size(w, h);

        if (auto* generic = dynamic_cast<games::GenericProfile*>(m_activeProfile)) {
            generic->config.max_delta_dist = m_aimSettings.max_delta_dist;
            generic->config.max_distance = m_aimSettings.max_distance;
        }
        if (auto* pf = dynamic_cast<games::PhantomForcesProfile*>(m_activeProfile)) {
            pf->config.max_delta_dist = m_aimSettings.max_delta_dist;
            pf->config.max_distance = m_aimSettings.max_distance;
        }
    }

    bool is_aim_key_held() {
        switch (m_aimSettings.aim_key) {
            case games::AimKey::LeftMouse:
                return m_esp.is_left_mouse_down();
            case games::AimKey::RightMouse:
                return m_esp.is_right_mouse_down();
            case games::AimKey::KeyE:
                return m_esp.is_key_down('e') || m_esp.is_key_down('E');
        }
        return false;
    }

    void start_character_refresh_thread() {
        std::thread([this]() {
            while (m_running) {
                m_game.refresh_character();
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
        }).detach();
    }

    void start_anti_afk_thread() {
        std::thread([this]() {
            while (m_running) {
                auto local = m_game.local_player();
                if (local) {
                    local.set_last_input_timestamp(9999999.0);
                }
                std::this_thread::sleep_for(std::chrono::seconds(5));
            }
        }).detach();
    }

    void main_loop() {
        std::println("\nEntering main loop. Press '/' to quit.\n");
        std::println("=== Controls ===");
        std::println("  Aim Key    - {} (game-dependent)",
            m_aimSettings.aim_key == games::AimKey::LeftMouse ? "Left Mouse" :
            m_aimSettings.aim_key == games::AimKey::RightMouse ? "Right Mouse" : "E Key");
        std::println("  \\           - Quit");
        std::println("  |           - Reload");
        std::println("");
        std::println("=== Aim Style ===");
        std::println("  * - Silent (direct camera write)");
        std::println("  ( - Legit (smooth interpolation)");
        std::println("  ) - Snap (instant lock)");
        std::println("");
        std::println("=== Target Selection ===");
        std::println("  % - Closest to Crosshair");
        std::println("  ^ - Closest Distance");
        std::println("  & - Lowest Health");
        std::println("");
        std::println("=== Aim Part ===");
        std::println("  # - Head");
        std::println("  $ - Torso");
        std::println("");
        std::println("=== Adjustments ===");
        std::println("  [ / ] - Decrease/Increase smoothing");
        std::println("  - / = - Decrease/Increase max delta distance");
        std::println("  ;     - Toggle aim on/off");
        if (dynamic_cast<games::PhantomForcesProfile*>(m_activeProfile)) {
            std::println("  T     - Switch target team (PF only)");
        }
        std::println("");

        while (m_running) {
            if (m_esp.was_key_pressed('\\')) {
                std::println("Quit requested");
                break;
            }

            handle_hotkeys();

            m_activeProfile->update(m_game);

            update_profile_screen_size();

            update_esp_and_aim();

            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }
    }

    void handle_hotkeys() {
        if (m_esp.was_key_pressed('|')) {
            auto result = process::inject_dylib(config::APP_NAME, config::DYLIB_NAME, process::InjectionMode::FORCE_RESTART);
            if (!result.success) {
                std::println("Failed to restart with dylib");
            }
        }

        if (m_esp.was_key_pressed('t') || m_esp.was_key_pressed('T')) {
            if (auto* pf = dynamic_cast<games::PhantomForcesProfile*>(m_activeProfile)) {
                pf->switch_teams();
            }
        }

        // Aim style toggle: * = Silent, ( = Legit, ) = Snap
        if (m_esp.was_key_pressed('*')) {
            m_aimSettings.style = games::AimStyle::Silent;
            std::println("[CONFIG] Aim style: Silent");
        }
        if (m_esp.was_key_pressed('(')) {
            m_aimSettings.style = games::AimStyle::Legit;
            std::println("[CONFIG] Aim style: Legit (smoothing={})", m_aimSettings.smoothing);
        }
        if (m_esp.was_key_pressed(')')) {
            m_aimSettings.style = games::AimStyle::Snap;
            std::println("[CONFIG] Aim style: Snap");
        }

        // Target selection: % = Crosshair, ^ = Distance, & = Health
        if (m_esp.was_key_pressed('%')) {
            m_aimSettings.selection = games::TargetSelection::ClosestToCrosshair;
            std::println("[CONFIG] Selection: Closest to Crosshair");
        }
        if (m_esp.was_key_pressed('^')) {
            m_aimSettings.selection = games::TargetSelection::ClosestDistance;
            std::println("[CONFIG] Selection: Closest Distance");
        }
        if (m_esp.was_key_pressed('&')) {
            m_aimSettings.selection = games::TargetSelection::LowestHealth;
            std::println("[CONFIG] Selection: Lowest Health");
        }

        // Aim part: # = Head, $ = Torso
        if (m_esp.was_key_pressed('#')) {
            m_aimSettings.aim_part = games::AimPart::Head;
            m_activeProfile->set_aim_part("Head");
            std::println("[CONFIG] Aim part: Head");
        }
        if (m_esp.was_key_pressed('$')) {
            m_aimSettings.aim_part = games::AimPart::Torso;
            m_activeProfile->set_aim_part("HumanoidRootPart");
            std::println("[CONFIG] Aim part: Torso");
        }

        // Smoothing adjustment: [ and ] keys
        if (m_esp.was_key_pressed('[')) {
            m_aimSettings.smoothing = std::max(1.0f, m_aimSettings.smoothing - 1.0f);
            std::println("[CONFIG] Smoothing: {}", m_aimSettings.smoothing);
        }
        if (m_esp.was_key_pressed(']')) {
            m_aimSettings.smoothing = std::min(20.0f, m_aimSettings.smoothing + 1.0f);
            std::println("[CONFIG] Smoothing: {}", m_aimSettings.smoothing);
        }

        // Max delta distance: - and = keys
        if (m_esp.was_key_pressed('-')) {
            m_aimSettings.max_delta_dist = std::max(1.0f, m_aimSettings.max_delta_dist - 1.0f);
            std::println("[CONFIG] Max delta: {} studs", m_aimSettings.max_delta_dist);
        }
        if (m_esp.was_key_pressed('=')) {
            m_aimSettings.max_delta_dist = std::min(50.0f, m_aimSettings.max_delta_dist + 1.0f);
            std::println("[CONFIG] Max delta: {} studs", m_aimSettings.max_delta_dist);
        }

        // Toggle aim on/off: ;
        if (m_esp.was_key_pressed(';')) {
            m_aimSettings.enabled = !m_aimSettings.enabled;
            std::println("[CONFIG] Aim: {}", m_aimSettings.enabled ? "ENABLED" : "DISABLED");
        }
    }

    void update_esp_and_aim() {
        auto camera = m_game.camera();
        if (!camera) {
            m_esp.clear_esp();
            return;
        }

        roblox::CFrame camera_cf = camera.cframe();
        float fov = camera.field_of_view();
        float screen_w = m_esp.window_width();
        float screen_h = m_esp.window_height();

        auto targets = m_activeProfile->find_targets(m_game, camera_cf, fov);

        if (targets.empty()) {
            m_esp.clear_esp();
            return;
        }

        games::Target* best_target = nullptr;
        float best_score = std::numeric_limits<float>::max();

        for (auto& t : targets) {
            if (!t.is_valid) continue;

            // Must be within delta distance for aim
            if (t.delta_distance > m_aimSettings.max_delta_dist)
                continue;

            float score = 0;
            switch (m_aimSettings.selection) {
                case games::TargetSelection::ClosestToCrosshair:
                    score = t.delta_distance;
                    break;
                case games::TargetSelection::ClosestDistance:
                    score = t.distance_3d;
                    break;
                case games::TargetSelection::LowestHealth:
                    score = t.health;
                    break;
            }

            if (score < best_score) {
                best_score = score;
                best_target = &t;
            }
        }

        bool aim_held = is_aim_key_held();

        if (aim_held && best_target && m_aimSettings.enabled) {
            m_activeProfile->apply_aim(*best_target, m_game, camera_cf, m_aimSettings);

            static int aim_log_counter = 0;
            if (++aim_log_counter >= 30) {
                aim_log_counter = 0;
                std::println("[AIM] {} | dist={:.1f} delta={:.2f} style={}",
                            best_target->name, best_target->distance_3d, best_target->delta_distance,
                            static_cast<int>(m_aimSettings.style));
            }
        }

        m_esp.begin_frame();

        int box_index = 0;
        for (const auto& target : targets) {
            if (!target.is_valid) continue;

            bool is_aim_target = (best_target && &target == best_target && aim_held);
            ESPColor color = m_activeProfile->get_target_color(target, is_aim_target);
            float border = m_activeProfile->get_target_border_width(target, is_aim_target);

            std::string label = target.name;
            label += std::format(" [{:.0f}m]", target.distance_3d);

            float box_width = std::max(20.0f, 2000.0f / target.distance_3d);
            float box_height = box_width * 2.0f;

            float x = target.screen_position.x;
            float y = target.screen_position.y;

            m_esp.add_box(box_index++,
                        x - box_width / 2,
                        y - box_height / 2,
                        box_width, box_height,
                        color, label, border);

            if (box_index >= MAX_ESP_COUNT - 1)
                break;
        }

        m_esp.end_frame();
    }
};

void dump_memory_info(task_t task, vm_address_t image_base) {
    std::println("\n=== Memory Layout ===\n");

    std::println("Segments:");
    macho::print_segments(task, image_base);

    std::println("\nSections:");
    macho::print_sections(task, image_base);

    auto code_size = macho::get_total_code_size(task, image_base);
    std::println("\nTotal code size: 0x{:X} ({} bytes)", code_size, code_size);
}

void test_pattern_scan(task_t task, vm_address_t image_base) {
    std::println("\n=== Pattern Scanning ===\n");

    auto ret_results = scanner::scan_code(task, image_base, "C0 03 5F D6");
    std::println("Found {} RET instructions in __text", ret_results.size());

    auto str_results = scanner::find_string(task, image_base, "<<<ROOT>>>");
    std::println("Found {} occurrences of '<<<ROOT>>>' in __cstring", str_results.size());
    for (const auto& r : str_results) {
        std::println("  0x{:X}", r.address);
    }

    auto data_str_results = scanner::find_string_in_data(task, image_base, "<<<ROOT>>>");
    std::println("Found {} occurrences in __DATA", data_str_results.size());
}

void show_mouse_position_for_5sec(ESPController& ctrl) {
    std::println("\n=== Input Monitoring (5 seconds) ===\n");
    std::println("Move mouse and press keys...");

    auto start = std::chrono::steady_clock::now();
    float last_x = 0, last_y = 0;

    while (true) {
        auto now = std::chrono::steady_clock::now();
        float elapsed = std::chrono::duration<float>(now - start).count();

        if (elapsed >= 5.0f)
            break;

        float x = ctrl.mouse_x();
        float y = ctrl.mouse_y();

        if (std::abs(x - last_x) > 10 || std::abs(y - last_y) > 10) {
            std::println("Mouse: ({:.0f}, {:.0f})", x, y);
            last_x = x;
            last_y = y;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

static ESPController* g_esp = nullptr;

void signal_handler(int sig) {
    std::println("\nSignal {} received, cleaning up...", sig);
    if (g_esp) {
        g_esp->clear_esp();
        g_esp->disable_esp();
    }
    std::exit(0);
}

int main(int argc, char* argv[]) {
    std::println("========================================");
    std::println("    RobloxEspCPP (Injector)");
    std::println("========================================");
    std::println(" App Name    : RobloxEspCPP (Injector)");
    std::println(" Author      : TheRouLetteBoi");
    std::println(" Repository  : https://github.com/TheRouletteBoi/RobloxEspCPP");
    std::println(" License     : MIT");
    std::println("========================================\n");

    auto injection_result = process::inject_dylib(config::APP_NAME, config::DYLIB_NAME, process::InjectionMode::AUTO);

    if (!injection_result.success) {
        std::println("Failed to attach to {}", config::APP_NAME);
        std::println("Make sure the app is running and you have SIP Disabled.");
        return 1;
    }

    task_t task = injection_result.task;
    pid_t pid = injection_result.pid;

    std::println("Attached to {} (pid: {}, task: {})", config::APP_NAME, pid, task);

    auto image = macho::get_image_info(task, config::APP_NAME);

    if (image.base == 0) {
        std::println("Failed to find image base");
        return 1;
    }

    std::println("Image base: 0x{:X}, slide: 0x{:X}", image.base, image.slide);

    dump_memory_info(task, image.base);
    test_pattern_scan(task, image.base);

    try {
        ESPController ctrl(config::SHM_PATH);

        g_esp = &ctrl;
        std::signal(SIGINT, signal_handler);
        std::signal(SIGTERM, signal_handler);

        std::println("\nWaiting for ESP dylib...");
        if (!ctrl.wait_for_dylib(5000)) {
            std::println("ESP dylib not ready. Is it injected?");
            std::println("(Continuing without ESP features)");
        } else {
            std::println("Connected to ESP dylib");

            Application app(task, ctrl);
            app.run();
        }

    } catch (const std::exception& e) {
        std::println("ESP error: {}", e.what());
    }

    std::println("\n========================================");
    std::println("    Done!");
    std::println("========================================");

    return 0;
}
