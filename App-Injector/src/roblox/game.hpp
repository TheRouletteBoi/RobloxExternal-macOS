#pragma once

#include "roblox/instance.hpp"
#include "roblox/classes.hpp"
#include "roblox/offsets.hpp"
#include "macho/macho.hpp"
#include "scanner/scanner.hpp"
#include "dumper/dumper.hpp"

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
        m_data_model = get_data_model();
        if (!m_data_model) return;

        m_workspace = Workspace(m_data_model.find_first_child_of_class("Workspace"));
        m_players = Players(m_data_model.find_first_child_of_class("Players"));
        m_replicated_storage = m_data_model.find_first_child_of_class("ReplicatedStorage");
        m_replicated_first = m_data_model.find_first_child_of_class("ReplicatedFirst");
        m_lighting = m_data_model.find_first_child_of_class("Lighting");
        m_teams = m_data_model.find_first_child_of_class("Teams");
        m_core_gui = m_data_model.find_first_child_of_class("CoreGui");

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
    
    Instance game() const { return m_data_model; }
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
        return m_data_model.is_valid();
    }
    
    explicit operator bool() const {
        return is_valid();
    }

    int64_t place_id() const {
        if (!m_data_model)
            return 0;

        int64_t pid = 0;
        memory::read_value(m_task, m_data_model.address() + offsets::DataModel::DATAMODEL_PLACEID, pid);
        return pid;
    }
    
    std::optional<std::string> job_id() const {
        if (!m_data_model)
            return std::nullopt;
        return read_rbx_string_at(m_task, m_data_model.address(), offsets::DataModel::DATAMODEL_JOBID);
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

    bool wait_for_character(int timeout_seconds = 30) {
        auto start = std::chrono::steady_clock::now();

        while (true) {
            refresh_character();

            if (my_hrp()) {
                return true;
            }

            auto elapsed = std::chrono::steady_clock::now() - start;
            if (std::chrono::duration_cast<std::chrono::seconds>(elapsed).count() >= timeout_seconds) {
                return false;
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
        
        if (m_data_model) {
            m_data_model.print_tree(2);
        }
    }
    
private:
    task_t m_task;
    vm_address_t m_image_base = 0;

    Instance m_data_model;
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

    Instance get_data_model() {
        auto image = macho::get_image_info(m_task, "RobloxPlayer");
        if (image.base == 0) {
            std::println("Failed to find RobloxPlayer image");
            return {};
        }
        m_image_base = image.base;

        auto datamodel = dumper::find_datamodel(m_task, m_image_base);
        if (datamodel) {
            Instance datamodel_instance(m_task, *datamodel);
            auto children = datamodel_instance.children();
            // if it has children and it's likely valid
            if (children.size() > 5)
                return datamodel_instance;
        }

        std::println("[GameContext] RTTI scanner failed...");
        return {};
    }
};

} // namespace roblox
