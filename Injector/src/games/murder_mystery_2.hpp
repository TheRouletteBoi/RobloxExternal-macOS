#pragma once

#include "games/generic.hpp"

namespace games {

    class MurderMystery2Profile : public GenericProfile {
    public:
        std::string name() const override { return "Murder Mystery 2"; }
        uint64_t place_id() const override { return 142823291; }

        bool detect(const roblox::GameContext& game) const override {
            return game.place_id() == place_id();
        }

        void initialize(roblox::GameContext& game) override {
            std::println("[MM2] Initializing Murder Mystery 2 profile...");
            config.aim_part = "HumanoidRootPart";
            config.max_distance = 200.0f;
            config.max_delta_dist = 10.0f;
        }
    };

} // namespace games
