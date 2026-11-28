// Every hook of the mod lives here (one $modify per class) so two features never hook the same function twice.
//
// Hook order: the hooks the window counter / bot depend on run FIRST, before other mods (Silicate, QOLMod,
// other bots and counters hook the same functions). Otherwise, for example, another mod's noclip swallowed the
// death before the counter ever saw it, or another practice fix overwrote the player after our respawn - and the
// counter could not count on Windows, where those mods are usually installed.
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
    // the level is still alive but another scene was put on top of it (it comes back when that one closes).
    // Before, this was treated like quitting: everything stopped and never came back - the hitboxes and the
    // yellow center line froze until the level was re-entered.
    PlayLayer* g_away = nullptr;

    // everything that holds pointers into a level lets go of them while that level still exists
    void leaveLevel() {
        kt::counter::onLevelExit();
        kt::path::reset();        // releases the pathfinder dummy while its level is still alive
        kt::hitbox::enter();
        kt::view::reset();
        kt::hud::detach();
        kt::layout::reset();
        g_ready = nullptr;
        g_away = nullptr;
    }

    // Lives as a child of the PlayLayer and dies with it. If a level is destroyed without onQuit / onExit
    // reaching us (e.g. it was under another scene that got replaced), the mod forgets it here instead of
    // touching freed nodes later.
    class LevelGuard : public CCNode {
    public:
        PlayLayer* m_owner = nullptr;
        static LevelGuard* create(PlayLayer* owner) {
            auto r = new LevelGuard();
            if (r->init()) {
                r->m_owner = owner;
                r->setID("kt-level-guard");
                r->autorelease();
                return r;
            }
            delete r;
            return nullptr;
        }
        ~LevelGuard() override {
            if (!m_owner || (g_ready != m_owner && g_away != m_owner)) return;
            kt::counter::onLevelExit();
            kt::path::forget();       // never touch the dummy: its level is being destroyed right now
            kt::hitbox::enter();
            kt::view::reset();
            kt::hud::detach();
            kt::layout::reset();
            g_ready = nullptr;
            g_away = nullptr;
        }
    };
}

class $modify(KtPlayLayer, PlayLayer) {
    static void onModify(auto& self) {
        (void)self.setHookPriority("PlayLayer::destroyPlayer", Priority::First);
        (void)self.setHookPriority("PlayLayer::resetLevel", Priority::First);
        (void)self.setHookPriority("PlayLayer::loadFromCheckpoint", Priority::First);
        (void)self.setHookPriority("PlayLayer::storeCheckpoint", Priority::First);
        (void)self.setHookPriority("PlayLayer::levelComplete", Priority::First);
        (void)self.setHookPriority("PlayLayer::postUpdate", Priority::First);
    }

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
        if (auto guard = LevelGuard::create(this)) this->addChild(guard);
        g_ready = this;
        g_away = nullptr;
    }

    void onQuit() {
        kt::counter::cancelCounting();     // never leave the game running at counting speed
        if (g_ready == this || g_away == this) leaveLevel();
        PlayLayer::onQuit();
    }

    // Leaving the level any other way than the pause menu (another mod replacing the scene...) cleans up.
    // A scene pushed ON TOP of the level (it stays alive underneath) only pauses the mod until it comes back.
    void onExit() {
        if (g_ready == this) {
            if (CCDirector::get()->isSendCleanupToScene()) {
                leaveLevel();
            } else {
                g_away = this;
                g_ready = nullptr;
            }
        }
        PlayLayer::onExit();
    }

    // the level is shown again (also after a scene on top of it was closed)
    void onEnterTransitionDidFinish() {
        PlayLayer::onEnterTransitionDidFinish();
        if (g_away == this) {
            g_away = nullptr;
            g_ready = this;
        }
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
        kt::counter::onLoadCheckpoint(this, nullptr);   // filled in below if this respawn loads a checkpoint
        PlayLayer::resetLevel();
        kt::hitbox::reset();
        kt::noclip::reset();
        kt::counter::onReset(this);
        kt::counter::onLoadCheckpoint(this, nullptr);
        kt::practice::sync(this);
        kt::layout::apply(this);
    }

    // a practice checkpoint is placed: the bot recorder remembers the whole player with it
    void storeCheckpoint(CheckpointObject* cp) {
        PlayLayer::storeCheckpoint(cp);
        kt::counter::onStoreCheckpoint(this, cp);
    }

    void loadFromCheckpoint(CheckpointObject* cp) {
        PlayLayer::loadFromCheckpoint(cp);
        kt::counter::onLoadCheckpoint(this, cp);
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
    static void onModify(auto& self) {
        (void)self.setHookPriority("GJBaseGameLayer::processCommands", Priority::First);
        (void)self.setHookPriority("GJBaseGameLayer::handleButton", Priority::First);
        (void)self.setHookPriority("GJBaseGameLayer::update", Priority::First);
    }

    void processCommands(float dt, bool isHalfTick, bool isLastTick) {
        if (!isHalfTick) {
            auto& c = Cfg::get();
            if (c.showTps) kt::speed::onStep();     // only measured when it is shown
            if (c.noclip) kt::noclip::onStep();
        }
        kt::counter::beforeStep(this);
        GJBaseGameLayer::processCommands(dt, isHalfTick, isLastTick);
        kt::counter::afterStep();
        // hitbox trail / center line: one point per physics tick (so it follows the TPS), not while counting
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
// Windows: the T key opens the menu everywhere - main menu, level list, editor, pause menu, level complete
// screen... - except while a level is actually being played (then T stays a normal game key) and while you
// type into a text box.
class $modify(KtKeyboard, CCKeyboardDispatcher) {
    static bool menuKeyAllowed() {
        if (kt::ui::isOpen()) return false;
        if (auto ime = CCIMEDispatcher::sharedDispatcher(); ime && ime->hasDelegate()) return false;   // typing
        auto scene = CCDirector::get()->getRunningScene();
        if (!scene || typeinfo_cast<CCTransitionScene*>(scene)) return false;    // scene still changing
        if (auto pl = PlayLayer::get()) {
            bool finished = pl->m_hasCompletedLevel || pl->m_levelEndAnimationStarted;
            if (!pl->m_isPaused && !finished) return false;                      // playing
        }
        if (auto el = LevelEditorLayer::get(); el && el->m_playbackMode == PlaybackMode::Playing) return false;
        return true;
    }

    bool dispatchKeyboardMSG(enumKeyCodes key, bool down, bool repeat, double time) {
        if (key == KEY_T && down && !repeat && menuKeyAllowed()) {
            kt::ui::open();
            return true;
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
