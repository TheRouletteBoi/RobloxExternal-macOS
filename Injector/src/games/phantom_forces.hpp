#pragma once

#include "games/game_profile.hpp"
#include "memory/memory.hpp"

#include <print>

namespace games {

class PhantomForcesProfile : public GameProfile {
public:
    struct Config {
        float max_distance = 400.0f;
        float max_delta_dist = 15.0f;
        bool predict_velocity = true;
    };

    Config config;

    std::string name() const override { return "Phantom Forces"; }
    uint64_t place_id() const override { return 292439477; }

    bool detect(const roblox::GameContext& game) const override {
        return game.place_id() == place_id();
    }

    void initialize(roblox::GameContext& game) override {
        std::println("[PF] Initializing Phantom Forces profile...");

        m_task = game.task();

        auto teams_service = game.teams();
        if (teams_service) {
            m_phantomsTeam = teams_service.find_first_child("Phantoms");
            m_ghostsTeam = teams_service.find_first_child("Ghosts");

            std::println("[PF] Phantoms team: {:#x}", m_phantomsTeam.address());
            std::println("[PF] Ghosts team: {:#x}", m_ghostsTeam.address());
        } else {
            std::println("[PF] WARNING: Teams service not found!");
        }

        m_initialized = true;
    }

    void update(roblox::GameContext& game) override {
        if (!m_initialized) {
            initialize(game);
        }

        update_workspace_players_folder(game);
        update_camera_parts(game);
        update_enemy_folder(game);
    }

    std::vector<Target> find_targets(
        roblox::GameContext& game,
        const roblox::CFrame& camera_cf,
        float fov_radians) override
    {
        std::vector<Target> targets;

        // PF-specific: Get HRP from camera subject's parent's primarypart
        // my_character = parent(camera_subject)
        // my_hrp = model_get_primarypart(my_character)
        roblox::Vector3 my_pos;

        auto camera = game.camera();
        if (!camera) return targets;

        // Get camera subject
        vm_address_t camera_subject = 0;
        memory::read_value(m_task, camera.address() + offsets::Camera::CAMERA_CAMERASUBJECT, camera_subject);

        if (camera_subject == 0) {
            static int log_ctr = 0;
            if (++log_ctr >= 60) { log_ctr = 0; std::println("[PF] No camera_subject"); }
            return targets;
        }

        // Get parent of camera subject (this is the character model)
        vm_address_t my_character = 0;
        memory::read_value(m_task, camera_subject + offsets::Instance::INSTANCE_PARENT, my_character);

        if (my_character == 0) {
            static int log_ctr = 0;
            if (++log_ctr >= 60) { log_ctr = 0; std::println("[PF] No my_character"); }
            return targets;
        }

        // Get primarypart of character (this is HRP)
        vm_address_t my_hrp_addr = 0;
        memory::read_value(m_task, my_character + offsets::ModelPrimative::MODEL_PRIMARYPART, my_hrp_addr);

        if (my_hrp_addr == 0) {
            static int log_ctr = 0;
            if (++log_ctr >= 60) { log_ctr = 0; std::println("[PF] No my_hrp (primarypart)"); }
            return targets;
        }

        // Read my position from HRP
        roblox::BasePart my_hrp(m_task, my_hrp_addr);
        my_pos = my_hrp.position();

        // Debug: check team folders
        static int debug_ctr = 0;
        if (++debug_ctr >= 120) {
            debug_ctr = 0;
            std::println("[PF] my_pos: ({:.1f}, {:.1f}, {:.1f})", my_pos.x, my_pos.y, my_pos.z);
            std::println("[PF] m_teamFolder0: {:#x}, m_teamFolder1: {:#x}",
                m_teamFolder0 ? m_teamFolder0.address() : 0,
                m_teamFolder1 ? m_teamFolder1.address() : 0);

            if (m_teamFolder0) {
                auto children0 = m_teamFolder0.children();
                std::println("[PF] teamFolder0 has {} children", children0.size());
            }
            if (m_teamFolder1) {
                auto children1 = m_teamFolder1.children();
                std::println("[PF] teamFolder1 has {} children", children1.size());
            }
        }

        // Use m_enemyFolderIndex to select which folder is enemy
        // Press T to toggle if showing wrong team
        roblox::Instance enemy_folder = (m_enemyFolderIndex == 0) ? m_teamFolder0 : m_teamFolder1;

        if (!enemy_folder) {
            static int log_ctr2 = 0;
            if (++log_ctr2 >= 60) { log_ctr2 = 0; std::println("[PF] No enemy_folder"); }
            return targets;
        }

        // Debug
        static int debug_ctr2 = 0;
        if (++debug_ctr2 >= 120) {
            debug_ctr2 = 0;
            std::println("[PF] Enemy folder index: {}, children: {}",
                m_enemyFolderIndex, enemy_folder.children().size());
        }

        auto enemies = enemy_folder.children();

        for (const auto& enemy_model : enemies) {
            Target target;
            target.is_valid = false;
            target.is_teammate = false;

            // Find the torso part (first Part child)
            auto torso = find_first_part_child(enemy_model);
            if (!torso)
                continue;

            // Validate part: check parent and self pointer
            auto parent = torso.parent();
            if (!parent)
                continue;

            vm_address_t self_ptr = 0;
            memory::read_value(m_task, torso.address() + offsets::Instance::INSTANCE_SELF, self_ptr);
            if (self_ptr != torso.address()) continue;

            target.character = enemy_model;
            target.custom_aim_part = torso.address();

            target.name = enemy_model.name().value_or("Enemy");
            target.aim_position = torso.position();

            if (config.predict_velocity) {
                roblox::Vector3 velocity = torso.velocity();
                target.aim_position = target.aim_position + velocity;
            }

            target.distance_3d = my_pos.distance_to(target.aim_position);

            if (target.distance_3d > config.max_distance)
                continue;

            target.delta_distance = calculate_delta_distance(camera_cf, target.aim_position);

            target.screen_position = world_to_screen(target.aim_position, camera_cf, fov_radians, screen_width(), screen_height());

            if (target.screen_position.z <= 0)
                continue;

            target.health = 100.0f;
            target.is_valid = true;
            targets.push_back(target);
        }

        return targets;
    }

    void apply_aim(const Target& target, roblox::GameContext& game, const roblox::CFrame& camera_cf, const AimSettings& settings) override
    {
        // Debug logging
        static int aim_debug_ctr = 0;
        bool should_log = (++aim_debug_ctr >= 30);
        if (should_log) aim_debug_ctr = 0;

        // aims by modifying camera_part's CFrame, not the main camera
        if (m_cameraPartCFrameAddr == 0) {
            if (should_log) std::println("[PF AIM] camera_part_cframe_addr is 0!");
            return;
        }

        if (should_log) {
            std::println("[PF AIM] camera_part_cframe: {:#x}, trigger_cframe: {:#x}",
                m_cameraPartCFrameAddr, m_triggerCFrameAddr);
            std::println("[PF AIM] camera_children: {}", m_cameraChildCount);
        }

        // Calculate direction to target
        roblox::Vector3 direction = (target.aim_position - camera_cf.position).normalized();

        // Calculate yaw compensation
        // Get current trigger look yaw
        roblox::CFrame trigger_cf;
        if (m_triggerCFrameAddr != 0) {
            memory::read_bytes(m_task, m_triggerCFrameAddr, &trigger_cf, sizeof(roblox::CFrame));
        } else {
            if (should_log) std::println("[PF AIM] trigger_cframe_addr is 0, using camera_cf");
            trigger_cf = camera_cf;
        }

        float target_yaw = std::asin(direction.y);
        roblox::Vector3 trigger_look = trigger_cf.look_vector();
        float cam_yaw = std::asin(trigger_look.y);

        // Calculate new Y component with yaw difference
        float yaw_diff = -(cam_yaw - target_yaw);
        float new_lv_y = std::sin(yaw_diff);

        // Flatten direction for horizontal component
        roblox::Vector3 horizontal_dir = direction;
        horizontal_dir.y = 0;
        horizontal_dir = horizontal_dir.normalized();
        horizontal_dir.y = new_lv_y;

        float r20 = -horizontal_dir.x;
        float r21 = -horizontal_dir.y;
        float r22 = -horizontal_dir.z;

        if (should_log) {
            std::println("[PF AIM] Writing r20={:.3f}, r21={:.3f}, r22={:.3f}", r20, r21, r22);
        }

        kern_return_t kr1 = vm_write(m_task, m_cameraPartCFrameAddr + 0x8,  reinterpret_cast<vm_offset_t>(&r20), 4);
        kern_return_t kr2 = vm_write(m_task, m_cameraPartCFrameAddr + 0x14, reinterpret_cast<vm_offset_t>(&r21), 4);
        kern_return_t kr3 = vm_write(m_task, m_cameraPartCFrameAddr + 0x20, reinterpret_cast<vm_offset_t>(&r22), 4);

        if (should_log && (kr1 != KERN_SUCCESS || kr2 != KERN_SUCCESS || kr3 != KERN_SUCCESS)) {
            std::println("[PF AIM] vm_write failed: kr1={}, kr2={}, kr3={}", kr1, kr2, kr3);
        }
    }

    ESPColor get_target_color(const Target& target, bool is_aim_target) const override {
        if (is_aim_target) {
            return ESPColor{1.0f, 0.0f, 1.0f, 1.0f};  // Magenta
        }
        return ESPColor{1.0f, 0.0f, 0.0f, 1.0f};      // Red
    }

    void switch_teams() {
        m_enemyFolderIndex = (m_enemyFolderIndex == 0) ? 1 : 0;
        std::println("[PF] Switched enemy folder to index {}", m_enemyFolderIndex);
    }

private:
    bool m_initialized = false;
    task_t m_task = 0;

    // Team references
    roblox::Instance m_phantomsTeam;
    roblox::Instance m_ghostsTeam;

    // Workspace/Players folder and its children
    roblox::Instance m_workspacePlayersFolder;
    roblox::Instance m_teamFolder0;  // One team folder
    roblox::Instance m_teamFolder1;  // Other team folder
    roblox::Instance m_enemyTeamFolder;

    // Camera parts for aim
    vm_address_t m_cameraPartCFrameAddr = 0;
    vm_address_t m_triggerCFrameAddr = 0;

    // Enemy folder index (0 or 1) - toggle with T key
    int m_enemyFolderIndex = 1;  // Start with folder1 as enemy (usually the larger team folder)

    // Camera child count (for checking if gun is equipped)
    size_t m_cameraChildCount = 0;

    void update_workspace_players_folder(roblox::GameContext& game) {
        auto workspace = game.workspace();
        if (!workspace) return;

        // Find Workspace/Players folder
        m_workspacePlayersFolder = workspace.find_first_child("Players");
        if (!m_workspacePlayersFolder) return;

        // Get children of Players folder (team folders)
        auto team_folders = m_workspacePlayersFolder.children();
        if (team_folders.size() >= 2) {
            m_teamFolder0 = team_folders[0];
            m_teamFolder1 = team_folders[1];
        }
    }

    void update_camera_parts(roblox::GameContext& game) {
        auto camera = game.camera();
        if (!camera) return;

        auto camera_children = camera.children();
        m_cameraChildCount = camera_children.size();

        // Debug logging
        static int cam_debug_ctr = 0;
        bool should_log = (++cam_debug_ctr >= 120);
        if (should_log) cam_debug_ctr = 0;

        if (should_log) {
            std::println("[PF CAM] Camera children count: {}", m_cameraChildCount);
        }

        // Need at least 3 children (gun equipped)
        if (m_cameraChildCount < 3) {
            if (should_log) std::println("[PF CAM] Not enough children (need 3+)");
            m_cameraPartCFrameAddr = 0;
            m_triggerCFrameAddr = 0;
            return;
        }

        // Find camera_part (Part directly under Camera)
        roblox::Instance camera_part;
        for (const auto& child : camera_children) {
            auto cls = child.class_name().value_or("");
            if (should_log) std::println("[PF CAM]   Child: {} ({})", child.name().value_or("?"), cls);
            if (cls == "Part") {
                camera_part = child;
                break;
            }
        }

        if (should_log) {
            std::println("[PF CAM] camera_part found: {}", camera_part ? "yes" : "no");
        }

        // Find trigger (Part with Motor6D child, in last camera child's descendants)
        roblox::Instance trigger;
        if (!camera_children.empty()) {
            auto main = camera_children.back();  // Last child
            if (should_log) std::println("[PF CAM] main (last child): {}", main.name().value_or("?"));

            auto main_children = main.children();

            for (const auto& child : main_children) {
                if (child.class_name().value_or("") == "Part") {
                    // Check if this Part has a Motor6D child
                    for (const auto& sub_child : child.children()) {
                        if (sub_child.class_name().value_or("") == "Motor6D") {
                            trigger = child;
                            if (should_log) std::println("[PF CAM] Found trigger Part with Motor6D");
                            break;
                        }
                    }
                    if (trigger) break;
                }
            }
        }

        // Get CFrame addresses
        if (camera_part) {
            vm_address_t props = 0;
            memory::read_value(m_task, camera_part.address() + offsets::BasePart::BASEPART_PROPERTIES, props);
            if (props != 0) {
                m_cameraPartCFrameAddr = props + offsets::Primitive::BASEPART_PROPS_CFRAME;
                if (should_log) std::println("[PF CAM] camera_part cframe addr: {:#x}", m_cameraPartCFrameAddr);
            } else {
                m_cameraPartCFrameAddr = 0;
                if (should_log) std::println("[PF CAM] camera_part props is 0!");
            }
        } else {
            m_cameraPartCFrameAddr = 0;
        }

        if (trigger) {
            vm_address_t props = 0;
            memory::read_value(m_task, trigger.address() + offsets::BasePart::BASEPART_PROPERTIES, props);
            if (props != 0) {
                m_triggerCFrameAddr = props + offsets::Primitive::BASEPART_PROPS_CFRAME;
            }
        } else {
            m_triggerCFrameAddr = 0;
        }
    }

    void update_enemy_folder(roblox::GameContext& game) {
        auto local_player = game.local_player();
        if (!local_player) return;

        auto my_team = local_player.team();

        // Determine which folder is enemy based on team
        if (my_team && my_team.address() == m_phantomsTeam.address()) {
            m_enemyTeamFolder = m_teamFolder0;  // Orange/other team
        } else if (my_team && my_team.address() == m_ghostsTeam.address()) {
            m_enemyTeamFolder = m_teamFolder1;  // Blue/other team
        } else {
            // Fallback: use first folder if team detection fails
            m_enemyTeamFolder = m_teamFolder0;
        }
    }

    roblox::BasePart find_first_part_child(const roblox::Instance& parent) {
        for (const auto& child : parent.children()) {
            auto class_name = child.class_name();
            if (class_name && *class_name == "Part") {
                return roblox::BasePart(child);
            }
        }
        return roblox::BasePart();
    }
};

} // namespace games
