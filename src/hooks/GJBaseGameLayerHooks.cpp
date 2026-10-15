#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>

#include "../core/SolverController.hpp"

using namespace geode::prelude;

// Match the 2.2081 bindings selected by mod.json. This target's binding includes
// both tick flags; do not copy the one-argument signature from the 2.208 binding.
// Feed the solver before the game's command/physics processing.
class $modify(DBGameLayer, GJBaseGameLayer) {
    void processCommands(float dt, bool isHalfTick, bool isLastTick) {
        dashback::SolverController::get().onStep(this);
        GJBaseGameLayer::processCommands(dt, isHalfTick, isLastTick);
    }
};
