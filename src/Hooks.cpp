// Every hook of the mod lives here (one $modify per class) so two features never hook the same function twice.
#include "Common.hpp"
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/modify/CCScheduler.hpp>

using namespace geode::prelude;

class $modify(KtPlayLayer, PlayLayer) {
    void setupHasCompleted() {
        PlayLayer::setupHasCompleted();
        kt::hitbox::reset();
        kt::path::reset();
        kt::noclip::reset();
        kt::hud::attach(this);
        kt::layout::apply(this);
        kt::counter::onLevelEnter(this);
    }

    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);
        kt::hitbox::update(this);
        kt::path::update(this);
        kt::counter::update(this);
        kt::view::update(this);
        kt::hud::update(this);
    }

    void destroyPlayer(PlayerObject* player, GameObject* obj) {
        if (kt::path::simulating()) {          // dummy player died: never touch the real one
            kt::path::onSimDeath();
            return;
        }
        if (kt::counter::onDeath(this)) return;
        if (Cfg::get().noclip && obj != m_anticheatSpike) {
            kt::noclip::onHit();
            return;
        }
        PlayLayer::destroyPlayer(player, obj);
    }

    void resetLevel() {
        PlayLayer::resetLevel();
        kt::hitbox::reset();
        kt::noclip::reset();
        kt::counter::onReset(this);
        kt::practice::sync(this);
        kt::layout::apply(this);
    }

    void loadFromCheckpoint(CheckpointObject* cp) {
        PlayLayer::loadFromCheckpoint(cp);
        kt::counter::onReset(this);        // keeps the recording, cuts it back to the checkpoint frame
        kt::practice::sync(this);
    }

    void onQuit() {
        kt::counter::cancelCounting();     // never leave the game running at counting speed
        PlayLayer::onQuit();
    }

    void levelComplete() {
        if (kt::counter::onComplete(this)) return;
        // Safe Mode: anything that changes gameplay marks the run as a test so nothing is submitted.
        if (Cfg::get().safeMode && Cfg::get().cheating()) m_isTestMode = true;
        PlayLayer::levelComplete();
    }
};

class $modify(KtBaseLayer, GJBaseGameLayer) {
    void processCommands(float dt, bool isHalfTick, bool isLastTick) {
        if (!isHalfTick) {
            kt::speed::onStep();
            kt::noclip::onStep();
        }
        kt::counter::beforeStep(this);
        GJBaseGameLayer::processCommands(dt, isHalfTick, isLastTick);
    }

    void handleButton(bool down, int button, bool isPlayer1) {
        if (!kt::counter::filterInput(down, button, isPlayer1)) return;
        kt::counter::onInput(this, down, button, isPlayer1);
        GJBaseGameLayer::handleButton(down, button, isPlayer1);
    }

    // Physical TPS: run the game's own update() several times with dt = 1 / TPS (see Cheats.cpp).
    void update(float dt) {
        auto& c = Cfg::get();
        auto pl = PlayLayer::get();
        if (!c.tpsBypass || c.tps < 241.f || !pl || static_cast<GJBaseGameLayer*>(pl) != this) {
            GJBaseGameLayer::update(dt);
            return;
        }
        float step = 0.f;
        int n = kt::speed::planSteps(dt, step);
        for (int i = 0; i < n; ++i) GJBaseGameLayer::update(step);
    }
};

// Speedhack: scale every frame's delta time. Also drives the 4x speed of the window counter.
class $modify(KtScheduler, CCScheduler) {
    void update(float dt) {
        kt::speed::updatePitch();
        CCScheduler::update(dt * kt::speed::factor());
    }
};

// The floating menu button is created once, on the first main menu, and then lives in every scene.
class $modify(KtMenuLayer, MenuLayer) {
    bool init() {
        if (!MenuLayer::init()) return false;
        kt::ui::spawnButton();
        return true;
    }
};
