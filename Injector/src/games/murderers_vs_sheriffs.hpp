#pragma once

#include "games/generic.hpp"

namespace games {

class MurderersVsSheriffsProfile : public GenericProfile {
public:
    std::string name() const override { return "Murderers VS Sheriffs"; }
    uint64_t place_id() const override { return 135856908115931; }
    
    bool detect(const roblox::GameContext& game) const override {
        return game.place_id() == place_id();
    }
    
    void initialize(roblox::GameContext& game) override {
        std::println("[MVS] Initializing Murderers VS Sheriffs profile...");
        config.aim_part = "HumanoidRootPart";
        config.max_distance = 200.0f;
        config.max_delta_dist = 10.0f;

        // Force aim_at_teammates false - we need to investigate team detection
        config.aim_at_teammates = false;
    }
    
    std::vector<Target> find_targets(roblox::GameContext& game, const roblox::CFrame& camera_cf, float fov_radians) override
    {
        // For now, show ALL players and let user decide
        // TODO: Investigate how teams work in this game
        auto targets = GenericProfile::find_targets(game, camera_cf, fov_radians);
        
        // Debug: Log team info once
        static bool logged = false;
        if (!logged && !targets.empty()) {
            logged = true;
            auto local = game.local_player();
            auto my_team = local.team();
            std::println("[MVS] My team: {:#x}", my_team ? my_team.address() : 0);
            
            auto teams = game.teams();
            if (teams) {
                std::println("[MVS] Teams service found, children:");
                for (const auto& child : teams.children()) {
                    auto name = child.name().value_or("???");
                    std::println("[MVS]   - {} ({:#x})", name, child.address());
                }
            } else {
                std::println("[MVS] No Teams service found");
            }
        }
        
        return targets;
    }
};

} // namespace games
