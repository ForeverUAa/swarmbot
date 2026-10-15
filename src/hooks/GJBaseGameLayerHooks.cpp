#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>

#include "../core/SolverController.hpp"

using namespace geode::prelude;

// Keep this signature identical to GeometryDash.bro for GD 2.208:
// GJBaseGameLayer::processCommands(float). Geode does not reliably warn when a
// $modify method has the wrong signature, so a mismatch can compile but never hook.
// Feed the solver immediately before the game's command/physics processing.
class $modify(DBGameLayer, GJBaseGameLayer) {
    void processCommands(float dt) {
        dashback::SolverController::get().onStep(this);
        GJBaseGameLayer::processCommands(dt);
    }
};
