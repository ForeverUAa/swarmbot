#include "SolverController.hpp"

#include "AlgorithmRegistry.hpp"
#include "SolutionStore.hpp"
#include "Perception.hpp"
#include "../algorithms/Builtins.hpp"
#include "../algorithms/ReplayAlgorithm.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/PlayLayer.hpp>
#include <Geode/binding/GJBaseGameLayer.hpp>
#include <Geode/binding/PlayerObject.hpp>
#include <Geode/binding/GJGameLevel.hpp>
#include <Geode/modify/EditorUI.hpp>
#include <Geode/modify/CCKeyboardDispatcher.hpp>

#include <algorithm>
#include <chrono>

using namespace geode::prelude;

namespace dashback {

namespace {
    bool s_editorSolveRequested = false;
    bool s_swarmModeActive = false;
    PlayLayer* s_swarmPlayLayer = nullptr;
}

void requestEditorSolve() {
    s_editorSolveRequested = true;
}

SolverController& SolverController::get() {
    static SolverController instance;
    return instance;
}

static double elapsedMs(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double, std::milli>(
               std::chrono::steady_clock::now() - start)
        .count();
}

static GameMode modeOf(PlayerObject* p) {
    if (!p) return GameMode::Cube;
    if (p->m_isSwing) return GameMode::Swing;
    if (p->m_isSpider) return GameMode::Spider;
    if (p->m_isRobot) return GameMode::Robot;
    if (p->m_isBall) return GameMode::Ball;
    if (p->m_isBird) return GameMode::UFO;
    if (p->m_isDart) return GameMode::Wave;
    if (p->m_isShip) return GameMode::Ship;
    return GameMode::Cube;
}

static const char* modeName(GameMode m) {
    switch (m) {
        case GameMode::Ship: return "ship";
        case GameMode::Ball: return "ball";
        case GameMode::UFO: return "ufo";
        case GameMode::Wave: return "wave";
        case GameMode::Robot: return "robot";
        case GameMode::Spider: return "spider";
        case GameMode::Swing: return "swing";
        case GameMode::Cube: return "cube";
        default: return "?";
    }
}

void SolverController::applySpeed() {
    if (auto* sched = CCDirector::sharedDirector()->getScheduler()) {
        sched->setTimeScale(m_speed);
    }
}

void SolverController::adjustSpeed(float delta) {
    if (!m_playLayer) return;
    m_speed = std::clamp(m_speed + delta, 0.5f, 20.f);
    applySpeed();
    log::info("dashback: speed {:.1f}x", m_speed);
}

void SolverController::onLevelStart(PlayLayer* pl) {
    m_playLayer = nullptr;
    m_active = false;
    m_solved = false;
    m_algo.reset();

    // Only the editor's dedicated button arms a one-shot solve request.
    // Opening or playing ordinary levels must never start the bot.
    if (!s_editorSolveRequested) return;
    s_editorSolveRequested = false;

    m_playLayer = pl;
    if (!pl || !pl->m_level) return;
    if (!Mod::get()->getSettingValue<bool>("enabled")) {
        log::info("dashback: solver disabled via settings");
        return;
    }

    // The editor button always starts an evolutionary swarm. Each real-game
    // attempt evaluates one candidate; after the full population dies, the GA
    // evolves a new generation and the controller resets for its next candidate.
    s_swarmModeActive = true;
    s_swarmPlayLayer = pl;
    pl->m_gameState.m_isFreeMode = true;

    registerBuiltinAlgorithms();

    m_algoId = "genetic";
    if (!AlgorithmRegistry::get().has(m_algoId)) {
        log::warn("dashback: unknown algorithm '{}', using 'backtracking'", m_algoId);
        m_algoId = "backtracking";
    }

    m_level.levelID = pl->m_level->m_levelID;
    m_level.name = std::string(pl->m_level->m_levelName);
    m_level.length = pl->m_levelLength;
    m_maxAttempts = 0; // keep evolving generations until solved or manually stopped
    m_showSensing = Mod::get()->getSettingValue<bool>("show-sensing");
    m_lastHazardDist = -1.f;
    m_lastMode = GameMode::Cube;

    // The dedicated editor button always starts a fresh search. Never replay a
    // previously saved single sequence in swarm mode.
    m_mode = Mode::Search;
    m_algo = AlgorithmRegistry::get().create(m_algoId);
    m_speed = static_cast<float>(Mod::get()->getSettingValue<double>("solve-speed"));
    log::info("dashback: starting genetic swarm for '{}' (id {}) at {}x",
        m_level.name, m_level.levelID, m_speed);
    if (!m_algo) {
        log::error("dashback: failed to create algorithm '{}'", m_algoId);
        return;
    }

    m_sessionId = std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::system_clock::now().time_since_epoch())
                      .count();
    m_metrics.begin(m_level, m_algo->name(), m_sessionId);

    m_active = true;
    m_attempt = 0;
    m_bestEver = 0.f;
    m_algo->onLevelStart(m_level);
    applySpeed();
    beginAttempt();
}

void SolverController::startSearch() {
    m_mode = Mode::Search;
    m_algo = AlgorithmRegistry::get().create(m_algoId);
    m_speed = static_cast<float>(Mod::get()->getSettingValue<double>("solve-speed"));
    m_attempt = 0;
    m_bestEver = 0.f;
    if (m_algo) m_algo->onLevelStart(m_level);
    applySpeed();
}

void SolverController::beginAttempt() {
    m_frame = 0;
    m_bestProgress = 0.f;
    m_lastHold = false;
    m_deadThisAttempt = false;
    m_awaitingFirstStep = true;
    ++m_attempt;
    m_attemptStart = std::chrono::steady_clock::now();
    if (m_algo) m_algo->onAttemptStart(m_attempt);
}

void SolverController::onAttemptStart() {
    if (!m_active || !m_algo) return;
    if (m_awaitingFirstStep) return; // debounce duplicate resetLevel() calls
    if (s_swarmModeActive && m_playLayer) {
        m_playLayer->m_gameState.m_isFreeMode = true;
    }
    beginAttempt();
}

void SolverController::onStep(GJBaseGameLayer* gl) {
    if (!m_active || !m_algo || !m_playLayer) return;
    if (gl != static_cast<GJBaseGameLayer*>(m_playLayer)) return;
    if (s_swarmModeActive) {
        m_playLayer->m_gameState.m_isFreeMode = true;
    }

    auto player = gl->m_player1;
    if (!player || player->m_isDead) return;

    m_awaitingFirstStep = false;

    StepContext ctx;
    ctx.frame = m_frame;
    ctx.player = player;
    ctx.gameLayer = gl;
    ctx.playerX = player->getPositionX();
    ctx.playerY = player->getPositionY();
    ctx.playerYVelocity = player->m_yVelocity;
    ctx.onGround = player->m_isOnGround;
    ctx.upsideDown = player->m_isUpsideDown;
    ctx.isShip = player->m_isShip;
    ctx.mode = modeOf(player);
    m_lastMode = ctx.mode;
    ctx.progress = (gl->m_levelLength > 0.f)
        ? std::clamp(ctx.playerX / gl->m_levelLength, 0.f, 1.f)
        : 0.f;

    // Debug perception overlay (does not influence the algorithm).
    if (m_showSensing) {
        auto s = sense(gl, player, 300.f, 90.f);
        m_lastHazardDist = s.hazardAhead ? s.hazardDistance : -1.f;
    }

    if (ctx.progress > m_bestProgress) m_bestProgress = ctx.progress;
    if (ctx.progress > m_bestEver) m_bestEver = ctx.progress;

    InputState in = m_algo->decide(ctx);
    if (in.hold != m_lastHold) {
        gl->handleButton(in.hold, static_cast<int>(PlayerButton::Jump), true);
        m_lastHold = in.hold;
    }

    ++m_frame;
}

void SolverController::scheduleReset() {
    queueInMainThread([this] {
        if (m_active && m_playLayer) m_playLayer->resetLevel();
    });
}

void SolverController::onDeath(PlayLayer* pl) {
    if (!m_active || !m_algo) return;
    if (pl != m_playLayer) return;
    if (m_deadThisAttempt) return;
    m_deadThisAttempt = true;

    AttemptResult r;
    r.attempt = m_attempt;
    r.success = false;
    r.bestProgress = m_bestProgress;
    r.deathFrame = m_frame;
    r.frames = m_frame;
    r.wallMs = elapsedMs(m_attemptStart);
    m_metrics.record(r);
    m_algo->onDeath(DeathInfo{m_frame, m_bestProgress});

    log::info("dashback: attempt {} died at frame {} ({:.1f}%, best ever {:.1f}%) [{:.0f}ms]",
        m_attempt, m_frame, m_bestProgress * 100.f, m_bestEver * 100.f, r.wallMs);

    if (m_mode == Mode::Replay) {
        log::warn("dashback: saved solution for '{}' is invalid, discarding and re-searching",
            m_level.name);
        SolutionStore::remove(m_algoId, m_level.levelID);
        startSearch();
        scheduleReset();
        return;
    }

    if (reachedAttemptLimit() || !m_algo->wantsAnotherAttempt()) {
        log::info("dashback: stopping after {} attempts (best {:.1f}%)", m_attempt, m_bestEver * 100.f);
        m_active = false;
        return;
    }

    scheduleReset();
}

void SolverController::onComplete(PlayLayer* pl) {
    if (!m_active || !m_algo) return;
    if (pl != m_playLayer) return;

    AttemptResult r;
    r.attempt = m_attempt;
    r.success = true;
    r.bestProgress = 1.0f;
    r.deathFrame = m_frame;
    r.frames = m_frame;
    r.wallMs = elapsedMs(m_attemptStart);
    m_metrics.record(r);
    m_algo->onComplete(r);

    m_solved = true;
    m_active = false;
    m_bestProgress = 1.0f;
    m_bestEver = 1.0f;

    if (m_mode == Mode::Search) {
        auto seq = m_algo->solution();
        if (!seq.empty()) {
            SolutionStore::save(m_algoId, m_level.levelID, seq);
            log::info("dashback: SOLVED '{}' with '{}' on attempt {} — reopen the level to watch it replay end-to-end",
                m_level.name, m_algoId, m_attempt);
        } else {
            log::info("dashback: SOLVED '{}' on attempt {} (no sequence to save)", m_level.name, m_attempt);
        }
    } else {
        log::info("dashback: replay of '{}' completed — solution verified", m_level.name);
    }
}

void SolverController::onLevelEnd() {
    s_swarmModeActive = false;
    s_swarmPlayLayer = nullptr;
    s_editorSolveRequested = false;
    if (m_active) {
        log::info("dashback: level closed after {} attempts", m_attempt);
    }
    m_active = false;
    m_playLayer = nullptr;
    m_algo.reset();

    if (auto* sched = CCDirector::sharedDirector()->getScheduler()) {
        sched->setTimeScale(1.0f);
    }
}

bool SolverController::reachedAttemptLimit() const {
    return m_maxAttempts > 0 && m_attempt >= m_maxAttempts;
}

std::string SolverController::hudText() const {
    if (!m_active && !m_solved) return "";
    std::string algo = (m_mode == Mode::Replay) ? std::string("replay")
                                                 : (m_algo ? m_algo->name() : std::string("-"));
    std::string status = m_solved ? "\nSOLVED" : (m_active ? "" : "\nSTOPPED");
    std::string text = fmt::format("dashback [{}]  {:.1f}x\nMode: {}\nAttempt: {}\nNow: {:.1f}%  Best: {:.1f}%{}",
        algo, m_speed, modeName(m_lastMode), m_attempt,
        m_bestProgress * 100.f, m_bestEver * 100.f, status);
    if (m_showSensing) {
        text += (m_lastHazardDist >= 0.f)
            ? fmt::format("\nHazard: {:.0f}", m_lastHazardDist)
            : std::string("\nHazard: none");
    }
    return text;
}

} // namespace dashback


// Editor-only entry point. Copy the vanilla Test button's sprite so this control
// matches the existing editor UI without introducing a separate visual style.
class $modify(DBEditorUI, EditorUI) {
    bool init(LevelEditorLayer* editorLayer) {
        if (!EditorUI::init(editorLayer)) return false;

        auto testMenu = this->getChildByID("playtest-menu");
        auto testButton = typeinfo_cast<CCMenuItemSpriteExtra*>(
            testMenu ? testMenu->getChildByID("playtest-button") : nullptr
        );
        if (!testMenu || !testButton) {
            log::warn("swarmbot: couldn't find the editor Test button");
            return true;
        }

        auto sourceSprite = typeinfo_cast<CCSprite*>(testButton->getNormalImage());
        if (!sourceSprite) {
            log::warn("swarmbot: couldn't copy the editor Test button sprite");
            return true;
        }

        auto sprite = CCSprite::createWithTexture(
            sourceSprite->getTexture(), sourceSprite->getTextureRect()
        );
        if (!sprite) return true;
        sprite->setFlipX(sourceSprite->isFlipX());
        sprite->setFlipY(sourceSprite->isFlipY());

        auto button = CCMenuItemSpriteExtra::create(
            sprite, this, menu_selector(DBEditorUI::onSwarmBot)
        );
        if (!button) return true;
        button->setID("swarmbot-editor-button");
        button->setScale(testButton->getScale());

        auto menu = CCMenu::create();
        if (!menu) return true;
        menu->setID("swarmbot-editor-menu");
        menu->addChild(button);
        menu->setPosition(testMenu->getPositionX(), testMenu->getPositionY() - 46.f);
        this->addChild(menu, 1000);

        return true;
    }

    void onSwarmBot(CCObject* sender) {
        dashback::requestEditorSolve();
        this->onPlaytest(sender);
    }
};


// During swarm runs, the camera is detached from player-follow. Arrow keys pan
// the view independently so the user can inspect the level while candidates run.
class $modify(DBSwarmKeyboard, CCKeyboardDispatcher) {
    bool dispatchKeyboardMSG(enumKeyCodes key, bool down, bool repeat, double delta) {
        if (down && !repeat && dashback::s_swarmModeActive && dashback::s_swarmPlayLayer) {
            auto* layer = dashback::s_swarmPlayLayer;
            auto camera = layer->m_gameState.m_cameraPosition;
            constexpr float pan = 60.f;
            const bool platformer = layer->m_level && layer->m_level->isPlatformer();

            // Platformer levels use left/right/up. Classic levels use up only.
            switch (key) {
                case KEY_Left:
                    if (!platformer) break;
                    layer->cameraMoveX(camera.x - pan, 0.f, 1.f, false);
                    return true;
                case KEY_Right:
                    if (!platformer) break;
                    layer->cameraMoveX(camera.x + pan, 0.f, 1.f, false);
                    return true;
                case KEY_Up:
                    layer->cameraMoveY(camera.y + pan, 0.f, 1.f, true);
                    return true;
                default:
                    break;
            }
        }
        return CCKeyboardDispatcher::dispatchKeyboardMSG(key, down, repeat, delta);
    }
};
