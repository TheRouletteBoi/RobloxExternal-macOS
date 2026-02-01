#pragma once

#include "games/game_profile.hpp"

namespace games {

class GenericProfile : public GameProfile {
public:
    struct Config {
        std::string aim_part = "HumanoidRootPart";
        float max_distance = 500.0f;
        float max_delta_dist = 8.0f;

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

        if (!players || !local_player || !my_hrp) {
            return targets;
        }

        roblox::Vector3 my_pos = my_hrp.position();
        auto my_team = local_player.team();

        for (const auto& player : players.get_players()) {
            if (player == local_player) continue;

            Target target;
            target.is_valid = false;

            auto their_team = player.team();
            target.is_teammate = (my_team && their_team && my_team == their_team);

            if (target.is_teammate && !config.aim_at_teammates) {
                continue;
            }

            auto character = player.character();
            if (!character) continue;

            target.character = character;

            target.name = player.display_name().value_or(
                          player.name().value_or("???"));

            auto humanoid = roblox::Humanoid(character.find_first_child_of_class("Humanoid"));
            if (humanoid) {
                target.health = humanoid.health();
                if (target.health <= 0)
                    continue;
            } else {
                target.health = 100.0f;
            }

            auto aim_part = roblox::BasePart(character.find_first_child(config.aim_part));
            if (!aim_part) {
                aim_part = roblox::BasePart(character.find_first_child("HumanoidRootPart"));
            }
            if (!aim_part)
                continue;

            target.aim_position = aim_part.position();

            target.distance_3d = my_pos.distance_to(target.aim_position);

            if (target.distance_3d > config.max_distance)
                continue;

            target.delta_distance = calculate_delta_distance(camera_cf, target.aim_position);

            target.screen_position = world_to_screen(target.aim_position, camera_cf, fov_radians, screen_width(), screen_height());

            if (target.screen_position.z <= 0)
                continue;  // Behind camera

            target.is_valid = true;
            targets.push_back(target);
        }

        return targets;
    }
};

} // namespace games
