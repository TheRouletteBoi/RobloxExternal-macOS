#pragma once

#include "roblox/instance.hpp"
#include "roblox/classes.hpp"
#include "roblox/offsets.hpp"
#include "macho/macho.hpp"
#include "scanner/scanner.hpp"

#include <print>
#include <thread>
#include <chrono>

namespace roblox {

class GameContext {
public:
    explicit GameContext(task_t task) : m_task(task) {
        refresh();
    }

    void refresh() {
        m_game = find_game();
        if (!m_game) return;

        m_workspace = Workspace(m_game.find_first_child_of_class("Workspace"));
        m_players = Players(m_game.find_first_child_of_class("Players"));
        m_replicated_storage = m_game.find_first_child_of_class("ReplicatedStorage");
        m_replicated_first = m_game.find_first_child_of_class("ReplicatedFirst");
        m_lighting = m_game.find_first_child_of_class("Lighting");
        m_teams = m_game.find_first_child_of_class("Teams");
        m_core_gui = m_game.find_first_child_of_class("CoreGui");

        if (m_workspace) {
            m_camera = m_workspace.current_camera();
        }

        if (m_players) {
            m_local_player = m_players.local_player();
        }
    }

    void refresh_character() {
        if (!m_local_player) return;
        
        m_my_character = Model(m_local_player.character());
        if (m_my_character) {
            m_my_hrp = m_my_character.humanoid_root_part();
            m_my_humanoid = m_my_character.humanoid();
        }
    }

    task_t task() const { return m_task; }
    
    Instance game() const { return m_game; }
    Workspace workspace() const { return m_workspace; }
    Players players() const { return m_players; }
    Camera camera() const { return m_camera; }
    Player local_player() const { return m_local_player; }
    Instance replicated_storage() const { return m_replicated_storage; }
    Instance replicated_first() const { return m_replicated_first; }
    Instance lighting() const { return m_lighting; }
    Instance teams() const { return m_teams; }
    Instance core_gui() const { return m_core_gui; }
    
    Model my_character() const { return m_my_character; }
    BasePart my_hrp() const { return m_my_hrp; }
    Humanoid my_humanoid() const { return m_my_humanoid; }

    bool is_valid() const {
        return m_game.is_valid();
    }
    
    explicit operator bool() const {
        return is_valid();
    }

    uint64_t place_id() const {
        if (!m_game)
            return 0;

        int64_t pid = 0;
        memory::read_value(m_task, m_game.address() + offsets::DataModel::DATAMODEL_PLACEID, pid);
        return static_cast<uint64_t>(pid);
    }
    
    std::optional<std::string> job_id() const {
        if (!m_game)
            return std::nullopt;
        return read_rbx_string_at(m_task, m_game.address(), offsets::DataModel::DATAMODEL_JOBID);
    }

    static GameContext wait_for_game(task_t task, int timeout_seconds = 60) {
        auto start = std::chrono::steady_clock::now();
        
        while (true) {
            GameContext ctx(task);
            if (ctx.is_valid()) {
                return ctx;
            }
            
            auto elapsed = std::chrono::steady_clock::now() - start;
            if (std::chrono::duration_cast<std::chrono::seconds>(elapsed).count() >= timeout_seconds) {
                return ctx; // Return invalid context
            }
            
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }

    void print_info() const {
        std::println("=== Game Info ===");
        std::println("PlaceId: {}", place_id());
        if (auto jid = job_id()) {
            std::println("JobId: {}", *jid);
        }
        std::println("");
        
        if (m_game) {
            m_game.print_tree(2);
        }
    }
    
private:
    task_t m_task;
    vm_address_t m_image_base = 0;

    Instance m_game;
    Workspace m_workspace;
    Players m_players;
    Camera m_camera;
    Player m_local_player;
    Instance m_replicated_storage;
    Instance m_replicated_first;
    Instance m_lighting;
    Instance m_teams;
    Instance m_core_gui;
    
    // Character references (refreshed separately)
    Model m_my_character;
    BasePart m_my_hrp;
    Humanoid m_my_humanoid;

    Instance find_game() {
        auto image = macho::get_image_info(m_task, "RobloxPlayer");
        if (image.base == 0) {
            std::println("Failed to find RobloxPlayer image");
            return {};
        }
        m_image_base = image.base;

        return find_game_in_memory();
    }

    Instance find_game_in_memory() {
        kern_return_t kr = KERN_SUCCESS;
        vm_address_t address = 0;
        vm_size_t size = 0;
        natural_t depth = 0;
        struct vm_region_submap_info_64 info;
        mach_msg_type_number_t info_count = VM_REGION_SUBMAP_INFO_COUNT_64;

        while (kr == KERN_SUCCESS) {
            memset(&info, 0, sizeof(info));
            info_count = VM_REGION_SUBMAP_INFO_COUNT_64;
            kr = vm_region_recurse_64(m_task, &address, &size, &depth,
                                     (vm_region_recurse_info_t)&info, &info_count);
            if (kr != KERN_SUCCESS) break;

            if (info.user_tag == VM_MEMORY_IOACCELERATOR) {
                auto game = scan_region_for_game(address, size);
                if (game.is_valid()) return game;
            }
            address += size;
        }
        return {};
    }

    Instance scan_region_for_game(vm_address_t start, vm_size_t size) {
        std::vector<uint8_t> buffer(size);
        if (!memory::read_bytes(m_task, start, buffer.data(), size))
            return {};

        for (size_t i = 0; i + 8 <= size; i += 8) {
            vm_address_t candidate = start + i;

            vm_address_t self = 0;
            if (!memory::read_value(m_task, candidate + offsets::Instance::INSTANCE_SELF, self))
                continue;
            if (self != candidate)
                continue;

            Instance inst(m_task, candidate);
            auto name = inst.name();
            if (name && (*name == "Ugc" || *name == "Game")) {
                // should have children (Workspace, Players, ect)
                auto children = inst.children();
                if (children.size() > 10) {
                    return inst;
                }
            }
        }
        return {};
    }
};

} // namespace roblox
