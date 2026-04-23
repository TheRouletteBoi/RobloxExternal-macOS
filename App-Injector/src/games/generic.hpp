#pragma once

#include "games/game_profile.hpp"

namespace games {

class GenericProfile : public GameProfile {
public:
    struct Config {
        std::string aim_part = "HumanoidRootPart";
        float max_distance = 500.0f;
        float max_delta_dist = 30.0f;

        bool aim_at_teammates = false;
    };

    Config config;

    std::string name() const override { return "Generic"; }
    uint64_t place_id() const override { return 0; }

    // Generic never auto detects, it's used as fallback
    bool detect(const roblox::GameContext& game) const override {
        return false;
    }

    void set_aim_part(const std::string& part_name) override {
        config.aim_part = part_name;
    }

std::vector<Target> find_targets(roblox::GameContext& game, const roblox::CFrame& camera_cf, float fov_radians) override
{
    std::vector<Target> targets;

    auto players = game.players();
    auto local_player = game.local_player();
    auto my_hrp = game.my_hrp();

    if (!players) {
        std::println("[find_targets] players() is null");
        return targets;
    }

    if (!local_player) {
        std::println("[find_targets] local_player() is null");
        return targets;
    }

    if (!my_hrp) {
        std::println("[find_targets] my_hrp() is null");
        return targets;
    }

    roblox::Vector3 my_pos = my_hrp.position();
    auto my_team = local_player.team();

    auto player_list = players.get_players();
    std::println("[find_targets] Total players: {}", player_list.size());

    for (const auto& player : player_list) {

        if (player == local_player) {
            std::println(" - Skipping local player");
            continue;
        }

        Target target;
        target.is_valid = false;

        auto their_team = player.team();
        target.is_teammate = (my_team && their_team && my_team == their_team);

        if (target.is_teammate && !config.aim_at_teammates) {
            std::println(" - {} skipped (teammate)",
                player.name().value_or("???"));
            continue;
        }

        auto character = player.character();
        if (!character) {
            std::println(" - {} skipped (no character)",
                player.name().value_or("???"));
            continue;
        }

        target.character = character;

        target.name = player.display_name().value_or(
                      player.name().value_or("???"));

        auto humanoid = roblox::Humanoid(character.find_first_child_of_class("Humanoid"));
        if (humanoid) {
            target.health = humanoid.health();
            if (target.health <= 0) {
                std::println(" - {} skipped (dead, health={})",
                    target.name, target.health);
                continue;
            }
        } else {
            std::println(" - {} has no humanoid, defaulting health=100",
                target.name);
            target.health = 100.0f;
        }

        auto aim_part = roblox::BasePart(character.find_first_child(config.aim_part));
        if (!aim_part) {
            std::println(" - {} missing aim_part '{}', trying HRP",
                target.name, config.aim_part);
            aim_part = roblox::BasePart(character.find_first_child("HumanoidRootPart"));
        }

        if (!aim_part) {
            std::println(" - {} skipped (no aim part or HRP)", target.name);
            continue;
        }

        target.aim_position = aim_part.position();
        target.velocity = calculate_velocity(character.address(), target.aim_position);
        target.predicted_position = target.aim_position;

        std::println("- aim_position: {} {} {}", target.aim_position.x, target.aim_position.y, target.aim_position.z);
        target.distance_3d = my_pos.distance_to(target.aim_position);

        if (target.distance_3d > config.max_distance) {
            std::println(" - {} skipped (distance {} > max {})",
                target.name, target.distance_3d, config.max_distance);
            continue;
        }

        target.delta_distance = calculate_delta_distance(camera_cf, target.aim_position);

        target.screen_position = world_to_screen(
            target.aim_position,
            camera_cf,
            fov_radians,
            screen_width(),
            screen_height()
        );

        if (target.screen_position.z <= 0) {
            std::println(" - {} skipped (behind camera, z={})",
                target.name, target.screen_position.z);
            continue;
        }

        target.screen_distance = calculate_screen_distance(
            target.screen_position,
            m_mouse_x,
            m_mouse_y
        );

        target.is_valid = true;
        targets.push_back(target);

        std::println(" + {} accepted | dist3d={} screenDist={}",
            target.name,
            target.distance_3d,
            target.screen_distance);

        std::println("   {} depth={} x={} y={}",
    target.name,
    target.screen_position.z,
    target.screen_position.x,
    target.screen_position.y);

    }

    std::println("[find_targets] Final valid targets: {}", targets.size());

    return targets;
}

};

} // namespace games
