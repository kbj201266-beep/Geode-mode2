// Every hook of the mod lives here (one $modify per class) so two features never hook the same function twice.
#include "Common.hpp"
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/modify/CCScheduler.hpp>

#include <chrono>

using namespace geode::prelude;

namespace {
    // true while the window counter runs many game steps inside one rendered frame: the overlays are then
    // drawn once after the burst instead of after every step
    bool g_fastForward = false;
    // true between setupHasCompleted and leaving the level. Nothing of the mod draws or simulates outside of it:
    // the game already runs frames while a level is still loading, and touching the previous level's nodes then
    // was a crash when entering a level.
    PlayLayer* g_ready = nullptr;

    // everything that holds pointers into a level lets go of them while that level still exists
    void leaveLevel() {
        kt::counter::onLevelExit();
        kt::path::reset();        // releases the pathfinder dummy while its level is still alive
        kt::hitbox::enter();
        kt::view::reset();
        kt::hud::detach();
        kt::layout::reset();
        g_ready = nullptr;
    }
}

class $modify(KtPlayLayer, PlayLayer) {
    // every color change of the level (color triggers, level start colors) - layout mode keeps bg/ground blue
    void updateColor(ccColor3B& color, float fadeTime, int colorID, bool blending, float opacity, ccHSVValue& copyHSV,
                     int colorIDToCopy, bool copyOpacity, EffectGameObject* callerObject, int unk1, int unk2) {
        if (!kt::layout::filterColor(colorID, color, fadeTime, colorIDToCopy)) return;
        PlayLayer::updateColor(color, fadeTime, colorID, blending, opacity, copyHSV, colorIDToCopy, copyOpacity,
                               callerObject, unk1, unk2);
    }

    // layout mode: decoration and color / pulse / alpha triggers are left out of the level while it loads
    void addObject(GameObject* obj) {
        if (kt::layout::skipObject(obj)) return;
        PlayLayer::addObject(obj);
    }

    void setupHasCompleted() {
        PlayLayer::setupHasCompleted();
        kt::hitbox::enter();
        kt::path::reset();
        kt::view::reset();
        kt::layout::reset();
        kt::noclip::reset();
        kt::hud::attach(this);
        kt::layout::apply(this);
        kt::counter::onLevelEnter(this);
        g_ready = this;
    }

    void onQuit() {
        kt::counter::cancelCounting();     // never leave the game running at counting speed
        leaveLevel();
        PlayLayer::onQuit();
    }

    // also covers leaving the level any other way than the pause menu
    void onExit() {
        if (g_ready == this) leaveLevel();
        PlayLayer::onExit();
    }

    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);
        if (g_ready != this) return;        // level still loading / already left: touch nothing
        if (g_fastForward) return;          // counting burst: drawn once afterwards
        kt::hitbox::update(this);
        kt::path::update(this);
        kt::counter::update(this);
        kt::view::update(this);
        kt::hud::update(this);
    }

    void destroyPlayer(PlayerObject* player, GameObject* obj) {
        // GD "kills" the player with an invisible anticheat spike at the start of every level. That is not a real
        // death - it must always reach the game, and the counter must never treat it as a failed run.
        if (obj == m_anticheatSpike) {
            PlayLayer::destroyPlayer(player, obj);
            return;
        }
        if (kt::path::simulating()) {          // dummy player died: never touch the real one
            kt::path::onSimDeath(obj);
            return;
        }
        if (kt::counter::onDeath(this, obj)) return;
        if (Cfg::get().noclip) {
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

    void levelComplete() {
        if (kt::counter::onComplete(this)) return;
        // Safe Mode: anything that changes gameplay marks the run as a test so nothing is submitted.
        if (Cfg::get().safeMode && Cfg::get().cheating()) m_isTestMode = true;
        // A level finished by replay playback was not played by you: never save or submit it.
        if (kt::counter::botPlaying()) m_isTestMode = true;
        PlayLayer::levelComplete();
    }
};

class $modify(KtBaseLayer, GJBaseGameLayer) {
    void processCommands(float dt, bool isHalfTick, bool isLastTick) {
        if (!isHalfTick) {
            auto& c = Cfg::get();
            if (c.showTps) kt::speed::onStep();     // only measured when it is shown
            if (c.noclip) kt::noclip::onStep();
        }
        kt::counter::beforeStep(this);
        GJBaseGameLayer::processCommands(dt, isHalfTick, isLastTick);
        // hitbox trail: one box per physics tick (so it follows the TPS), not while counting
        if (!isHalfTick && !g_fastForward && g_ready && static_cast<GJBaseGameLayer*>(g_ready) == this) {
            kt::hitbox::onTick(g_ready);
        }
    }

    void handleButton(bool down, int button, bool isPlayer1) {
        if (!kt::counter::filterInput(down, button, isPlayer1)) return;
        kt::counter::onInput(this, down, button, isPlayer1);
        GJBaseGameLayer::handleButton(down, button, isPlayer1);
    }

    // Window counter: as many game steps per rendered frame as fit into ~14 ms. Each run that finishes is
    // followed by the respawn for the next one right inside the burst, so dozens of runs fit in one frame.
    void countBurst(PlayLayer* pl) {
        auto& c = Cfg::get();
        bool bypass = c.tpsBypass && c.tps >= 241.f;
        float dt = bypass ? 1.f / c.tps : 1.f / 60.f;       // 1 tick (custom TPS) or 4 ticks (240 TPS) per call
        auto t0 = std::chrono::steady_clock::now();
        g_fastForward = true;
        for (int i = 0; i < 4000; ++i) {
            kt::counter::update(pl);                         // finish the run / respawn for the next one
            if (!kt::counter::isCounting()) break;
            if (kt::counter::pendingReset()) continue;
            GJBaseGameLayer::update(dt);
            if (std::chrono::steady_clock::now() - t0 > std::chrono::milliseconds(14)) break;
        }
        g_fastForward = false;
        kt::hitbox::update(pl);
        kt::view::update(pl);
        kt::hud::update(pl);
    }

    // Physical TPS: run the game's own update() several times with dt = 1 / TPS (see Cheats.cpp).
    void update(float dt) {
        auto& c = Cfg::get();
        auto pl = PlayLayer::get();
        bool mine = pl && static_cast<GJBaseGameLayer*>(pl) == this;
        if (mine && pl == g_ready && kt::counter::isCounting() && !g_fastForward) {
            countBurst(pl);
            return;
        }
        if (!c.tpsBypass || c.tps < 241.f || !mine) {
            GJBaseGameLayer::update(dt);
            return;
        }
        float step = 0.f;
        int n = kt::speed::planSteps(dt, step);
        for (int i = 0; i < n; ++i) GJBaseGameLayer::update(step);
    }
};

// Speedhack: scale every frame's delta time.
class $modify(KtScheduler, CCScheduler) {
    void update(float dt) {
        kt::speed::updatePitch();
        CCScheduler::update(dt * kt::speed::factor());
    }
};

#ifdef GEODE_IS_WINDOWS
#include <Geode/modify/CCKeyboardDispatcher.hpp>
// Windows: the T key opens the menu - in a level (the game is paused first) and on the main menu.
// Other screens are left alone so typing a "t" into a search box never opens it.
class $modify(KtKeyboard, CCKeyboardDispatcher) {
    bool dispatchKeyboardMSG(enumKeyCodes key, bool down, bool repeat, double time) {
        if (key == KEY_T && down && !repeat && !kt::ui::isOpen()) {
            auto pl = PlayLayer::get();
            auto scene = CCDirector::get()->getRunningScene();
            bool onMenu = scene && scene->getChildByType<MenuLayer>(0);
            if (pl && g_ready == pl) {
                if (!pl->m_isPaused && !pl->m_hasCompletedLevel) pl->pauseGame(false);
                kt::ui::open();
                return true;
            }
            if (!pl && onMenu) {
                kt::ui::open();
                return true;
            }
        }
        return CCKeyboardDispatcher::dispatchKeyboardMSG(key, down, repeat, time);
    }
};
#endif

// The floating menu button is created once, on the first main menu, and then lives in every scene.
class $modify(KtMenuLayer, MenuLayer) {
    bool init() {
        if (!MenuLayer::init()) return false;
        kt::ui::spawnButton();
        return true;
    }
};
