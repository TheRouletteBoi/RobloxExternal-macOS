#pragma once

#include "games/game_profile.hpp"
#include "games/generic.hpp"
#include "games/phantom_forces.hpp"
#include "games/murder_mystery_2.hpp"
#include "games/murderers_vs_sheriffs.hpp"
#include "games/murderers_vs_sheriffs_duels.hpp"

namespace games {

    inline GameProfileFactory create_default_factory() {
        GameProfileFactory factory;

        factory.register_profile<PhantomForcesProfile>();
        factory.register_profile<MurderMystery2Profile>();
        factory.register_profile<MurderersVsSheriffsProfile>();
        factory.register_profile<MurderersVsSheriffsDuelsProfile>();

        return factory;
    }

} // namespace games
