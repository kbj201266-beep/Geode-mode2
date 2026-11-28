# GD Toolkit v1.2.6 - 전체 코드

각 파일 내용을 전부 지우고 아래 코드로 통째로 바꾸세요. 이미지/효과음(resources 폴더)과 tools 폴더는 그대로 둡니다.

## mod.json

```json
{
    "geode": "6.0.0",
    "gd": {
        "win": "2.2091",
        "android": "2.2091",
        "mac": "2.2091"
    },
    "id": "kbjgd.gd-toolkit",
    "name": "GD Toolkit",
    "version": "v1.2.6",
    "developer": "KBJGD",
    "description": "Hitboxes, noclip, pathfinder, layout mode, practice music sync, speedhack, physical TPS and a window frame counter.",
    "tags": ["gameplay", "utility"],
    "dependencies": {
        "geode.node-ids": {
            "version": ">=v1.23.0",
            "required": false
        }
    },
    "resources": {
        "sprites": ["resources/sprites/*.png"],
        "files": ["resources/sfx/*.wav"]
    }
}
```

## CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.21)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64")
set(CMAKE_CXX_VISIBILITY_PRESET hidden)

project(GDToolkit VERSION 1.2.0)

# Windows: the GitHub runner's Visual Studio library can be newer than its clang and then refuses to compile
# ("STL1000: Unexpected compiler version"). This tells it to accept the compiler anyway.
if (WIN32)
    add_compile_definitions(_ALLOW_COMPILER_AND_STL_VERSION_MISMATCH)
endif()

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS src/*.cpp)
add_library(${PROJECT_NAME} SHARED ${SOURCES})

if (NOT DEFINED ENV{GEODE_SDK})
    message(FATAL_ERROR "Unable to find Geode SDK! Please define GEODE_SDK environment variable to point to Geode")
endif()

add_subdirectory($ENV{GEODE_SDK} ${CMAKE_CURRENT_BINARY_DIR}/geode)
setup_geode_mod(${PROJECT_NAME})
```

## .github/workflows/build.yml

```yaml
name: Build mod

on:
  workflow_dispatch:
  push:
    branches: [main]

jobs:
  build:
    strategy:
      fail-fast: false            # if one platform fails, the other one still builds
      matrix:
        config:
          - name: Windows
            os: windows-latest
            target: Win64
          - name: Android64
            os: ubuntu-latest
            target: Android64

    name: ${{ matrix.config.name }}
    runs-on: ${{ matrix.config.os }}

    steps:
      - uses: actions/checkout@v4

      - name: Build the mod
        uses: geode-sdk/build-geode-mod@main
        with:
          sdk: nightly
          combine: true
          target: ${{ matrix.config.target }}

  package:
    name: Package builds
    runs-on: ubuntu-latest
    needs: ['build']
    if: always()

    steps:
      - uses: geode-sdk/build-geode-mod/combine@main
        id: build

      - uses: actions/upload-artifact@v4
        with:
          name: Build Output
          path: ${{ steps.build.outputs.build-output }}
```

## src/Config.hpp

```cpp
#pragma once
#include <Geode/Geode.hpp>

// X-macro tables: one line per setting, name + default. Saved under the same key.
#define KT_BOOLS(X) \
    X(noclip, false) X(safeMode, true) X(speedhack, false) X(tpsBypass, false) X(showTps, false) \
    X(hitboxes, false) X(playerHitbox, true) X(hitboxTrail, false) X(hitborders, false) \
    X(pathfinder, false) \
    X(layoutMode, false) X(practiceSync, false) \
    X(counterHud, true) X(counterSfx, true) X(ignoreInputs, true) X(counterOverlay, true) X(saveOnFinish, true) X(counterList, false)
#define KT_FLOATS(X) \
    X(speed, 1.f) X(tps, 240.f) X(hitFill, 0.18f) X(hitBorder, 1.5f) \
    X(btnScale, 0.55f) X(btnX, 0.92f) X(btnY, 0.55f)
#define KT_INTS(X) X(trailLen, 90) X(pathLen, 90)

struct Cfg {
#define X(n, d) bool n = d;
    KT_BOOLS(X)
#undef X
#define X(n, d) float n = d;
    KT_FLOATS(X)
#undef X
#define X(n, d) int n = d;
    KT_INTS(X)
#undef X

    static Cfg& get();
    void load();
    void save() const;

    // anything that changes gameplay -> level completion is not submitted when Safe Mode is on
    bool cheating() const {
        return noclip || (speedhack && speed != 1.f) || (tpsBypass && tps != 240.f);
    }
};
```

## src/Config.cpp

```cpp
#include "Config.hpp"

Cfg& Cfg::get() {
    static Cfg c;
    static bool loaded = false;
    if (!loaded) {
        loaded = true;
        c.load();
    }
    return c;
}

void Cfg::load() {
    auto m = geode::Mod::get();
#define X(n, d) n = m->getSavedValue<decltype(n)>(#n, d);
    KT_BOOLS(X)
    KT_FLOATS(X)
    KT_INTS(X)
#undef X
}

void Cfg::save() const {
    auto m = geode::Mod::get();
#define X(n, d) m->setSavedValue<decltype(n)>(#n, n);
    KT_BOOLS(X)
    KT_FLOATS(X)
    KT_INTS(X)
#undef X
    // Write to disk now. Otherwise settings only reach the disk when the game closes normally - on a phone the
    // game is usually swiped away, so changes (like TPS back to 240) were lost and the old value came back.
    (void)m->saveData();
}
```

## src/Common.hpp

```cpp
#pragma once
#include <Geode/Geode.hpp>
#include <string>
#include <vector>
#include "Config.hpp"

// Public surface of every feature module. Hooks live in Hooks.cpp and only call these.
namespace kt {

namespace hitbox {
    void enter();              // new level: forget cached objects and nodes
    void update(PlayLayer*);   // draws hitboxes / borders / trail each frame
    void onTick(PlayLayer*);   // once per physics tick: adds one trail box
    void reset();              // clears trail history (on every death / restart)
}

namespace path {
    void update(PlayLayer*);   // pathfinder: simulates a dummy player forward
    bool simulating();         // true while the dummy is being stepped
    void onSimDeath(GameObject* obj);
    void reset();
    void forget();             // the level is already gone: drop the dummy without touching it
}

namespace noclip {
    void onHit();              // called instead of dying
    void onStep();
    float accuracy();          // 0..100
    void reset();
}

namespace speed {
    float factor();            // total time scale: speedhack * counting speed (used by the scheduler hook)
    void updatePitch();        // music pitch follows the speedhack
    int planSteps(float dt, float& step);   // physical TPS: how many physics updates of `step` seconds to run
    void onStep();             // counts physics steps for the TPS readout
    float measuredTps();
}

namespace practice {
    void sync(PlayLayer*);     // re-seek music after a checkpoint respawn
}

namespace layout {
    void apply(PlayLayer*);    // layout mode: hide decoration, paint hitbox objects white (or undo it)
    void reset();              // forget everything about the current level (leaving / entering a level)
    bool blockTrigger(GameObject*); // layout mode: true = this color / pulse / alpha trigger does nothing
    // every level color change goes through this; false = ignore it (layout mode keeps bg/ground blue)
    bool filterColor(int colorID, cocos2d::ccColor3B& color, float& fadeTime, int& copyID);
}

namespace counter {
    struct Input {
        int frame;
        bool down;
        bool p1;
    };
    struct Result {
        int frame = 0;           // the press
        int early = 0;
        int late = 0;
        bool counted = false;
        int relFrame = -1;       // its release (-1 = held to the end)
        int relEarly = 0;
        int relLate = 0;
        bool relCounted = false;
        int window() const { return early + late + 1; }
        int relWindow() const { return relEarly + relLate + 1; }
    };
    struct Cue {                 // one counted press or release, as shown in the level
        int frame;
        int window;
        bool release;
        cocos2d::CCPoint pos;    // where it happened in the level
    };
    struct TrialLine {
        std::vector<cocos2d::CCPoint> pts;   // path of one counting trial
        bool ok;                              // survived (white) or died (red)
    };

    enum class Mode { Disabled = 0, Record = 1, Playback = 2 };

    void onLevelEnter(PlayLayer*);
    void onLevelExit();                                            // stop counting, free the saved checkpoints
    bool filterInput(bool down, int button, bool p1);              // false = swallow the real input
    void onInput(GJBaseGameLayer*, bool down, int button, bool p1);
    void beforeStep(GJBaseGameLayer*);                             // inject macro / show cues
    void afterStep();                                              // the physics step is over
    void onStoreCheckpoint(PlayLayer*, CheckpointObject*);         // practice checkpoint placed
    void onLoadCheckpoint(PlayLayer*, CheckpointObject*);          // practice respawn (before onReset)
    bool onDeath(PlayLayer*, GameObject* obj);                     // true = swallow the death
    bool onComplete(PlayLayer*);                                   // true = swallow level complete
    void onReset(PlayLayer*);                                      // after a reset AND after a checkpoint load
    void update(PlayLayer*);                                       // state machine (finish a run / respawn)
    bool pendingReset();                                           // a respawn for the next run is waiting

    Mode mode();
    void setMode(Mode);
    void startCounting();
    void cancelCounting();
    void abortCounting(std::string const& why);   // stop counting and show why
    float speedFactor();         // always 1 (counting runs many steps per frame instead, see Hooks.cpp)
    bool isRecording();
    bool isCounting();
    bool botPlaying();           // Playback mode with a replay: the mod is clicking, not the player
    std::string statusText();
    std::string summaryText();
    std::string countingText();  // "Counting.. 10/26\nETA ~51s"
    std::string macroInfo();     // "Loaded  -  30 inputs at 240 TPS"
    std::string infoText();      // everything, for Copy Info

    // data for the on-level overlay
    std::vector<Result> const& results();
    std::vector<Cue> const& cues();          // finished windows in frame order (shown one by one while playing)
    std::vector<Cue> countedCues();          // everything counted so far (grows while counting)
    size_t cueCursor();                      // how many cues the player has reached in this attempt
    int lastCue();                           // index in cues() of the last reached one, -1 = none
    bool baselineRunning();                  // the first (path recording) run of a count
    std::vector<cocos2d::CCPoint> const& pathPoints();
    std::vector<TrialLine> const& trialLines();
    std::vector<cocos2d::CCPoint> const& currentTrialPath();
    int version();               // changes whenever the overlay must be redrawn
    int trialSerial();           // changes whenever a counting trial finished (trial lines must be redrawn)

    // replay files (each returns a short message for the user)
    std::string currentName();
    void selectReplay(int delta);
    std::string newReplay(std::string const& name);
    std::string saveReplay();
    std::string loadReplay();
    std::string deleteReplay();
    void openReplaysFolder();
}

namespace view {
    void reset();              // new level: forget cached nodes
    void update(PlayLayer*);   // window counter overlay: path, donut rings, window numbers, stats
}

namespace hud {
    void attach(PlayLayer*);
    void detach();             // the level is going away: forget its labels
    void update(PlayLayer*);
    void flash(std::string const& text);
}

namespace ui {
    void open();                 // opens the toolkit menu
    bool isOpen();
    void spawnButton();          // creates the floating, draggable menu button (shown in every scene)
    void refreshButton();        // re-applies size after the slider changed
    cocos2d::CCSprite* modSprite(char const* name);   // one of this mod's own images
}

} // namespace kt
```

## src/PlayerState.hpp

```cpp
#pragma once
// The physics state of a PlayerObject that the game's own checkpoints do NOT keep.
// GD checkpoints only keep part of the player (position, speed, gamemode...). Everything else - y velocity
// precision, ground / slope state, jump buffer, held buttons, dash, collision bookkeeping - stays whatever it was,
// so a run from a checkpoint is not the same as the recorded run. Bots (xdBot, Antic's Counter) save these fields
// and put them back after loading a checkpoint. Used by the window counter (checkpoints) and the pathfinder
// (exact copy of the real player into the preview dummy).
#include <Geode/Geode.hpp>
#include <type_traits>

#define KT_PLAYER_FIELDS(X) \
    X(m_wasTeleported) X(m_fixGravityBug) X(m_reverseSync) X(m_yVelocityBeforeSlope) X(m_dashX) X(m_dashY) \
    X(m_dashAngle) X(m_dashStartTime) X(m_dashRing) X(m_slopeStartTime) X(m_maybeLastGroundObject) \
    X(m_lastCollisionBottom) X(m_lastCollisionTop) X(m_lastCollisionLeft) X(m_lastCollisionRight) \
    X(m_prevCollisionBottom) X(m_prevCollisionTop) X(m_currentSlope2) X(m_preLastGroundObject) X(m_slopeAngle) \
    X(m_slopeSlidingMaybeRotated) X(m_collidedObject) X(m_lastGroundObject) X(m_collidingWithLeft) \
    X(m_collidingWithRight) X(m_maybeSavedPlayerFrame) X(m_scaleXRelated2) X(m_groundYVelocity) \
    X(m_yVelocityRelated) X(m_scaleXRelated3) X(m_collidingBetweenSteps) X(m_scaleXRelated4) X(m_scaleXRelated5) \
    X(m_isCollidingWithSlope) X(m_isBallRotating) X(m_noPotentialSlope) X(m_currentPotentialSlope) \
    X(m_currentSlope) X(m_slopeCenterOffset) X(m_collidingWithSlopeId) X(m_slopeIsDownhill) X(m_slopeAngleRadians) \
    X(m_rotateObjectsRelated) X(m_potentialSlopeMap) X(m_rotationSpeed) X(m_rotateSpeed) X(m_isRotating) \
    X(m_ballSpinning) X(m_speedMultiplier) X(m_yStart) X(m_gravity) X(m_gameModeChangedTime) \
    X(m_ringJumpedSincePress) X(m_hasStartedFalling) X(m_landedSinceFlip) X(m_lastJumpTime) X(m_lastFlipTime) \
    X(m_lastSpiderFlipTime) X(m_accelerationOrSpeed) X(m_snapDistance) X(m_usedRingThisStep) X(m_usedRings) \
    X(m_objectSnappedTo) X(m_slopeRotation) X(m_currentSlopeYVelocity) X(m_lastSlopeSnapY) \
    X(m_lastSlopeSnapOffset) X(m_lastSlopeSnapAtFarEdge) X(m_lastSlopeSnapAtNearEdge) X(m_isAccelerating) \
    X(m_isCurrentSlopeTop) X(m_collidedTopMinY) X(m_collidedBottomMaxY) X(m_collidedLeftMaxX) \
    X(m_collidedRightMinX) X(m_maybeIsColliding) X(m_jumpBuffered) X(m_stateRingJump) X(m_wasJumpBuffered) \
    X(m_wasRobotJump) X(m_stateJumpBuffered) X(m_statePreJump) X(m_touchedRing) X(m_touchedCustomRing) \
    X(m_touchedGravityPortal) X(m_brokeBlockSincePress) X(m_touchedPad) X(m_yVelocity) X(m_fallSpeed) \
    X(m_isOnSlope) X(m_wasOnSlope) X(m_slopeVelocity) X(m_slopeResnapped) X(m_isShip) X(m_isBird) X(m_isBall) \
    X(m_isDart) X(m_isRobot) X(m_isSpider) X(m_isUpsideDown) X(m_isOnGround) X(m_isGoingLeft) X(m_isSideways) \
    X(m_isSwing) X(m_reverseCooldown) X(m_maybeReverseSpeed) X(m_reverseSyncStep) X(m_ballRollSpeed) \
    X(m_isDashing) X(m_dashFireFrame) X(m_groundObjectMaterial) X(m_vehicleSize) X(m_playerSpeed) \
    X(m_shipRotation) X(m_lastPortalPos) X(m_touchingGround) X(m_lastLandTime) X(m_decelerationRamp) \
    X(m_isRising) X(m_lastGroundXDeltaTime) X(m_decreaseBoostSlide) X(m_isLocked) X(m_controlsDisabled) \
    X(m_lastGroundedPos) X(m_touchedRings) X(m_lastActivatedPortal) X(m_hasEverJumped) X(m_hasEverHitRing) \
    X(m_position) X(m_totalTime) X(m_diagonalYStep) X(m_stateOnGround) X(m_stateBoostY) X(m_stateBoostX) \
    X(m_stateNoWallPush) X(m_stateScale) X(m_platformerXVelocity) X(m_holdingRight) X(m_holdingLeft) \
    X(m_leftPressedFirst) X(m_platformXMove) X(m_hasPlatformMomentum) X(m_platformMomentum) \
    X(m_forcedSlopeSlide) X(m_isSliding) X(m_slopeSpeedBonus) X(m_isOnIce) X(m_platformRideTime) \
    X(m_wasTouchingGround) X(m_wasCollidingTopOrBottom) X(m_slopeJumpCount) X(m_slopeJumpResetTime) \
    X(m_changedDirectionsTime) X(m_slopeEndTime) X(m_isMoving) X(m_forcedSlideLeft) X(m_forcedSlideRight) \
    X(m_lastSlideLeft) X(m_slideSlopeAngle) X(m_isPlatformer) X(m_stateNoAutoJump) X(m_stateDartSlide) \
    X(m_stateHitHead) X(m_stateFlipGravity) X(m_gravityMod) X(m_stateForce) X(m_stateForceVector) \
    X(m_affectedByForces) X(m_touchedForceIds) X(m_lastMovedTime) X(m_playerSpeedAC) X(m_fixRobotJump) \
    X(m_holdingButtons) X(m_inputsLocked) X(m_isOutOfBounds) X(m_fallStartY) X(m_ignoreDamage) \
    X(m_enable22Changes) X(m_enableImpulseFix)

namespace kt {

struct PState {
#define X(f) std::remove_cvref_t<decltype(PlayerObject::f)> f{};
    KT_PLAYER_FIELDS(X)
#undef X
    cocos2d::CCPoint pos;
    float rot = 0.f;
    bool valid = false;
};

inline void savePlayer(PlayerObject* p, PState& st) {
    st.valid = p != nullptr;
    if (!p) return;
#define X(f) st.f = p->f;
    KT_PLAYER_FIELDS(X)
#undef X
    st.pos = p->getPosition();
    st.rot = p->getRotation();
}

inline void loadPlayer(PlayerObject* p, PState const& st) {
    if (!p || !st.valid) return;
#define X(f) p->f = st.f;
    KT_PLAYER_FIELDS(X)
#undef X
    p->m_isDead = false;
    p->setPosition(st.pos);
    p->setRotation(st.rot);
}

// dst becomes an exact physics copy of src (used for the pathfinder dummy)
inline void copyPlayer(PlayerObject* dst, PlayerObject* src) {
    if (!dst || !src) return;
#define X(f) dst->f = src->f;
    KT_PLAYER_FIELDS(X)
#undef X
    dst->m_isDead = false;
    dst->setPosition(src->getPosition());
    dst->setRotation(src->getRotation());
}

} // namespace kt
```

## src/Hooks.cpp

```cpp
// Every hook of the mod lives here (one $modify per class) so two features never hook the same function twice.
//
// Hook order: the hooks the window counter / bot depend on run FIRST, before other mods (Silicate, QOLMod,
// other bots and counters hook the same functions). Otherwise, for example, another mod's noclip swallowed the
// death before the counter ever saw it, or another practice fix overwrote the player after our respawn.
//
// Counting safety: while counting, deaths are swallowed and your own clicks are blocked (the counter clicks
// and decides itself). That is only allowed while the counter is REALLY driving the level. Before, the level
// could lose its "ready" mark while counting was on: nothing was counted, but deaths were still swallowed -
// the player just ran through every object. Now the ready mark repairs itself, and if counting still cannot
// run it is stopped right away (with the reason in the counter tab) instead of turning into noclip.
#include "Common.hpp"
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/modify/CCScheduler.hpp>
#include <Geode/modify/EffectGameObject.hpp>
#include <Geode/modify/GradientTriggerObject.hpp>

#include <chrono>

using namespace geode::prelude;

namespace {
    // true while the window counter runs many game steps inside one rendered frame: the overlays are then
    // drawn once after the burst instead of after every step
    bool g_fastForward = false;
    struct FastForward {
        FastForward() { g_fastForward = true; }
        ~FastForward() { g_fastForward = false; }     // never stays on, whatever happens inside the burst
    };

    // the level that finished loading and has not been left yet
    PlayLayer* g_setup = nullptr;
    // g_setup while it is on screen. Nothing of the mod draws or simulates outside of it: the game already runs
    // frames while a level is still loading, and touching the previous level's nodes then was a crash.
    PlayLayer* g_ready = nullptr;
    // the level is still alive but another scene was put on top of it (it comes back when that one closes)
    PlayLayer* g_away = nullptr;

    int g_steps = 0;          // physics steps run so far (to see whether a counting burst moved the game at all)
    int g_idleBursts = 0;     // counting bursts in a row that ran no physics step

    // The ready mark repairs itself: a level that finished loading, was never left and is on screen is ready.
    PlayLayer* ready(PlayLayer* pl) {
        if (pl && g_ready != pl && g_setup == pl && pl->isRunning()) {
            g_ready = pl;
            g_away = nullptr;
        }
        return g_ready;
    }

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
        g_setup = nullptr;
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
            if (!m_owner || (g_ready != m_owner && g_away != m_owner && g_setup != m_owner)) return;
            kt::counter::onLevelExit();
            kt::path::forget();       // never touch the dummy: its level is being destroyed right now
            kt::hitbox::enter();
            kt::view::reset();
            kt::hud::detach();
            kt::layout::reset();
            g_ready = nullptr;
            g_away = nullptr;
            g_setup = nullptr;
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
        g_setup = this;
        g_ready = this;
        g_away = nullptr;
        g_idleBursts = 0;
    }

    void onQuit() {
        kt::counter::cancelCounting();     // never leave the game running at counting speed
        if (g_setup == this || g_ready == this || g_away == this) leaveLevel();
        PlayLayer::onQuit();
    }

    // Leaving the level any other way than the pause menu (another mod replacing the scene...) cleans up.
    // A scene pushed ON TOP of the level (it stays alive underneath) only pauses the mod until it comes back.
    void onExit() {
        if (g_ready == this || g_setup == this) {
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
        if (ready(this) != this) return;    // level still loading / already left: touch nothing
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
        // counting that is not really running must never act like noclip
        if (kt::counter::isCounting() && ready(this) != this)
            kt::counter::abortCounting("Stopped - the level was not ready for counting, try again");
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
        ++g_steps;
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
        int stepsBefore = g_steps;
        {
            FastForward ff;
            for (int i = 0; i < 4000; ++i) {
                kt::counter::update(pl);                     // finish the run / respawn for the next one
                if (!kt::counter::isCounting()) break;
                if (kt::counter::pendingReset()) continue;
                GJBaseGameLayer::update(dt);
                if (std::chrono::steady_clock::now() - t0 > std::chrono::milliseconds(14)) break;
            }
        }
        // the game must actually step while counting; if another mod stops it, say so instead of hanging
        if (kt::counter::isCounting()) {
            if (g_steps == stepsBefore) {
                if (++g_idleBursts > 120) {
                    g_idleBursts = 0;
                    kt::counter::abortCounting("Stopped - the game ran no physics steps (another mod's bot / "
                                               "frame stepper on?)");
                }
            } else {
                g_idleBursts = 0;
            }
        }
        kt::hitbox::update(pl);
        kt::view::update(pl);
        kt::hud::update(pl);
    }

    // Physical TPS: run the game's own update() several times with dt = 1 / TPS (see Cheats.cpp).
    void update(float dt) {
        auto& c = Cfg::get();
        auto pl = PlayLayer::get();
        bool mine = pl && static_cast<GJBaseGameLayer*>(pl) == this;
        if (mine && kt::counter::isCounting() && !g_fastForward) {
            if (ready(pl) == pl) {
                countBurst(pl);
                return;
            }
            // counting without a ready level would only block clicks and deaths: stop it
            kt::counter::abortCounting("Stopped - the level was not ready for counting, try again");
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

// Layout mode: color / pulse / alpha triggers stay in the level but do nothing when they fire.
class $modify(KtEffectObject, EffectGameObject) {
    void triggerObject(GJBaseGameLayer* layer, int uniqueID, std::vector<int> const* remapKeys) {
        if (kt::layout::blockTrigger(this)) return;
        EffectGameObject::triggerObject(layer, uniqueID, remapKeys);
    }
};

class $modify(KtGradientObject, GradientTriggerObject) {
    void triggerObject(GJBaseGameLayer* layer, int uniqueID, std::vector<int> const* remapKeys) {
        if (kt::layout::blockTrigger(this)) return;
        GradientTriggerObject::triggerObject(layer, uniqueID, remapKeys);
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
```

## src/Counter.cpp

```cpp
// Window frame counter + replay files.
//
// Modes (like a bot menu):  Disabled / Record / Playback
//  - Record:   choose it and resume - the level restarts by itself and every click is stored. Practice mode is
//              fine: when you respawn at a checkpoint, the replay is cut back to that frame and continues.
//              Finish the level to end the recording.
//  - Playback: the replay is played back by the mod, on exactly the frames it was recorded on.
//              "Ignore Inputs" blocks your own clicks meanwhile.
//  - Count:    1) one normal run of the replay from the level start. It records the player's path, where every
//                 click and release happened, and saves a checkpoint a few frames before every click.
//                 If the replay dies somewhere, the clicks before that point are still counted.
//              2) for every click: respawn at the checkpoint before it and
//                   - first replay it UNCHANGED (control run). If that does not survive, the checkpoint is not
//                     exact: the frame offset is re-calibrated (0, +1, -1), and if it still fails this click is
//                     counted with full level restarts instead. This is what guarantees correct windows - before,
//                     a broken checkpoint made every shift die, so every click showed "1".
//                   - shift the click (with its release) earlier / later by 1,2,3... frames  -> click window
//                   - shift only the release earlier / later                                 -> release window
//                 A shift "works" if the player survives until the NEXT click. window = early + late + 1.
//                 The respawn goes through the game's own practice respawn (resetLevel with our checkpoint),
//                 so every object, orb, trigger and moving block is reset exactly like a real respawn.
//              3) when done, the level restarts from the beginning.
//   Speed: while counting, the game is stepped as many times per rendered frame as fit into ~14 ms (only one
//   picture is drawn per frame), so counting runs as fast as the device allows.
//  - Play:     while you (or the bot) play, every click / release shows its window one by one as you reach it.
// Replays are text files in <save dir>/replays/<name>.ktr:
//   1 inputs "frame:down:p1;"            2 click results "frame:early:late:counted;"
//   3 click positions "x,y;"             4 path "x,y;"
//   5 release results "early:late:counted;"   6 release positions "x,y;"
#include "Common.hpp"
#include "PlayerState.hpp"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <map>
#include <sstream>
#include <type_traits>

using namespace geode::prelude;

namespace kt::counter {

namespace {
    constexpr int kMaxSide = 12;         // search at most 12 frames each way -> window up to 25f
    constexpr float kNoPos = -1e9f;
    constexpr size_t kMaxLines = 16;     // trial lines kept on screen for the current click
    constexpr int kCalib[3] = {0, 1, -1};   // frame offsets tried when the control run from a checkpoint fails

    enum class Phase { Idle, Baseline, Trial };
    // sides of one click: 0 click earlier, 1 click later, 2 release earlier, 3 release later, 4 control (unchanged)
    enum Side { DownEarly = 0, DownLate = 1, RelEarly = 2, RelLate = 3, Control = 4 };

    constexpr int kSurvive = 120;        // a shifted run must also survive this many ticks after the input

    struct Click {
        int down = 0;        // index into macro
        int rel = -1;        // index of the matching release (-1 = held to the end)
        int checkFrame = 0;  // surviving until here = success (next click, and at least kSurvive ticks)
        int nextPress = 0;   // frame of the next press (inputs may not be moved past it)
    };

    // full player state saved with every checkpoint: see PlayerState.hpp

    struct S {
        Mode mode = Mode::Disabled;
        std::vector<Input> macro;
        std::vector<Click> clicks;
        std::vector<Result> results;
        std::vector<Cue> cues;               // finished windows in frame order (live display)
        size_t cueCursor = 0;                // cues already reached in this attempt
        int lastCue = -1;
        std::vector<int> downToClick;        // macro index -> click index (-1 = not a click press)
        std::vector<int> relToClick;         // macro index -> click index (-1 = not a click release)
        std::vector<CCPoint> clickPos;       // where each press happened (from the counting run)
        std::vector<CCPoint> relPos;         // where each release happened
        std::vector<CCPoint> path;           // player path from the counting run
        int lastPathFrame = -1;
        int version = 0;                     // bumps whenever the overlay must redraw
        int trialSerial = 0;                 // bumps whenever the finished trial lines change

        std::vector<CheckpointObject*> cps;  // retained checkpoint a few frames before each click
        std::vector<int> cpFrame;            // planned frame of each checkpoint
        std::vector<int> cpAt;               // frame it was really taken on
        std::vector<PState> cpP1, cpP2;      // full player state at that moment (see PState)
        int pendingIdx = -1;                 // which checkpoint is being loaded
        std::vector<char> relMatters;        // per click: the release changes the path (ship, wave, robot)
        // per click: -1 not tested, 1 checkpoint replays exactly, 0 checkpoint broken -> testing a full restart,
        //            2 full restart verified, 3 not reproducible at all (skipped, shown as "?")
        std::vector<signed char> cpOk;
        int broken = 0;                      // clicks that could not be reproduced
        std::vector<std::string> notes;      // per click: how it was counted (Copy Info)
        int lastDeathFrame = -1, lastDeathId = -1;
        std::string lastTrial;               // shown under the counting text
        size_t nextCp = 0;
        int frameShift = 0;                  // added to the game's frame counter after a checkpoint respawn
        int pendingCp = -1;                  // frame of the checkpoint that is being loaded right now
        int calibIdx = 0;                    // which kCalib value the control run uses
        int bestShift = 0;                   // the calibration that worked last (tried first for the next click)
        int fallbacks = 0;                   // clicks that had to be counted with full restarts
        bool reached = false;                // the current run survived until its check frame
        std::vector<CCPoint> trialPath;      // path of the running trial
        std::vector<TrialLine> lines;        // finished trials of the current click

        std::vector<std::string> names;      // replay files on disk
        int sel = 0;

        bool armed = false, recording = false;
        Phase phase = Phase::Idle;
        bool needReset = false, trialDead = false, completed = false, injecting = false;
        bool restartAfter = false;           // put the player back at the level start on the next frame
        int stepFrame = 0;                   // frame number of the physics step being processed right now
        bool inStep = false;                 // inside a physics step right now (between beforeStep and afterStep)
        // clicks that came in between two physics steps: they belong to the NEXT step and get its frame there.
        // Before, they got the frame of the step before, so the bot pressed one tick too early.
        std::vector<std::pair<bool, bool>> pendingRec;    // {down, p1}

        // practice fix while recording: the full player state of every practice checkpoint, put back when you
        // respawn there - like a continuous run, so the recorded bot replays it the same way from the start
        std::map<CheckpointObject*, std::pair<PState, PState>> practice;
        CheckpointObject* practiceCp = nullptr;   // checkpoint loaded by the respawn being processed right now
        std::string levelName;               // name of the level being played (new recordings are named after it)

        std::vector<Input> trial;            // what is injected (macro for playback, shifted macro for counting)
        size_t cursor = 0;
        int checkFrame = 0;
        int k = 0, dir = 0, off = 0;
        int limit = 0;                       // clicks that can be counted (all, or those before the replay dies)
        int deathFrame = 0;
        std::chrono::steady_clock::time_point t0;
        std::string msg = "Idle";
    } s;

    bool active() { return s.phase != Phase::Idle; }
    bool playing() { return s.mode == Mode::Playback && !s.macro.empty() && !active(); }
    int rawFrame(GJBaseGameLayer* l) { return static_cast<int>(l->m_gameState.m_currentProgress); }
    int frameOf(GJBaseGameLayer* l) { return rawFrame(l) + (s.phase != Phase::Idle ? s.frameShift : 0); }
    bool hasPos(CCPoint const& p) { return p.x > kNoPos / 2; }

    // frame offset used for the current control run: the one that worked last first, then 0 / +1 / -1
    int calibShift() { return s.calibIdx == 0 ? s.bestShift : kCalib[s.calibIdx]; }

    void syncTrial() { s.trial = s.macro; }
    void syncCursor(int frame) {
        s.cursor = std::lower_bound(s.trial.begin(), s.trial.end(), frame,
                                    [](Input const& i, int f) { return i.frame < f; }) - s.trial.begin();
    }
    void syncCues(int frame) {
        s.cueCursor = std::lower_bound(s.cues.begin(), s.cues.end(), frame,
                                       [](Cue const& c, int f) { return c.frame < f; }) - s.cues.begin();
        s.lastCue = static_cast<int>(s.cueCursor) - 1;
    }

    void releaseCps() {
        for (auto cp : s.cps) if (cp) cp->release();
        s.cps.clear();
        s.cpFrame.clear();
        s.cpAt.clear();
        s.cpP1.clear();
        s.cpP2.clear();
        s.cpOk.clear();
        s.nextCp = 0;
        s.pendingIdx = -1;
        s.frameShift = 0;
        s.pendingCp = -1;
    }

    // ---------- (de)serialization ----------
    // calls f(a, b, c, d) for every "a:b:c[:d];" entry (d = -1 when missing)
    template <class F>
    void parse(std::string const& str, F&& f) {
        size_t pos = 0;
        while (pos < str.size()) {
            auto end = str.find(';', pos);
            if (end == std::string::npos) break;
            int a, b, c, d = -1;
            if (std::sscanf(str.substr(pos, end - pos).c_str(), "%d:%d:%d:%d", &a, &b, &c, &d) >= 3) f(a, b, c, d);
            pos = end + 1;
        }
    }

    template <class F>
    void parsePts(std::string const& str, F&& f) {
        size_t pos = 0;
        while (pos < str.size()) {
            auto end = str.find(';', pos);
            if (end == std::string::npos) break;
            float x, y;
            if (std::sscanf(str.substr(pos, end - pos).c_str(), "%f,%f", &x, &y) == 2) f(x, y);
            pos = end + 1;
        }
    }

    // every counted press and release, in counting order (press of click 0, its release, press of click 1 ...)
    std::vector<Cue> collectCues() {
        std::vector<Cue> out;
        for (size_t i = 0; i < s.results.size(); ++i) {
            auto& r = s.results[i];
            if (r.counted) out.push_back({r.frame, r.window(), false, i < s.clickPos.size() ? s.clickPos[i] : CCPoint(kNoPos, kNoPos)});
            if (r.relCounted && r.relFrame >= 0)
                out.push_back({r.relFrame, r.relWindow(), true, i < s.relPos.size() ? s.relPos[i] : CCPoint(kNoPos, kNoPos)});
        }
        return out;
    }

    void buildCues() {
        s.cues = collectCues();
        std::stable_sort(s.cues.begin(), s.cues.end(), [](Cue const& a, Cue const& b) { return a.frame < b.frame; });
        s.cueCursor = 0;
        s.lastCue = -1;
    }

    void buildClicks() {
        s.clicks.clear();
        int last = s.macro.empty() ? 0 : s.macro.back().frame;
        for (int i = 0; i < static_cast<int>(s.macro.size()); ++i) {
            auto& in = s.macro[i];
            if (!in.down) continue;
            Click c;
            c.down = i;
            c.nextPress = last + 90;
            for (int j = i + 1; j < static_cast<int>(s.macro.size()); ++j) {
                if (c.rel < 0 && !s.macro[j].down && s.macro[j].p1 == in.p1) c.rel = j;
                if (s.macro[j].down) { c.nextPress = s.macro[j].frame; break; }
            }
            // a shift only works if the run survives until the next click AND a while after the input
            // (the recorded inputs keep playing meanwhile)
            c.checkFrame = std::max(c.nextPress, in.frame + kSurvive);
            s.clicks.push_back(c);
        }
        s.results.assign(s.clicks.size(), Result{});
        s.downToClick.assign(s.macro.size(), -1);
        s.relToClick.assign(s.macro.size(), -1);
        for (size_t i = 0; i < s.clicks.size(); ++i) {
            auto& c = s.clicks[i];
            s.results[i].frame = s.macro[c.down].frame;
            s.results[i].relFrame = c.rel >= 0 ? s.macro[c.rel].frame : -1;
            s.downToClick[c.down] = static_cast<int>(i);
            if (c.rel >= 0) s.relToClick[c.rel] = static_cast<int>(i);
        }
        s.clickPos.assign(s.clicks.size(), CCPoint(kNoPos, kNoPos));
        s.relPos.assign(s.clicks.size(), CCPoint(kNoPos, kNoPos));
    }

    std::string encode() {
        std::ostringstream o;
        char b[64];
        for (auto& i : s.macro) o << i.frame << ':' << i.down << ':' << i.p1 << ';';
        o << '\n';
        for (auto& x : s.results) o << x.frame << ':' << x.early << ':' << x.late << ':' << (x.counted ? 1 : 0) << ';';
        o << '\n';
        for (auto& p : s.clickPos) { std::snprintf(b, sizeof b, "%.1f,%.1f;", p.x, p.y); o << b; }
        o << '\n';
        for (auto& p : s.path) { std::snprintf(b, sizeof b, "%.1f,%.1f;", p.x, p.y); o << b; }
        o << '\n';
        for (auto& x : s.results) o << x.relEarly << ':' << x.relLate << ':' << (x.relCounted ? 1 : 0) << ';';
        o << '\n';
        for (auto& p : s.relPos) { std::snprintf(b, sizeof b, "%.1f,%.1f;", p.x, p.y); o << b; }
        o << '\n';
        return o.str();
    }

    void decode(std::string const& text) {
        std::vector<std::string> lines;
        std::string cur;
        for (char ch : text) {
            if (ch == '\n') { lines.push_back(cur); cur.clear(); }
            else if (ch != '\r') cur += ch;
        }
        if (!cur.empty()) lines.push_back(cur);
        while (lines.size() < 6) lines.emplace_back();

        s.macro.clear();
        parse(lines[0], [](int f, int d, int p, int) { s.macro.push_back({f, d != 0, p != 0}); });
        buildClicks();
        size_t i = 0;
        parse(lines[1], [&](int f, int e, int l, int counted) {
            if (i < s.results.size() && s.results[i].frame == f) {
                s.results[i].early = e;
                s.results[i].late = l;
                s.results[i].counted = counted < 0 ? (e + l > 0) : counted != 0;
            }
            ++i;
        });
        size_t j = 0;
        parsePts(lines[2], [&](float x, float y) {
            if (j < s.clickPos.size()) s.clickPos[j] = CCPoint(x, y);
            ++j;
        });
        s.path.clear();
        parsePts(lines[3], [](float x, float y) { s.path.push_back(CCPoint(x, y)); });
        size_t r = 0;
        parse(lines[4], [&](int e, int l, int counted, int) {
            if (r < s.results.size()) {
                s.results[r].relEarly = e;
                s.results[r].relLate = l;
                s.results[r].relCounted = counted != 0 && s.results[r].relFrame >= 0;
            }
            ++r;
        });
        size_t q = 0;
        parsePts(lines[5], [&](float x, float y) {
            if (q < s.relPos.size()) s.relPos[q] = CCPoint(x, y);
            ++q;
        });
        buildCues();
        syncTrial();
        s.cursor = 0;
        ++s.version;
    }

    // ---------- files ----------
    asp::fs::Path replaysDir() {
        auto d = Mod::get()->getSaveDir();
        d /= "replays";
        (void)utils::file::createDirectoryAll(d);
        return d;
    }
    asp::fs::Path replayPath(std::string const& name) {
        auto p = replaysDir();
        p /= (name + ".ktr");
        return p;
    }

    void refreshList() {
        std::string keep = (s.sel >= 0 && s.sel < static_cast<int>(s.names.size())) ? s.names[s.sel] : "";
        if (keep.empty()) keep = Mod::get()->getSavedValue<std::string>("replayName", "");
        s.names.clear();
        auto r = utils::file::readDirectory(replaysDir());
        if (r.isOk()) {
            for (auto& p : r.unwrap()) {
                auto ext = p.extension();
                auto stem = p.stem();
                if (ext && stem && *ext == "ktr") s.names.push_back(std::string(*stem));
            }
        }
        std::sort(s.names.begin(), s.names.end());
        s.sel = 0;
        for (int i = 0; i < static_cast<int>(s.names.size()); ++i) if (s.names[i] == keep) s.sel = i;
    }

    bool hasSelection() { return s.sel >= 0 && s.sel < static_cast<int>(s.names.size()); }

    void autosave() {
        if (!hasSelection()) return;
        (void)utils::file::writeString(replayPath(s.names[s.sel]), encode());
    }

    // No replay chosen with "New": make one named after the level, so a recording is never lost.
    void ensureReplayFile() {
        refreshList();
        if (hasSelection()) return;
        // the level name is remembered on entering (the level may already be going away here)
        std::string base = s.levelName.empty() ? std::string("replay") : s.levelName;
        std::string name;
        for (char ch : base) {
            if (std::isalnum(static_cast<unsigned char>(ch)) || ch == ' ' || ch == '_' || ch == '-') name += ch;
        }
        if (name.empty()) name = "replay";
        if (name.size() > 18) name.resize(18);
        std::string unique = name;
        for (int i = 2; std::find(s.names.begin(), s.names.end(), unique) != s.names.end(); ++i)
            unique = name + " " + std::to_string(i);
        (void)utils::file::writeString(replayPath(unique), "");
        Mod::get()->setSavedValue<std::string>("replayName", unique);
        refreshList();
    }

    // Ends a recording and ALWAYS saves it - when the level is completed, but also when you quit the level,
    // switch the mode, or never finished (practice runs usually never reach 100%). Before, only a completed
    // level with a replay created by "New" was saved, so many recordings were simply lost.
    bool g_forceArm = false;     // the restart done by Record mode itself starts the recording, whatever frame
    void finishRecording() {
        if (!s.recording) { s.armed = false; return; }
        s.recording = false;
        s.armed = false;
        if (s.macro.empty()) { s.msg = "Nothing recorded"; return; }
        buildClicks();
        buildCues();
        s.path.clear();
        ensureReplayFile();
        autosave();
        syncTrial();
        ++s.version;
        s.msg = "Recorded " + std::to_string(s.clicks.size()) + " clicks - saved as \"" +
                (hasSelection() ? s.names[s.sel] : std::string("?")) + "\"";
    }

    // ---------- trial construction ----------
    // click = -1 builds the untouched macro (baseline). Returns false if the shift is impossible
    // (it would reorder inputs, or the release would come before its press / after the next click).
    bool makeTrial(int click, int off, int side) {
        s.trial = s.macro;
        if (click < 0) {
            s.checkFrame = (s.macro.empty() ? 0 : s.macro.back().frame) + kSurvive + 10;
            return true;
        }
        auto& c = s.clicks[click];
        if (side == Control) {               // unchanged inputs from this click's checkpoint
            s.checkFrame = c.checkFrame;
            return true;
        }
        bool releaseOnly = side >= RelEarly;
        if (releaseOnly) {
            if (c.rel < 0) return false;
            s.trial[c.rel].frame += off;
            if (s.trial[c.rel].frame <= s.trial[c.down].frame) return false;
            if (s.trial[c.rel].frame >= c.nextPress) return false;
        } else {
            s.trial[c.down].frame += off;
            if (c.rel >= 0) s.trial[c.rel].frame += off;
            if (s.trial[c.down].frame >= c.nextPress) return false;
            // the respawn point is before the earliest press we try; never move a press in front of it
            if (s.cpOk[click] == 1 && s.trial[c.down].frame < s.cpFrame[click]) return false;
        }
        for (size_t i = 0; i < s.trial.size(); ++i) {
            if (s.trial[i].frame < 1) return false;
            if (i > 0 && s.trial[i].frame < s.trial[i - 1].frame) return false;
        }
        s.checkFrame = c.checkFrame;
        return true;
    }

    void advanceClick() {
        ++s.k;
        s.dir = Control;
        s.off = 0;
        s.calibIdx = 0;
        s.lines.clear();
        ++s.trialSerial;
    }

    // move on to the next side of the current click, or to the next click
    void nextSide() {
        auto& r = s.results[s.k];
        switch (s.dir) {
            case DownEarly: s.dir = DownLate; s.off = 1; break;
            case DownLate:
                r.counted = true;          // the click window is final: it appears on screen now
                ++s.version;
                if (s.clicks[s.k].rel >= 0 && s.k < static_cast<int>(s.relMatters.size()) && s.relMatters[s.k]) {
                    s.dir = RelEarly;
                    s.off = -1;
                } else {
                    advanceClick();
                }
                break;
            case RelEarly: s.dir = RelLate; s.off = 1; break;
            default:
                r.relCounted = true;       // the release window is final
                ++s.version;
                advanceClick();
                break;
        }
    }

    void stop(std::string const& why) {
        s.phase = Phase::Idle;       // the counting speed ends with the phase (see speedFactor)
        s.needReset = false;
        // The last trial left the player somewhere in the level (often past the end, where GD never
        // completes again because the counter swallowed it). Restart so the level can be played normally.
        s.restartAfter = true;
        s.msg = why;
        s.reached = false;
        s.lines.clear();
        ++s.trialSerial;
        s.trialPath.clear();
        releaseCps();
        syncTrial();
        buildCues();
        ++s.version;
    }

    void complete() {
        if (Cfg::get().saveOnFinish) {
            buildCues();
            autosave();
        }
        int fb = s.fallbacks, br = s.broken;
        std::string extra;
        if (fb) extra += ", " + std::to_string(fb) + " via restart";
        if (br) extra += ", " + std::to_string(br) + " not reproducible";
        stop("Done - " + std::to_string(s.limit - br) + " clicks counted" + extra);
        FMODAudioEngine::sharedEngine()->playEffect(std::string("ding_9.wav"_spr), 1.f, 1.f, 1.f);
    }

    void scheduleNext() {
        while (s.k < s.limit) {
            if (makeTrial(s.k, s.off, s.dir)) { s.needReset = true; return; }
            // impossible shift (would cross another input) counts as the end of that side
            if (s.dir != Control && s.k < static_cast<int>(s.notes.size()))
                s.notes[s.k] += std::string(s.notes[s.k].empty() ? "" : ", ") + (s.dir <= DownLate ? "" : "rel") +
                                (s.off > 0 ? "+" : "") + std::to_string(s.off) + " blocked";
            nextSide();
        }
        complete();
    }

    void finish(bool ok) {
        if (s.phase == Phase::Baseline) {
            if (!ok) {
                // The unchanged replay itself dies at deathFrame (a macro that does not finish the level, e.g. one
                // recorded only up to some point). Nothing after that point can be survived, so:
                //  - every click before the death is counted,
                //  - no click may demand surviving past that frame. Before, the last clicks asked for "survive
                //    until the next click / 120 ticks", which lay behind the end of the replay - every shift
                //    "died" there and those clicks always showed 1.
                int d = s.deathFrame;
                int usable = 0;
                while (usable < static_cast<int>(s.clicks.size()) && s.macro[s.clicks[usable].down].frame < d - 1)
                    ++usable;
                if (usable == 0) { stop("The replay dies before its first click - record a longer run"); return; }
                s.limit = usable;
                for (auto& c : s.clicks) c.checkFrame = std::min(c.checkFrame, d);
            }
            s.phase = Phase::Trial;
            s.k = 0; s.dir = Control; s.off = 0; s.calibIdx = 0;
        } else if (s.dir == Control) {
            // control run: the UNCHANGED replay must survive until the next click, otherwise nothing measured
            // for this click could be trusted
            auto& st = s.cpOk[s.k];
            std::string died = " (died f" + std::to_string(s.lastDeathFrame) + " obj " + std::to_string(s.lastDeathId) + ")";
            if (ok) {
                if (st == -1) {
                    st = 1;
                    s.bestShift = calibShift();
                    s.notes[s.k] = "checkpoint";
                } else {
                    st = 2;
                    s.notes[s.k] = "restart";
                }
                s.dir = DownEarly;
                s.off = -1;
            } else if (st == -1 && s.calibIdx + 1 < 3) {
                ++s.calibIdx;                // try the checkpoint again with another frame offset
            } else if (st == -1) {
                // the checkpoint cannot reproduce the run: verify a full restart instead (slow but exact)
                st = 0;
                ++s.fallbacks;
                s.notes[s.k] = "checkpoint failed" + died;
            } else {
                // even a full restart does not reproduce the recording here: skip this click
                st = 3;
                ++s.broken;
                s.notes[s.k] = "NOT REPRODUCIBLE" + died;
                s.results[s.k].counted = false;
                s.results[s.k].relCounted = false;
                advanceClick();
            }
            s.lastTrial = "check " + std::string(ok ? "ok" : "FAILED" + died);
        } else {
            s.lastTrial = std::string(s.dir <= DownLate ? "click " : "release ") + (s.off > 0 ? "+" : "") +
                          std::to_string(s.off) + (ok ? " ok" : " died f" + std::to_string(s.lastDeathFrame) +
                          " obj " + std::to_string(s.lastDeathId));
            // Copy Info: where each side of the window ended (relative to the press), to check the counter
            if (!ok) {
                int press = s.macro[s.clicks[s.k].down].frame;
                s.notes[s.k] += std::string(s.notes[s.k].empty() ? "" : ", ") + (s.dir <= DownLate ? "" : "rel") +
                                (s.off > 0 ? "+" : "") + std::to_string(s.off) + " died +" +
                                std::to_string(s.lastDeathFrame - press) + " obj " + std::to_string(s.lastDeathId) +
                                " (need +" + std::to_string(s.clicks[s.k].checkFrame - press) + ")";
            }
            if (!s.trialPath.empty()) {
                s.lines.push_back({s.trialPath, ok});
                if (s.lines.size() > kMaxLines) s.lines.erase(s.lines.begin());
                ++s.trialSerial;
            }
            auto& r = s.results[s.k];
            if (ok) {
                switch (s.dir) {
                    case DownEarly: r.early++; break;
                    case DownLate: r.late++; break;
                    case RelEarly: r.relEarly++; break;
                    default: r.relLate++; break;
                }
                s.off += (s.dir % 2 == 0) ? -1 : 1;
                if (std::abs(s.off) > kMaxSide) nextSide();
            } else {
                nextSide();
            }
        }
        s.trialPath.clear();
        scheduleNext();
    }

    std::string eta(float sec) {
        int t = static_cast<int>(sec + 0.5f);
        if (t >= 60) return "~" + std::to_string(t / 60) + "m " + std::to_string(t % 60) + "s";
        return "~" + std::to_string(t) + "s";
    }
}

// ================= hook-facing API =================

void onLevelEnter(PlayLayer* pl) {
    if (active()) stop("Stopped");
    s.levelName = (pl && pl->m_level) ? std::string(pl->m_level->m_levelName) : std::string();
    s.restartAfter = false;      // a fresh level is already at the start
    s.armed = s.recording = false;
    if (s.mode == Mode::Record) {  // Record chosen before entering: start recording from this level's start
        s.armed = true;
        s.restartAfter = true;
    }
    releaseCps();
    s.practice.clear();
    s.practiceCp = nullptr;
    s.pendingRec.clear();
    s.lines.clear();
    s.trialPath.clear();
    refreshList();
    s.cursor = 0;
    s.cueCursor = 0;
    s.lastCue = -1;
    ++s.version;
}

// The level is being left: the saved checkpoints belong to it, so they are freed while it still exists.
void onLevelExit() {
    if (active()) stop("Stopped");
    s.restartAfter = false;
    finishRecording();           // quitting the level saves what was recorded so far
    s.armed = s.recording = false;
    releaseCps();
    s.practice.clear();
    s.practiceCp = nullptr;
    s.pendingRec.clear();
    s.inStep = false;
    s.lines.clear();
    s.trialPath.clear();
}

bool filterInput(bool, int, bool) {
    if (active()) return s.injecting;
    if (playing() && Cfg::get().ignoreInputs) return s.injecting;
    return true;
}

// Recording and playback use the SAME frame number: the one taken at the start of the physics step
// (beforeStep). Playback presses at that same point of the same step, so the bot repeats the run exactly.
// A click that arrives between two steps (that is how a real mouse / key press usually comes in) only acts on
// the next step, so it is stamped with that step's frame when the step starts.
void onInput(GJBaseGameLayer*, bool down, int button, bool p1) {
    if (!s.recording || s.injecting || button != 1) return;
    if (s.inStep) s.macro.push_back({s.stepFrame, down, p1});
    else s.pendingRec.push_back({down, p1});
}

void afterStep() { s.inStep = false; }

void onStoreCheckpoint(PlayLayer* pl, CheckpointObject* cp) {
    if (!cp || active()) return;
    auto& st = s.practice[cp];
    savePlayer(pl->m_player1, st.first);
    savePlayer(pl->m_gameState.m_isDualMode ? pl->m_player2 : nullptr, st.second);
}

void onLoadCheckpoint(PlayLayer*, CheckpointObject* cp) { s.practiceCp = active() ? nullptr : cp; }

void beforeStep(GJBaseGameLayer* l) {
    int f = frameOf(l);
    s.stepFrame = f;
    s.inStep = true;
    if (!s.pendingRec.empty()) {
        if (s.recording) for (auto& [down, p1] : s.pendingRec) s.macro.push_back({f, down, p1});
        s.pendingRec.clear();
    }
    if (active() && s.needReset) return;      // the old run is still going until the reset happens
    bool baseline = s.phase == Phase::Baseline;

    if (baseline) {
        if (f != s.lastPathFrame && f % 2 == 0 && l->m_player1) {     // path of the normal run
            s.lastPathFrame = f;
            s.path.push_back(l->m_player1->getPosition());
        }
        // a checkpoint a few frames before every click, so trials never restart the level
        auto pl = PlayLayer::get();
        while (pl && s.nextCp < s.cpFrame.size() && s.cpFrame[s.nextCp] <= f) {
            if (!s.cps[s.nextCp]) {
                if (auto cp = pl->createCheckpoint()) {
                    cp->retain();
                    s.cps[s.nextCp] = cp;
                    s.cpAt[s.nextCp] = f;
                    savePlayer(pl->m_player1, s.cpP1[s.nextCp]);
                    savePlayer(pl->m_gameState.m_isDualMode ? pl->m_player2 : nullptr, s.cpP2[s.nextCp]);
                }
            }
            ++s.nextCp;
        }
    } else if (s.phase == Phase::Trial && f % 2 == 0 && l->m_player1) {
        s.trialPath.push_back(l->m_player1->getPosition());
    }
    // survived until the check frame: success, whatever happens later in the same burst of steps
    if (active() && s.checkFrame > 0 && f >= s.checkFrame && !s.trialDead) s.reached = true;

    if (active() || playing()) {
        s.injecting = true;
        while (s.cursor < s.trial.size() && s.trial[s.cursor].frame <= f) {
            size_t index = s.cursor++;
            auto& in = s.trial[index];
            if (baseline) {
                auto p = in.p1 ? l->m_player1 : l->m_player2;
                if (p && in.down && index < s.downToClick.size() && s.downToClick[index] >= 0)
                    s.clickPos[s.downToClick[index]] = p->getPosition();
                if (p && !in.down && index < s.relToClick.size() && s.relToClick[index] >= 0) {
                    int ci = s.relToClick[index];
                    s.relPos[ci] = p->getPosition();
                    // a release only changes the path in modes where holding matters (like Antic's Counter):
                    // ship, wave, robot. Cube / ball / UFO / spider / swing releases are not counted.
                    s.relMatters[ci] = p->m_isShip || p->m_isDart || p->m_isRobot;
                }
            }
            l->handleButton(in.down, 1, in.p1);
        }
        s.injecting = false;
    }

    // live display: every press / release whose frame is reached now shows its window
    if (!active() && !s.recording) {
        while (s.cueCursor < s.cues.size() && s.cues[s.cueCursor].frame <= f) {
            auto& c = s.cues[s.cueCursor];
            s.lastCue = static_cast<int>(s.cueCursor);
            ++s.cueCursor;
            if (c.frame != f) continue;
            if (Cfg::get().counterHud) hud::flash(std::to_string(c.window) + "f");
            if (Cfg::get().counterSfx) {
                auto name = "ding_" + std::to_string(std::min(c.window, 12)) + ".wav";
                FMODAudioEngine::sharedEngine()->playEffect(
                    std::string(Mod::get()->expandSpriteName(name.c_str())), 1.f, 1.f, 1.f);
            }
        }
    }
}

bool onDeath(PlayLayer* pl, GameObject* obj) {
    if (!active()) return false;
    if (s.reached || s.needReset) return true;     // already decided: deaths after the check frame do not count
    if (!s.trialDead) {
        // The death belongs to the physics step that was just processed (stepFrame). The game's frame counter
        // has already moved on by then: before, the death was stamped one frame too late, so "survive until the
        // frame where the replay dies" could never be reached - the unchanged replay itself "failed", every click
        // became NOT REPRODUCIBLE (all 0) or every shift died (window 1).
        s.deathFrame = s.stepFrame;
        (void)pl;
        s.lastDeathFrame = s.deathFrame;
        s.lastDeathId = obj ? obj->m_objectID : -1;
    }
    s.trialDead = true;
    return true;
}

bool onComplete(PlayLayer*) {
    if (s.recording) {
        finishRecording();
        return false;       // let the real completion happen
    }
    if (active()) { s.completed = true; return true; }
    return false;
}

// Called after every level reset AND every checkpoint load.
void onReset(PlayLayer* pl) {
    if (active()) {
        // after a checkpoint respawn the game's frame counter may not continue where the checkpoint was
        // taken: line our frames up with the checkpoint (plus the calibration offset of the control run)
        s.frameShift = s.pendingCp >= 0 ? s.pendingCp - rawFrame(pl) + calibShift() : 0;
        s.reached = false;
        // put back everything the game's checkpoint forgot
        int i = s.pendingIdx;
        if (s.pendingCp >= 0 && i >= 0 && i < static_cast<int>(s.cpP1.size())) {
            loadPlayer(pl->m_player1, s.cpP1[i]);
            loadPlayer(pl->m_player2, s.cpP2[i]);
        }
    } else if (s.recording && s.practiceCp) {
        // practice respawn while recording: the player continues exactly as it was when the checkpoint was placed
        auto it = s.practice.find(s.practiceCp);
        if (it != s.practice.end()) {
            loadPlayer(pl->m_player1, it->second.first);
            loadPlayer(pl->m_player2, it->second.second);
        }
    }
    s.pendingRec.clear();        // clicks of the old attempt never leak into the new one
    int f = frameOf(pl);
    // the restart Record mode does itself always starts the recording (a start position or practice mode can
    // make the first frame bigger than 1, and then the recording never started before)
    if (s.armed && (f <= 1 || g_forceArm)) {
        s.armed = false;
        s.recording = true;
        s.macro.clear();
        s.msg = "Recording...";
    } else if (s.recording) {
        // respawn (checkpoint or restart): forget everything after this frame, keep the rest
        s.macro.erase(std::remove_if(s.macro.begin(), s.macro.end(), [&](Input const& i) { return i.frame >= f; }),
                      s.macro.end());
    }
    syncCursor(f);
    syncCues(f);
    s.trialDead = false;
    s.completed = false;
    s.lastPathFrame = -1;
    if (active()) s.trialPath.clear();
}

void update(PlayLayer* pl) {
    if (s.restartAfter && !active()) {
        s.restartAfter = false;
        g_forceArm = s.armed;
        pl->resetLevelFromStart();
        g_forceArm = false;
        return;
    }
    if (!active()) return;
    if (s.needReset) {
        s.needReset = false;
        bool useCp = s.phase == Phase::Trial && s.k < static_cast<int>(s.cps.size()) && s.cps[s.k] &&
                     (s.cpOk[s.k] == -1 || s.cpOk[s.k] == 1);
        if (useCp) {
            // a real practice respawn at OUR checkpoint: resetLevel resets the whole level (orbs, triggers,
            // moving objects...) and then loads the last checkpoint - exactly what happens when you die
            s.pendingCp = s.cpAt[s.k];
            s.pendingIdx = s.k;
            auto arr = CCArray::create();
            arr->addObject(s.cps[s.k]);
            auto oldArr = pl->m_checkpointArray;
            bool oldPractice = pl->m_isPracticeMode;
            pl->m_checkpointArray = arr;
            pl->m_isPracticeMode = true;
            pl->resetLevel();
            pl->m_checkpointArray = oldArr;
            pl->m_isPracticeMode = oldPractice;
        } else {
            s.pendingCp = -1;
            s.pendingIdx = -1;
            pl->resetLevelFromStart();
        }
        return;
    }
    if (s.completed || s.reached) finish(true);
    else if (s.trialDead) finish(false);
}

bool pendingReset() { return active() && s.needReset; }

// ================= UI-facing API =================

Mode mode() { return s.mode; }

void setMode(Mode m) {
    if (active()) return;
    finishRecording();           // switching away from Record keeps what was recorded
    s.mode = m;
    s.recording = s.armed = false;
    if (m == Mode::Record) {
        // The level restarts from the beginning by itself as soon as you resume; recording starts there.
        s.armed = true;
        s.restartAfter = PlayLayer::get() != nullptr;
        s.msg = "Resume the game - the level restarts and recording begins";
    } else if (m == Mode::Playback) {
        syncTrial();
        auto pl = PlayLayer::get();
        syncCursor(pl ? frameOf(pl) : 0);
        s.msg = s.macro.empty() ? "Nothing to play - load or record a replay" : "Playback on";
    } else {
        s.msg = "Idle";
    }
}

void startCounting() {
    if (s.macro.empty()) { s.msg = "No replay - record or load one first"; return; }
    s.armed = s.recording = false;
    buildClicks();
    s.cues.clear();
    s.cueCursor = 0;
    s.lastCue = -1;
    releaseCps();
    size_t nc = s.clicks.size();
    s.cps.assign(nc, nullptr);
    s.cpFrame.assign(nc, 0);
    s.cpAt.assign(nc, 0);
    s.cpP1.assign(nc, PState{});
    s.cpP2.assign(nc, PState{});
    s.cpOk.assign(nc, -1);
    s.relMatters.assign(nc, 0);
    for (size_t i = 0; i < nc; ++i) {
        int down = s.clicks[i].down;
        int cpf = std::max(0, s.macro[down].frame - kMaxSide - 2);
        // never respawn while the button is still held from the previous click (a respawn forgets the hold):
        // move the checkpoint after that click's release
        for (int j = down - 1; j >= 0; --j) {
            if (s.macro[j].p1 != s.macro[down].p1) continue;
            if (!s.macro[j].down) cpf = std::max(cpf, std::min(s.macro[j].frame + 1, s.macro[down].frame));
            break;
        }
        s.cpFrame[i] = cpf;
    }
    s.fallbacks = 0;
    s.broken = 0;
    s.notes.assign(nc, "");
    s.lastTrial.clear();
    s.bestShift = 0;
    s.calibIdx = 0;
    s.reached = false;
    s.path.clear();
    s.lines.clear();
    ++s.trialSerial;
    s.trialPath.clear();
    s.lastPathFrame = -1;
    s.limit = static_cast<int>(s.clicks.size());
    s.deathFrame = 0;
    s.restartAfter = false;
    s.phase = Phase::Baseline;
    makeTrial(-1, 0, DownEarly);
    s.needReset = true;
    s.t0 = std::chrono::steady_clock::now();
    s.msg = "Counting";
    ++s.version;
}

// counting no longer speeds up time (that only made each frame take bigger steps); Hooks.cpp instead runs
// many game steps per frame while counting
float speedFactor() { return 1.f; }

void cancelCounting() { if (active()) stop("Stopped"); }
void abortCounting(std::string const& why) { if (active()) stop(why); }

bool isRecording() { return s.recording || s.armed; }
bool botPlaying() { return s.mode == Mode::Playback && !s.macro.empty(); }
bool isCounting() { return active(); }
bool baselineRunning() { return s.phase == Phase::Baseline; }

std::string countingText() {
    if (!active()) return "";
    int n = s.phase == Phase::Baseline ? static_cast<int>(s.clicks.size()) : s.limit;
    if (s.phase == Phase::Baseline) return "Preparing.. 0/" + std::to_string(n) + "\nETA ~?";
    float el = std::chrono::duration<float>(std::chrono::steady_clock::now() - s.t0).count();
    std::string e = s.k > 0 ? eta(el / s.k * (n - s.k)) : std::string("~?");
    std::string how;
    if (s.k < static_cast<int>(s.cpOk.size())) {
        int st = s.cpOk[s.k];
        how = st == 1 ? "checkpoint" : st == -1 ? "checking" : "restart";
    }
    return "Counting.. " + std::to_string(s.k) + "/" + std::to_string(n) + "  (" + how + ")\nETA " + e +
           "\n" + s.lastTrial;
}

std::string statusText() {
    if (s.armed) return "Record ready - resume, the level restarts and recording begins";
    if (s.recording) return "Recording... " + std::to_string(s.macro.size()) + " inputs";
    if (active()) {
        auto t = countingText();
        std::replace(t.begin(), t.end(), '\n', ' ');
        return t;
    }
    return s.msg;
}

std::string macroInfo() {
    std::string state = s.recording ? "Recording" : s.mode == Mode::Playback ? "Playing"
                      : active() ? "Counting" : "Loaded";
    auto& c = Cfg::get();
    int tps = c.tpsBypass ? static_cast<int>(c.tps) : 240;
    return state + "  -  " + std::to_string(s.macro.size()) + " inputs at " + std::to_string(tps) + " TPS";
}

std::string summaryText() {
    std::vector<size_t> idx;
    float sum = 0.f;
    for (size_t i = 0; i < s.results.size(); ++i) {
        if (!s.results[i].counted) continue;
        idx.push_back(i);
        sum += s.results[i].window();
    }
    if (idx.empty()) return "No counted windows yet";
    std::sort(idx.begin(), idx.end(), [](size_t a, size_t b) { return s.results[a].window() < s.results[b].window(); });
    std::ostringstream o;
    char buf[96];
    std::snprintf(buf, sizeof buf, "%d clicks   average window %.1f f\nTightest clicks:\n",
                  static_cast<int>(idx.size()), sum / idx.size());
    o << buf;
    for (size_t n = 0; n < std::min<size_t>(5, idx.size()); ++n) {
        auto& r = s.results[idx[n]];
        std::snprintf(buf, sizeof buf, "#%d  frame %d   %df  (-%d / +%d)\n", static_cast<int>(idx[n]) + 1, r.frame,
                      r.window(), r.early, r.late);
        o << buf;
    }
    return o.str();
}

std::string infoText() {
    std::ostringstream o;
    o << "Replay: " << currentName() << "\n" << s.msg << "\n" << summaryText() << "\nAll clicks:\n";
    for (size_t i = 0; i < s.results.size(); ++i) {
        auto& r = s.results[i];
        std::string note = i < s.notes.size() ? s.notes[i] : "";
        if (!r.counted && note.empty()) continue;
        o << "#" << (i + 1) << "  frame " << r.frame;
        if (r.counted) o << "  click " << r.window() << "f (-" << r.early << " / +" << r.late << ")";
        if (r.relCounted) o << "  release " << r.relWindow() << "f (-" << r.relEarly << " / +" << r.relLate << ")";
        if (!note.empty()) o << "  [" << note << "]";
        o << "\n";
    }
    return o.str();
}

std::vector<Result> const& results() { return s.results; }
std::vector<Cue> const& cues() { return s.cues; }
std::vector<Cue> countedCues() { return collectCues(); }
size_t cueCursor() { return s.cueCursor; }
int lastCue() { return s.lastCue; }
std::vector<CCPoint> const& pathPoints() { return s.path; }
std::vector<TrialLine> const& trialLines() { return s.lines; }
std::vector<CCPoint> const& currentTrialPath() { return s.trialPath; }
int version() { return s.version; }
int trialSerial() { return s.trialSerial; }

// ---------- replay files ----------

std::string currentName() {
    if (s.names.empty()) refreshList();
    return hasSelection() ? s.names[s.sel] : std::string("(none)");
}

void selectReplay(int delta) {
    refreshList();
    if (s.names.empty()) return;
    int n = static_cast<int>(s.names.size());
    s.sel = ((s.sel + delta) % n + n) % n;
    Mod::get()->setSavedValue<std::string>("replayName", s.names[s.sel]);
}

std::string newReplay(std::string const& rawName) {
    std::string name = rawName;
    while (!name.empty() && name.back() == ' ') name.pop_back();
    while (!name.empty() && name.front() == ' ') name.erase(name.begin());
    if (name.empty()) return "Enter a name first";
    refreshList();
    if (std::find(s.names.begin(), s.names.end(), name) != s.names.end()) return "A replay with that name exists";
    s.macro.clear(); s.clicks.clear(); s.results.clear(); s.cues.clear();
    s.clickPos.clear(); s.relPos.clear(); s.path.clear(); s.downToClick.clear(); s.relToClick.clear();
    s.cueCursor = 0;
    s.lastCue = -1;
    syncTrial();
    s.cursor = 0;
    ++s.version;
    (void)utils::file::writeString(replayPath(name), encode());
    Mod::get()->setSavedValue<std::string>("replayName", name);
    refreshList();
    s.msg = "Created " + name;
    return "Created \"" + name + "\"";
}

std::string saveReplay() {
    refreshList();
    if (!hasSelection()) return "Create a replay with New first";
    autosave();
    return "Saved \"" + s.names[s.sel] + "\"";
}

std::string loadReplay() {
    refreshList();
    if (!hasSelection()) return "No replay selected";
    auto r = utils::file::readString(replayPath(s.names[s.sel]));
    if (!r.isOk()) return "Could not read the file";
    decode(r.unwrap());
    if (auto pl = PlayLayer::get()) syncCues(frameOf(pl));
    s.msg = "Loaded " + s.names[s.sel];
    return "Loaded \"" + s.names[s.sel] + "\" (" + std::to_string(s.clicks.size()) + " clicks)";
}

std::string deleteReplay() {
    refreshList();
    if (!hasSelection()) return "No replay selected";
    std::string name = s.names[s.sel];
    std::remove(replayPath(name).string().c_str());
    s.sel = 0;
    Mod::get()->setSavedValue<std::string>("replayName", "");
    refreshList();
    return "Deleted \"" + name + "\"";
}

void openReplaysFolder() {
    (void)utils::file::openFolder(replaysDir());
}

} // namespace kt::counter
```

## src/Hitbox.cpp

```cpp
// Hitboxes, hitbox trail, and Hitborders (NaN style "accurate gaps").
//
// Hitborders: every object is grown by the player's size, so only the player's CENTER (yellow dot) matters:
//   red   = hazards grown by half the outer player box + solids grown by half the inner box  -> center in red = death
//   blue  = solids grown by half the outer player box                                         -> passable / landable
//   green = pads, orbs, portals grown by half the outer box
// All areas go into ONE grid: every cell gets exactly one color (red > green > blue), so areas never overlap
// and outlines are drawn only where the color changes.
//
// Performance: objects are sorted by x once per level, so each frame only the objects near the player are visited.
#include "Common.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>

using namespace geode::prelude;

namespace kt::hitbox {

namespace {
    constexpr float kInner = 0.3f;     // inner (solid-death) hitbox = 30% of the outer box
    constexpr float kMargin = 400.f;   // extra x range so wide objects are not missed
    struct TrailBox {
        CCRect outer, inner;
    };
    std::deque<TrailBox> g_trail1, g_trail2;
    std::deque<CCPoint> g_center1, g_center2;
    constexpr char const* kNodeId = "kt-hitbox-node";

    enum class Kind { Skip, Hazard, Solid, Special };
    Kind kindOf(GameObject* o) {
        switch (o->m_objectType) {
            case GameObjectType::Hazard:
            case GameObjectType::AnimatedHazard: return Kind::Hazard;
            case GameObjectType::Solid:
            case GameObjectType::Slope:
            case GameObjectType::Breakable: return Kind::Solid;
            case GameObjectType::Decoration:
            case GameObjectType::Modifier:
            case GameObjectType::CollisionObject:
            case GameObjectType::EnterEffectObject:
            case GameObjectType::Special: return Kind::Skip;
            default: return Kind::Special;
        }
    }

    // ---- object cache: only hitbox objects, sorted by x ----
    PlayLayer* g_owner = nullptr;
    unsigned g_count = 0;
    std::vector<GameObject*> g_sorted;
    int g_sortTick = 0;

    bool byX(GameObject* a, GameObject* b) { return a->getPositionX() < b->getPositionX(); }

    void ensureCache(PlayLayer* pl) {
        unsigned n = pl->m_objects ? pl->m_objects->count() : 0;
        if (pl == g_owner && n == g_count) {
            // moving objects (move triggers) change their x: keep the order right so none are missed.
            // A full sort once a second has a fixed cost (an insertion sort could freeze the game when many
            // objects move far).
            if (++g_sortTick % 60 == 0) std::sort(g_sorted.begin(), g_sorted.end(), byX);
            return;
        }
        g_owner = pl;
        g_count = n;
        g_sorted.clear();
        if (!pl->m_objects) return;
        g_sorted.reserve(n);
        for (auto obj : CCArrayExt<GameObject*>(pl->m_objects)) {
            if (obj && kindOf(obj) != Kind::Skip) g_sorted.push_back(obj);
        }
        std::sort(g_sorted.begin(), g_sorted.end(), byX);
    }

    CCRect objRect(GameObject* o);

    // a box that can be drawn: real numbers, not empty, not absurdly big (scale 0, NaN or 10000-block-wide
    // objects exist in real levels and made the drawing code allocate huge buffers)
    bool validRect(CCRect const& r) {
        float v[4] = {r.origin.x, r.origin.y, r.size.width, r.size.height};
        for (float f : v) if (!std::isfinite(f)) return false;
        return r.size.width > 0.01f && r.size.height > 0.01f && r.size.width < 6000.f && r.size.height < 6000.f;
    }

    // objects the game currently ignores for collision: "no touch", or in a group that is toggled off
    bool collides(GameObject* o) { return !o->m_isNoTouch && !o->m_isGroupDisabled; }

    // only objects whose box really reaches into [x0, x1] (the visible part of the level)
    template <class F>
    void forNear(float x0, float x1, F&& f) {
        auto it = std::lower_bound(g_sorted.begin(), g_sorted.end(), x0 - kMargin,
                                   [](GameObject* o, float x) { return o->getPositionX() < x; });
        for (; it != g_sorted.end() && (*it)->getPositionX() <= x1 + kMargin; ++it) {
            if (!collides(*it)) continue;
            auto r = objRect(*it);
            if (!validRect(r)) continue;        // broken / invisible-size objects would poison the drawing
            if (r.getMaxX() < x0 || r.getMinX() > x1) continue;
            f(*it);
        }
    }

    // visible x range: the player sits about a third into the screen
    void viewRange(PlayLayer* pl, float& x0, float& x1) {
        float w = CCDirector::get()->getWinSize().width;
        float px = pl->m_player1->getPositionX();
        x0 = px - w * 0.6f;
        x1 = px + w * 1.0f;
    }

    CCDrawNode* g_node = nullptr;     // kept as a pointer: no search through the level's children every frame
    CCDrawNode* getNode(PlayLayer* pl) {
        if (!g_node) {
            auto parent = pl->m_objectLayer;
            g_node = static_cast<CCDrawNode*>(parent->getChildByID(kNodeId));
            if (!g_node) {
                g_node = CCDrawNode::create();
                g_node->setID(kNodeId);
                parent->addChild(g_node, 9999);
            }
        }
        return g_node;
    }

    void poly(CCDrawNode* n, std::vector<CCPoint> const& v, ccColor3B c, bool fill, float alpha = 1.f) {
        auto& cfg = Cfg::get();
        ccColor4F border{c.r / 255.f, c.g / 255.f, c.b / 255.f, alpha};
        ccColor4F fillC{border.r, border.g, border.b, fill ? cfg.hitFill * alpha : 0.f};
        n->drawPolygon(const_cast<CCPoint*>(v.data()), static_cast<unsigned>(v.size()), fillC, cfg.hitBorder, border);
    }

    std::vector<CCPoint> corners(CCRect const& r) {
        return {{r.getMinX(), r.getMinY()}, {r.getMaxX(), r.getMinY()}, {r.getMaxX(), r.getMaxY()}, {r.getMinX(), r.getMaxY()}};
    }

    CCRect innerOf(CCRect const& outer) {
        float w = outer.size.width * kInner, h = outer.size.height * kInner;
        return {outer.getMidX() - w / 2, outer.getMidY() - h / 2, w, h};
    }

    // axis aligned box of an object (rotated objects use the box around their corners)
    CCRect objRect(GameObject* o) {
        if (o->m_orientedBox) {
            auto& c = o->m_orientedBox->m_corners;
            float x0 = c[0].x, x1 = c[0].x, y0 = c[0].y, y1 = c[0].y;
            for (auto& p : c) {
                x0 = std::min(x0, p.x); x1 = std::max(x1, p.x);
                y0 = std::min(y0, p.y); y1 = std::max(y1, p.y);
            }
            return {x0, y0, x1 - x0, y1 - y0};
        }
        return o->getObjectRect();
    }

    CCRect grow(CCRect const& r, float hx, float hy) {
        return {r.origin.x - hx, r.origin.y - hy, r.size.width + 2 * hx, r.size.height + 2 * hy};
    }

    // rotated by something that is not a multiple of 90 degrees (then its box is not axis aligned)
    bool isTilted(GameObject* o) {
        if (!o->m_orientedBox) return false;
        float r = std::fmod(std::fabs(o->getRotation()), 90.f);
        return r > 0.5f && r < 89.5f;
    }

    // saw blades and other round hazards: the game tests a circle, not a box
    bool isCircle(GameObject* o) {
        float r = o->m_objectRadius;
        return std::isfinite(r) && r > 0.f && r < 2000.f && o->m_objectType != GameObjectType::Slope;
    }

    bool needsPolygon(GameObject* o) {
        return o->m_objectType == GameObjectType::Slope || isTilted(o) || isCircle(o);
    }

    // The real hitbox shape: a circle for round hazards, a triangle for slopes (45 and 22.5 degree), the rotated
    // box for tilted objects, otherwise the plain rectangle.
    std::vector<CCPoint> shapeOf(GameObject* o) {
        if (isCircle(o)) {
            auto r = objRect(o);
            CCPoint c(r.getMidX(), r.getMidY());
            float rad = o->m_objectRadius * std::max(std::fabs(o->getScaleX()), std::fabs(o->getScaleY()));
            std::vector<CCPoint> pts;
            constexpr int seg = 24;
            for (int i = 0; i < seg; ++i) {
                float a = 6.2831853f * i / seg;
                pts.push_back(CCPoint(c.x + rad * std::cos(a), c.y + rad * std::sin(a)));
            }
            return pts;
        }
        if (o->m_objectType == GameObjectType::Slope) {
            auto r = objRect(o);
            CCPoint bl(r.getMinX(), r.getMinY()), br(r.getMaxX(), r.getMinY());
            CCPoint tl(r.getMinX(), r.getMaxY()), tr(r.getMaxX(), r.getMaxY());
#ifdef GEODE_IS_WINDOWS
            // slopeFloorTop / slopeWallLeft do not exist on Windows (inlined by the compiler), so the triangle is
            // computed from the object itself: the unrotated slope is the right triangle bl, br, tr (ramp going up
            // to the right, vertical wall on the right); flip and rotation are applied to it and every point is
            // snapped to the corner of the box it lands in.
            float a = o->getRotation() * 3.14159265f / 180.f;
            float ca = std::cos(a), sa = std::sin(a);
            std::vector<CCPoint> tri;
            for (CCPoint p : {CCPoint(-1.f, -1.f), CCPoint(1.f, -1.f), CCPoint(1.f, 1.f)}) {
                if (o->m_isFlipX) p.x = -p.x;
                if (o->m_isFlipY) p.y = -p.y;
                float x = p.x * ca + p.y * sa;          // cocos rotation is clockwise
                float y = -p.x * sa + p.y * ca;
                tri.push_back(CCPoint(x < 0.f ? r.getMinX() : r.getMaxX(), y < 0.f ? r.getMinY() : r.getMaxY()));
            }
            return tri;
#else
            // GD's names describe the FLAT sides of the triangle: slopeFloorTop = the flat (floor) side is at the
            // top (an upside-down slope), slopeWallLeft = the vertical wall is on the left.
            bool flatTop = o->slopeFloorTop();
            bool wallLeft = o->slopeWallLeft();
            if (flatTop) return wallLeft ? std::vector<CCPoint>{tl, tr, bl} : std::vector<CCPoint>{tl, tr, br};
            return wallLeft ? std::vector<CCPoint>{bl, br, tl} : std::vector<CCPoint>{bl, br, tr};
#endif
        }
        if (isTilted(o)) {
            auto& c = o->m_orientedBox->m_corners;
            return std::vector<CCPoint>(c.begin(), c.end());
        }
        return corners(o->getObjectRect());
    }

    std::vector<CCPoint> hullOf(std::vector<CCPoint> pts);

    // shape grown by a box of half size (hx, hy) = convex hull of the shape moved to every box corner
    std::vector<CCPoint> grownHull(std::vector<CCPoint> const& shape, float hx, float hy) {
        std::vector<CCPoint> pts;
        for (auto& p : shape) {
            pts.push_back(CCPoint(p.x - hx, p.y - hy));
            pts.push_back(CCPoint(p.x + hx, p.y - hy));
            pts.push_back(CCPoint(p.x + hx, p.y + hy));
            pts.push_back(CCPoint(p.x - hx, p.y + hy));
        }
        return hullOf(std::move(pts));
    }

    // convex hull (monotone chain)
    std::vector<CCPoint> hullOf(std::vector<CCPoint> pts) {
        if (pts.size() < 3) return pts;
        std::sort(pts.begin(), pts.end(), [](CCPoint const& a, CCPoint const& b) {
            return a.x < b.x || (a.x == b.x && a.y < b.y);
        });
        auto cross = [](CCPoint const& o, CCPoint const& a, CCPoint const& b) {
            return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
        };
        std::vector<CCPoint> h(pts.size() * 2);
        size_t k = 0;
        for (size_t i = 0; i < pts.size(); ++i) {
            while (k >= 2 && cross(h[k - 2], h[k - 1], pts[i]) <= 0) --k;
            h[k++] = pts[i];
        }
        for (size_t i = pts.size() - 1, t = k + 1; i > 0; --i) {
            while (k >= t && cross(h[k - 2], h[k - 1], pts[i - 1]) <= 0) --k;
            h[k++] = pts[i - 1];
        }
        h.resize(k > 0 ? k - 1 : 0);
        return h;
    }

    void fillPoly(CCDrawNode* n, std::vector<CCPoint> const& v, ccColor4F fill, float bw, ccColor4F border) {
        if (v.size() < 3) return;
        n->drawPolygon(const_cast<CCPoint*>(v.data()), static_cast<unsigned>(v.size()), fill, bw, border);
    }

    struct Area {
        CCRect r;
        uint8_t level;      // 1 blue, 2 green, 3 red (higher wins)
    };

    // One grid for all areas: every cell gets the highest level that covers it.
    void drawAreas(CCDrawNode* n, std::vector<Area> const& areas, float bw, float fillA) {
        if (areas.empty()) return;
        std::vector<float> xs, ys;
        xs.reserve(areas.size() * 2);
        ys.reserve(areas.size() * 2);
        for (auto& a : areas) {
            xs.push_back(a.r.getMinX()); xs.push_back(a.r.getMaxX());
            ys.push_back(a.r.getMinY()); ys.push_back(a.r.getMaxY());
        }
        auto same = [](float a, float b) { return std::fabs(a - b) < 0.01f; };
        std::sort(xs.begin(), xs.end());
        std::sort(ys.begin(), ys.end());
        xs.erase(std::unique(xs.begin(), xs.end(), same), xs.end());
        ys.erase(std::unique(ys.begin(), ys.end(), same), ys.end());
        int nx = static_cast<int>(xs.size()) - 1, ny = static_cast<int>(ys.size()) - 1;
        if (nx <= 0 || ny <= 0) return;

        ccColor4F plain[4] = {{0, 0, 0, 0},
                              {0.05f, 0.12f, 0.95f, 1.f},
                              {0.1f, 0.75f, 0.2f, fillA},
                              {0.55f, 0.05f, 0.05f, fillA}};
        // Very busy screens would need a gigantic grid (cells = columns x rows): that ran out of memory and
        // crashed. Then every area is simply drawn on its own, blue first, red last (red still on top).
        if (static_cast<size_t>(nx) * static_cast<size_t>(ny) > 250000) {
            for (uint8_t lvl = 1; lvl <= 3; ++lvl) {
                for (auto& a : areas) {
                    if (a.level != lvl) continue;
                    auto q = corners(a.r);
                    n->drawPolygon(q.data(), 4, plain[lvl], lvl >= 2 ? bw / 2.f : 0.f,
                                   lvl == 3 ? ccColor4F{1.f, 0.1f, 0.1f, 1.f} : lvl == 2 ? ccColor4F{0.2f, 1.f, 0.3f, 1.f} : plain[1]);
                }
            }
            return;
        }

        auto idx = [](std::vector<float> const& v, float x) {
            return static_cast<int>(std::lower_bound(v.begin(), v.end(), x - 0.005f) - v.begin());
        };
        std::vector<uint8_t> cell(static_cast<size_t>(nx) * ny, 0);
        auto at = [&](int i, int j) -> uint8_t& { return cell[static_cast<size_t>(j) * nx + i]; };
        for (auto& a : areas) {
            int x0 = idx(xs, a.r.getMinX()), x1 = idx(xs, a.r.getMaxX());
            int y0 = idx(ys, a.r.getMinY()), y1 = idx(ys, a.r.getMaxY());
            for (int j = y0; j < y1 && j < ny; ++j)
                for (int i = x0; i < x1 && i < nx; ++i) at(i, j) = std::max(at(i, j), a.level);
        }

        ccColor4F fills[4] = {{0, 0, 0, 0},
                              {0.05f, 0.12f, 0.95f, 1.f},             // blue (solid, no transparency like NaN's)
                              {0.1f, 0.75f, 0.2f, fillA},             // green
                              {0.55f, 0.05f, 0.05f, fillA}};          // red
        ccColor4F borders[4] = {{0, 0, 0, 0}, {0, 0, 0, 0}, {0.2f, 1.f, 0.3f, 1.f}, {1.f, 0.1f, 0.1f, 1.f}};

        // fill: cells become as few rectangles as possible (a row run is stacked onto the run above it when
        // both have the same x range and color), so no two quads share an edge or overlap.
        struct Open { int s, e, j0; uint8_t v; };
        std::vector<Open> open;
        auto flush = [&](Open const& o, int jEnd) {
            CCPoint q[4] = {{xs[o.s], ys[o.j0]}, {xs[o.e], ys[o.j0]}, {xs[o.e], ys[jEnd]}, {xs[o.s], ys[jEnd]}};
            n->drawPolygon(q, 4, fills[o.v], 0.f, fills[o.v]);
        };
        for (int j = 0; j < ny; ++j) {
            std::vector<Open> next;
            int i = 0;
            while (i < nx) {
                uint8_t v = at(i, j);
                int s = i;
                while (i < nx && at(i, j) == v) ++i;
                if (!v) continue;
                Open cur{s, i, j, v};
                for (auto& o : open) {
                    if (o.j0 >= 0 && o.s == s && o.e == i && o.v == v) { cur.j0 = o.j0; o.j0 = -1; break; }
                }
                next.push_back(cur);
            }
            for (auto& o : open) if (o.j0 >= 0) flush(o, j);
            open = std::move(next);
        }
        for (auto& o : open) flush(o, ny);

        // outline: drawn where the color changes, in the color of the higher area (blue has none).
        // Neighbouring edge pieces of the same color are joined into one long line, so even a big level only
        // needs a few lines instead of one per cell.
        if (bw <= 0.f) return;
        float rad = bw / 2.f;
        auto val = [&](int i, int j) -> uint8_t { return (i < 0 || j < 0 || i >= nx || j >= ny) ? 0 : at(i, j); };
        auto edge = [](uint8_t a, uint8_t b) -> uint8_t {
            uint8_t hi = std::max(a, b);
            return (hi >= 2 && a != b) ? hi : 0;
        };
        // vertical lines at every x boundary
        for (int i = 0; i <= nx; ++i) {
            int j = 0;
            while (j < ny) {
                uint8_t e = edge(val(i - 1, j), val(i, j));
                if (!e) { ++j; continue; }
                int s = j;
                while (j < ny && edge(val(i - 1, j), val(i, j)) == e) ++j;
                n->drawSegment({xs[i], ys[s]}, {xs[i], ys[j]}, rad, borders[e]);
            }
        }
        // horizontal lines at every y boundary
        for (int j = 0; j <= ny; ++j) {
            int i = 0;
            while (i < nx) {
                uint8_t e = edge(val(i, j - 1), val(i, j));
                if (!e) { ++i; continue; }
                int s = i;
                while (i < nx && edge(val(i, j - 1), val(i, j)) == e) ++i;
                n->drawSegment({xs[s], ys[j]}, {xs[i], ys[j]}, rad, borders[e]);
            }
        }
    }

    void pushCenter(std::deque<CCPoint>& t, PlayerObject* p) {
        if (!p) return;
        t.push_back(p->getPosition());
        while (static_cast<int>(t.size()) > std::clamp(Cfg::get().trailLen * 4, 2, 1200)) t.pop_front();
    }

    void drawCenter(CCDrawNode* n, std::deque<CCPoint> const& t, PlayerObject* p) {
        ccColor4F yellow{1.f, 0.95f, 0.1f, 1.f};
        for (size_t i = 1; i < t.size(); ++i) n->drawSegment(t[i - 1], t[i], 0.6f, yellow);
        if (!p) return;
        auto c = p->getPosition();
        CCPoint dot[4] = {{c.x - 2.5f, c.y - 2.5f}, {c.x + 2.5f, c.y - 2.5f}, {c.x + 2.5f, c.y + 2.5f}, {c.x - 2.5f, c.y + 2.5f}};
        n->drawPolygon(dot, 4, yellow, 0.f, yellow);
    }

    void drawHitborders(CCDrawNode* n, PlayLayer* pl) {
        auto outer = pl->m_player1->getObjectRect();
        auto inner = innerOf(outer);
        float ox = outer.size.width / 2, oy = outer.size.height / 2;
        float ix = inner.size.width / 2, iy = inner.size.height / 2;

        // Straight boxes go into the merged grid. Slopes and tilted objects are not rectangles, so they get
        // their exact grown shape as a polygon instead (blue ones under the grid, red/green ones on top).
        std::vector<Area> areas;
        std::vector<std::vector<CCPoint>> blueP, greenP, redP;
        float x0, x1;
        viewRange(pl, x0, x1);
        forNear(x0, x1, [&](GameObject* obj) {
            auto kind = kindOf(obj);
            if (kind == Kind::Solid && obj->m_objectType == GameObjectType::Slope && obj->m_slopeIsHazard) kind = Kind::Hazard;
            if (needsPolygon(obj)) {
                auto shape = shapeOf(obj);
                switch (kind) {
                    case Kind::Hazard: redP.push_back(grownHull(shape, ox, oy)); break;
                    case Kind::Solid:
                        blueP.push_back(grownHull(shape, ox, oy));
                        if (!obj->m_isPassable) redP.push_back(grownHull(shape, ix, iy));
                        break;
                    case Kind::Special: greenP.push_back(grownHull(shape, ox, oy)); break;
                    default: break;
                }
                return;
            }
            auto r = objRect(obj);
            switch (kind) {
                case Kind::Hazard: areas.push_back({grow(r, ox, oy), 3}); break;
                case Kind::Solid:
                    areas.push_back({grow(r, ox, oy), 1});
                    // passable blocks (jump through from below) can never kill: no red core
                    if (!obj->m_isPassable) areas.push_back({grow(r, ix, iy), 3});
                    break;
                case Kind::Special: areas.push_back({grow(r, ox, oy), 2}); break;
                default: break;
            }
        });

        float bw = Cfg::get().hitBorder;
        float fa = std::clamp(Cfg::get().hitFill * 3.f, 0.15f, 0.85f);   // fill opacity slider also drives this view
        ccColor4F blue{0.05f, 0.12f, 0.95f, 1.f};
        for (auto& p : blueP) fillPoly(n, p, blue, 0.f, blue);
        drawAreas(n, areas, bw, fa);
        for (auto& p : greenP) fillPoly(n, p, {0.1f, 0.75f, 0.2f, fa}, bw / 2.f, {0.2f, 1.f, 0.3f, 1.f});
        for (auto& p : redP) fillPoly(n, p, {0.55f, 0.05f, 0.05f, fa}, bw / 2.f, {1.f, 0.1f, 0.1f, 1.f});

        // the yellow center line gets its points per physics tick (onTick), so it always follows the player
        if (g_center1.empty()) pushCenter(g_center1, pl->m_player1);
        drawCenter(n, g_center1, pl->m_player1);
        if (pl->m_gameState.m_isDualMode) {
            if (g_center2.empty()) pushCenter(g_center2, pl->m_player2);
            drawCenter(n, g_center2, pl->m_player2);
        }
    }

    void drawObject(CCDrawNode* n, GameObject* o) {
        ccColor3B col;
        switch (kindOf(o)) {
            case Kind::Hazard: col = {255, 60, 60}; break;
            case Kind::Solid: col = {70, 130, 255}; break;
            case Kind::Skip: return;
            default: col = {80, 255, 120}; break;      // portals, pads, orbs, coins...
        }
        // Circles for round hazards, triangles for slopes, oriented boxes for rotated objects, else a rect.
        if (isCircle(o)) {
            poly(n, shapeOf(o), col, true);
        } else if (o->m_objectType == GameObjectType::Slope) {
            if (o->m_slopeIsHazard) col = {255, 60, 60};
            poly(n, shapeOf(o), col, true);
        } else if (o->m_orientedBox) {
            auto& c = o->m_orientedBox->m_corners;   // std::array<CCPoint, 4> in 2.2091
            poly(n, std::vector<CCPoint>(c.begin(), c.end()), col, true);
        } else {
            poly(n, corners(o->getObjectRect()), col, true);
        }
    }

    void drawPlayer(CCDrawNode* n, PlayerObject* p) {
        if (!p) return;
        auto outer = p->getObjectRect();
        poly(n, corners(outer), {255, 215, 60}, true);
        poly(n, corners(innerOf(outer)), {60, 220, 255}, true);
    }

    // Trail = the real hitbox of every physics tick, drawn one by one (nothing smoothed or joined): at 240 TPS
    // that is 240 boxes per second of gameplay, at 360 TPS 360. The boxes are taken per tick, not per rendered
    // frame, so the speedhack (or FPS) does not change how many there are.
    // Red outline = outer hitbox, blue outline = inner hitbox.
    void drawTrail(CCDrawNode* n, std::deque<TrailBox> const& t) {
        if (t.empty()) return;
        ccColor4F none{0.f, 0.f, 0.f, 0.f};
        ccColor4F red{1.f, 0.f, 0.f, 1.f}, blue{0.f, 0.25f, 1.f, 1.f};
        float bw = std::max(0.3f, Cfg::get().hitBorder * 0.5f);
        for (auto const& b : t) {
            auto o = corners(b.outer);
            n->drawPolygon(o.data(), 4, none, bw, red);
        }
        for (auto const& b : t) {
            auto i = corners(b.inner);
            n->drawPolygon(i.data(), 4, none, bw, blue);
        }
    }

    void push(std::deque<TrailBox>& t, PlayerObject* p) {
        if (!p) return;
        auto outer = p->getObjectRect();
        t.push_back({outer, innerOf(outer)});
        // the length setting is in 60 fps frames: 4 ticks each at 240 TPS
        while (static_cast<int>(t.size()) > std::clamp(Cfg::get().trailLen * 4, 4, 2000)) t.pop_front();
    }
}

// Called once per physics tick (Hooks.cpp): the trail gets one box per tick.
void onTick(PlayLayer* pl) {
    auto& c = Cfg::get();
    if (!pl->m_player1) return;
    bool dual = pl->m_gameState.m_isDualMode;
    if (c.hitborders) {
        pushCenter(g_center1, pl->m_player1);
        if (dual) pushCenter(g_center2, pl->m_player2);
        else g_center2.clear();
    }
    if (!c.hitboxTrail) return;
    push(g_trail1, pl->m_player1);
    if (dual) push(g_trail2, pl->m_player2);
    else g_trail2.clear();
}

// New level: the object cache and the draw node belong to the old one.
void enter() {
    g_owner = nullptr;
    g_count = 0;
    g_sorted.clear();
    g_node = nullptr;
    reset();
}

// Every death / restart: only the trails. The object cache stays (sorting the whole level again on every
// death caused a hitch each time).
void reset() {
    g_trail1.clear();
    g_trail2.clear();
    g_center1.clear();
    g_center2.clear();
}

static void drawAll(PlayLayer* pl, CCDrawNode* node, Cfg const& c);

void update(PlayLayer* pl) {
    auto& c = Cfg::get();
    static bool s_drawn = false;
    if (!c.hitboxes && !c.hitboxTrail && !c.hitborders) {
        if (s_drawn && g_node) g_node->clear();     // nothing on: no work at all after the first clear
        s_drawn = false;
        return;
    }
    if (path::simulating() || !pl->m_player1 || !pl->m_objectLayer) return;
    auto node = getNode(pl);
    node->clear();
    s_drawn = true;
    try {
        drawAll(pl, node, c);
    } catch (...) {
        // never take the game down because of an overlay (e.g. out of memory on an extreme level)
        node->clear();
    }
}

static void drawAll(PlayLayer* pl, CCDrawNode* node, Cfg const& c) {
    ensureCache(pl);

    // Hitborders and hitboxes are independent: turn both on to get the NaN look (merged danger area underneath,
    // the real object hitboxes drawn on top). Anything drawn later ends up above what was drawn before.
    if (c.hitborders) drawHitborders(node, pl);

    if (c.hitboxTrail) {
        drawTrail(node, g_trail1);
        drawTrail(node, g_trail2);
    }
    if (!c.hitboxes) return;

    float x0, x1;
    viewRange(pl, x0, x1);
    forNear(x0, x1, [&](GameObject* obj) { drawObject(node, obj); });
    if (c.playerHitbox) {
        drawPlayer(node, pl->m_player1);
        if (pl->m_gameState.m_isDualMode) drawPlayer(node, pl->m_player2);
    }
}

} // namespace kt::hitbox
```

## src/Path.cpp

```cpp
// Pathfinder: steps a dummy copy of the player forward twice - once holding, once released - and draws both
// trajectories (green = hold, red = release). Recomputed every frame and drawn tick by tick, so it is smooth.
//
// The dummy is an exact copy of the real player (every physics field, see PlayerState.hpp) and uses the game's
// own collision, so it reacts to blocks, slopes, pads, orbs, speed portals and gravity portals like the real
// player would. Things that would change the REAL level are switched off for the moment of the preview:
//   - gamemode / dual / mirror portals (they move the ground, split the screen or flip it) - the line ends there,
//   - coins, keys and touch triggers (they would be collected / fired),
//   - breakable blocks act as normal blocks (they would break).
// Objects' "activated" flags and the level's last-portal memory are saved before and put back after, and the
// dummy plays no effects or sounds.
//
// GD's physics works in "60 fps frame" units: one 240 TPS tick is dt = 0.25.
#include "Common.hpp"
#include "PlayerState.hpp"
#include <algorithm>

using namespace geode::prelude;

namespace kt::path {

namespace {
    constexpr float kTick = 0.25f;
    bool g_sim = false;
    bool g_simDead = false;
    PlayerObject* g_fake = nullptr;
    PlayLayer* g_owner = nullptr;
    CCDrawNode* g_node = nullptr;
    std::vector<CCPoint> g_hold, g_free;

    // every object that is not a plain block / slope / spike / decoration, sorted by x
    std::vector<GameObject*> g_inter;
    unsigned g_count = 0;
    bool g_built = false;

    struct Saved {
        GameObject* obj;
        GameObjectType type;
        bool activated;
    };
    std::vector<Saved> g_saved;
    std::vector<GameObject*> g_enders;      // gamemode / dual / mirror portals inside the preview range

    bool isBlock(GameObjectType t) {
        switch (t) {
            case GameObjectType::Solid:
            case GameObjectType::Slope:
            case GameObjectType::Hazard:
            case GameObjectType::AnimatedHazard:
            case GameObjectType::Decoration: return true;
            default: return false;
        }
    }

    void ensureCache(PlayLayer* pl) {
        unsigned n = pl->m_objects ? pl->m_objects->count() : 0;
        if (g_built && n == g_count) return;
        g_built = true;
        g_count = n;
        g_inter.clear();
        if (!pl->m_objects) return;
        for (auto obj : CCArrayExt<GameObject*>(pl->m_objects)) {
            if (obj && !isBlock(obj->m_objectType)) g_inter.push_back(obj);
        }
        std::sort(g_inter.begin(), g_inter.end(),
                  [](GameObject* a, GameObject* b) { return a->getPositionX() < b->getPositionX(); });
    }

    // portals that change the whole level (ground position, dual screen, mirror): the preview ends there
    bool endsPreview(int id) {
        switch (id) {
            case 12: case 13: case 47: case 111: case 660: case 745: case 1331: case 1933:   // gamemodes
            case 286: case 287:                                                              // dual
            case 45: case 46:                                                                // mirror
                return true;
            default: return false;
        }
    }
    // objects the dummy must never touch at all
    bool untouchable(GameObject* o) {
        if (o->m_isTrigger) return true;                                  // touch triggers would fire
        switch (o->m_objectID) {
            case 142: case 1329:                                          // secret / user coins
            case 1275: case 1587: case 1588: case 1589: case 1598: case 1614:   // keys / collectibles
                return true;
            default: return endsPreview(o->m_objectID);
        }
    }

    void protect(float x0, float x1) {
        g_saved.clear();
        g_enders.clear();
        auto it = std::lower_bound(g_inter.begin(), g_inter.end(), x0,
                                   [](GameObject* o, float x) { return o->getPositionX() < x; });
        for (; it != g_inter.end() && (*it)->getPositionX() <= x1; ++it) {
            auto o = *it;
            g_saved.push_back({o, o->m_objectType, o->m_isActivated});
            if (o->m_objectType == GameObjectType::Breakable) o->m_objectType = GameObjectType::Solid;
            else if (untouchable(o)) o->m_objectType = GameObjectType::Decoration;
            if (endsPreview(o->m_objectID)) g_enders.push_back(o);
        }
    }
    void unprotect() {
        for (auto& s : g_saved) {
            s.obj->m_objectType = s.type;
            s.obj->m_isActivated = s.activated;
        }
        g_saved.clear();
    }

    bool hitsEnder() {
        if (g_enders.empty()) return false;
        auto r = g_fake->getObjectRect();
        for (auto o : g_enders) {
            if (r.intersectsRect(o->getObjectRect())) return true;
        }
        return false;
    }

    void run(PlayLayer* pl, PlayerObject* real, bool hold, int steps, std::vector<CCPoint>& pts) {
        pts.clear();
        g_fake->copyAttributes(real);
        copyPlayer(g_fake, real);              // exact physics copy of the real player
        g_fake->m_playEffects = false;         // no particles / sounds from the dummy
        g_fake->setVisible(false);

        g_sim = true;
        g_simDead = false;
        // g_sim must never stay on (the hitboxes stop being redrawn while it is on), whatever happens inside
        struct SimOff { ~SimOff() { g_sim = false; } } simOff;
        if (hold) g_fake->pushButton(PlayerButton::Jump);
        else g_fake->releaseButton(PlayerButton::Jump);
        pts.push_back(g_fake->getPosition());
        for (int i = 0; i < steps && !g_simDead; ++i) {
            g_fake->update(kTick);
            pl->checkCollisions(g_fake, kTick, false);
            pts.push_back(g_fake->getPosition());
            if (hitsEnder()) break;
        }
    }

    void draw() {
        g_node->clear();
        auto line = [](std::vector<CCPoint> const& v, ccColor4F col) {
            for (size_t i = 1; i < v.size(); ++i) g_node->drawSegment(v[i - 1], v[i], 0.9f, col);
        };
        line(g_hold, {0.3f, 1.f, 0.5f, 0.9f});
        line(g_free, {1.f, 0.35f, 0.35f, 0.9f});
    }
}

bool simulating() { return g_sim; }

// Only real obstacles end the preview.
void onSimDeath(GameObject* obj) {
    if (obj) {
        switch (obj->m_objectType) {
            case GameObjectType::Hazard:
            case GameObjectType::AnimatedHazard:
            case GameObjectType::Solid:
            case GameObjectType::Slope:
            case GameObjectType::Breakable: break;
            default: return;
        }
    }
    g_simDead = true;
}

void reset() {
    if (g_fake) {
        g_fake->release();
        g_fake = nullptr;
    }
    g_owner = nullptr;
    g_node = nullptr;          // belongs to the old level
    g_inter.clear();
    g_count = 0;
    g_built = false;
    g_hold.clear();
    g_free.clear();
    g_sim = false;
}

void forget() {
    g_fake = nullptr;          // its level was destroyed without telling us: leaking one dummy beats a crash
    reset();
}

void update(PlayLayer* pl) {
    auto& c = Cfg::get();
    bool on = c.pathfinder && pl->m_player1 && !pl->m_player1->m_isDead && !pl->m_isPaused && !counter::isCounting();
    if (!on) {
        if (g_node && g_node->isVisible()) g_node->setVisible(false);
        return;
    }

    if (g_owner != pl || !g_fake) {
        reset();
        g_owner = pl;
        g_fake = PlayerObject::create(1, 1, pl, pl->m_objectLayer, false);
        if (!g_fake) return;
        g_fake->retain();
        g_fake->setVisible(false);
    }
    if (!g_node) {
        g_node = CCDrawNode::create();
        pl->m_objectLayer->addChild(g_node, 9998);
    }
    if (!g_node->isVisible()) g_node->setVisible(true);

    ensureCache(pl);
    int steps = std::max(10, c.pathLen) * 4;   // length is in 60 fps frames -> 4 ticks each
    float px = pl->m_player1->getPositionX();
    // what the level remembers about the last portal is restored too
    auto lastPortal1 = pl->m_gameState.m_lastActivatedPortal1;
    auto lastPortal2 = pl->m_gameState.m_lastActivatedPortal2;
    protect(px - 150.f, px + steps * kTick * 12.f + 150.f);
    run(pl, pl->m_player1, true, steps, g_hold);
    run(pl, pl->m_player1, false, steps, g_free);
    unprotect();
    pl->m_gameState.m_lastActivatedPortal1 = lastPortal1;
    pl->m_gameState.m_lastActivatedPortal2 = lastPortal2;
    draw();
}

} // namespace kt::path
```

## src/Cheats.cpp

```cpp
// noclip, speedhack + physical TPS, practice music sync, layout mode.
#include "Common.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <unordered_map>

using namespace geode::prelude;

namespace kt::noclip {
namespace {
    int g_steps = 0, g_bad = 0;
    bool g_hitThisStep = false;
}
void onHit() { g_hitThisStep = true; }
void onStep() {
    ++g_steps;
    if (g_hitThisStep) ++g_bad;
    g_hitThisStep = false;
}
float accuracy() { return g_steps ? 100.f * (1.f - static_cast<float>(g_bad) / g_steps) : 100.f; }
void reset() { g_steps = g_bad = 0; g_hitThisStep = false; }
}

namespace kt::speed {
namespace {
    float g_lastPitch = -1.f;
    float g_acc = 0.f;
    constexpr int kMaxUpdates = 40;      // physics updates per rendered frame (keeps slow devices alive)
    int g_stepCount = 0;
    float g_tps = 0.f;
    std::chrono::steady_clock::time_point g_t0 = std::chrono::steady_clock::now();

    float userSpeed() {
        auto& c = Cfg::get();
        return c.speedhack ? std::clamp(c.speed, 0.01f, 10.f) : 1.f;
    }

    // The speedhack only acts while a level is really being played: not in menus, not while paused,
    // not on the level-complete screen.
    float levelSpeed() {
        auto pl = PlayLayer::get();
        if (!pl || pl->m_isPaused || pl->m_hasCompletedLevel || pl->m_levelEndAnimationStarted) return 1.f;
        return userSpeed();
    }
}

// Speedhack = every frame's delta time is multiplied (CCScheduler::update hook in Hooks.cpp). Reapplied every
// frame, so it keeps working after GD resets things, and combines with the counting speed.
float factor() { return levelSpeed() * counter::speedFactor(); }

void updatePitch() {
    float p = levelSpeed();
    bool need = p != 1.f || g_lastPitch != 1.f;     // songs create new channels, so keep reapplying while != 1
    g_lastPitch = p;
    if (!need) return;
    if (auto fmod = FMODAudioEngine::sharedEngine()) {
        if (fmod->m_backgroundMusicChannel) fmod->m_backgroundMusicChannel->setPitch(p);
    }
}

// Physical TPS (240..1000). GD already splits every update into 1/240 s steps, so the way to change the tick
// rate is to call the game's own update() several times per frame with a small fixed dt = 1/TPS (one physics
// step each). Hooking getModifiedDelta alone breaks time-warp physics.
// Below 240 TPS the game itself splits again into 1/240 steps, so lower values are not supported.
int planSteps(float dt, float& step) {
    step = 1.f / std::clamp(Cfg::get().tps, 240.f, 2000.f);
    g_acc += dt;
    int n = static_cast<int>(g_acc / step);
    if (n > kMaxUpdates) { n = kMaxUpdates; g_acc = 0.f; }
    else g_acc -= n * step;
    return n;
}

void onStep() {
    ++g_stepCount;
    auto now = std::chrono::steady_clock::now();
    float el = std::chrono::duration<float>(now - g_t0).count();
    if (el >= 1.f) {
        g_tps = g_stepCount / el;
        g_stepCount = 0;
        g_t0 = now;
    }
}
float measuredTps() { return g_tps; }
}

namespace kt::practice {
// Native GD restarts the song from the level start after a checkpoint respawn. This seeks it to where the
// player actually is: the level time restored from the checkpoint, plus the song start offset.
void sync(PlayLayer* pl) {
    if (!Cfg::get().practiceSync || !pl->m_isPracticeMode || !pl->m_player1) return;
    if (counter::isCounting()) return;
    double t = pl->m_gameState.m_levelTime;
    double offset = pl->m_levelSettings ? pl->m_levelSettings->m_songOffset : 0.0;
    unsigned ms = static_cast<unsigned>(std::max(0.0, (t + offset) * 1000.0));
    FMODAudioEngine::sharedEngine()->setMusicTimeMS(ms, true, 0);
}
}

// Layout mode:
//  - background and ground are the default blue and stay that way (color triggers are ignored),
//  - decoration is hidden,
//  - every object with a hitbox is white - also hidden / invisible blocks - and has no glow.
// The object loop only runs when the setting actually changed (or a new level opened), never on every death.
// Turning it off restores the objects; the colors come back with the next restart.
namespace kt::layout {
namespace {
    constexpr int kWhite = 1011;
    constexpr ccColor3B kBg{40, 125, 255};       // GD's default background blue
    constexpr ccColor3B kGround{0, 102, 255};    // GD's default ground blue
    struct Orig {
        bool hide;
        bool invisible;
        int base;
        int detail;
        bool glow;
    };
    PlayLayer* g_owner = nullptr;
    bool g_applied = false;
    std::unordered_map<GameObject*, Orig> g_orig;

    bool isDeco(GameObject* o) {
        return o->m_objectType == GameObjectType::Decoration && !o->m_isTrigger;
    }
    bool hasHitbox(GameObject* o) {
        switch (o->m_objectType) {
            case GameObjectType::Decoration:
            case GameObjectType::Modifier:
            case GameObjectType::CollisionObject:
            case GameObjectType::EnterEffectObject:
            case GameObjectType::Special: return false;
            default: return true;
        }
    }
}

// Called by the trigger hooks (Hooks.cpp) whenever a trigger fires. In layout mode the triggers that change
// colors / opacity do nothing - they stay in the level, they just have no effect.
// (They used to be left out of the level while it loaded. GD still kept pointers to the left-out objects -
// groups, linked objects, portals - and big custom levels crashed right when they were entered.)
bool blockTrigger(GameObject* o) {
    if (!Cfg::get().layoutMode || !o) return false;
    switch (o->m_objectID) {
        case 899:                                           // color trigger
        case 29: case 30: case 104: case 105: case 221:     // old color triggers (BG, G, line, obj, col 1)
        case 717: case 718: case 743: case 744:             // old color triggers (col 2-4, 3DL)
        case 900: case 915:                                 // G2 / line color triggers
        case 1006:                                          // pulse trigger
        case 1007:                                          // alpha trigger (would make blocks invisible)
        case 2903:                                          // gradient trigger
        case 3029: case 3030: case 3031:                    // BG / G / MG change triggers
            return true;
        default: return false;
    }
}

// Called by the PlayLayer::updateColor hook (every color change of the level goes through it).
// Returns false to ignore the change.
bool filterColor(int colorID, ccColor3B& color, float& fadeTime, int& copyID) {
    if (!Cfg::get().layoutMode) return true;
    switch (colorID) {
        case 1000: color = kBg; break;                 // background
        case 1001: case 1009: color = kGround; break;  // ground 1 / ground 2
        default: return false;                         // every other color trigger does nothing
    }
    fadeTime = 0.f;
    copyID = 0;
    return true;
}

// A new level can be created at the same memory address as the previous one, so "is this the same level"
// cannot be judged by the pointer alone: forget everything whenever a level is left or entered.
void reset() {
    g_owner = nullptr;
    g_applied = false;
    g_orig.clear();
}

void apply(PlayLayer* pl) {
    if (g_owner != pl) {
        g_owner = pl;
        g_orig.clear();
        g_applied = false;
    }
    bool any = Cfg::get().layoutMode;
    if (any) {
        // set background and ground to blue right away (cheap: three channels)
        for (int id : {1000, 1001, 1009}) {
            ccColor3B col = id == 1000 ? kBg : kGround;
            ccHSVValue hsv{};
            pl->updateColor(col, 0.f, id, false, 1.f, hsv, 0, false, nullptr, 0, 0);
        }
    }
    if (any == g_applied) return;          // objects already done: no work at all
    if (!pl->m_objects) return;
    bool hideDeco = any, white = any, noGlow = any;

    for (auto obj : CCArrayExt<GameObject*>(pl->m_objects)) {
        if (!obj) continue;
        bool deco = isDeco(obj), solid = hasHitbox(obj);
        if (!deco && !solid) continue;
        if (!g_orig.count(obj)) {
            if (!any) continue;
            g_orig[obj] = {obj->m_isHide, obj->m_isInvisibleBlock,
                           obj->m_baseColor ? obj->m_baseColor->m_colorID : 0,
                           obj->m_detailColor ? obj->m_detailColor->m_colorID : 0,
                           obj->m_glowSprite ? obj->m_glowSprite->isVisible() : false};
        }
        auto& o = g_orig[obj];
        // back to the original first, then apply whatever is on now
        obj->m_isHide = o.hide;
        obj->m_isInvisibleBlock = o.invisible;
        if (obj->m_baseColor) obj->m_baseColor->m_colorID = o.base;
        if (obj->m_detailColor) obj->m_detailColor->m_colorID = o.detail;
        if (obj->m_glowSprite) obj->m_glowSprite->setVisible(o.glow);

        if (hideDeco && deco) {
            obj->m_isHide = true;
            obj->setVisible(false);
        }
        if (white && solid) {
            // white channel 1011 is always fully opaque, so blocks that were see-through become visible too
            obj->m_isHide = false;
            obj->m_isInvisibleBlock = false;
            if (obj->m_baseColor) obj->m_baseColor->m_colorID = kWhite;
            if (obj->m_detailColor) obj->m_detailColor->m_colorID = kWhite;
        }
        if (noGlow && obj->m_glowSprite) obj->m_glowSprite->setVisible(false);
    }
    if (!any) g_orig.clear();
    g_applied = any;
}
}
```

## src/View.cpp

```cpp
// Window counter overlay (Antic's Counter / NaN Counter look):
//  - while counting: the paths of the trials (white = survived, red = died), "Counting.. 10/26 / ETA ~51s",
//    and a donut ring for every press / release the moment its window is finished - one by one.
//  - while playing (yourself or the bot): the run's path, and the donut rings appear one by one as you reach
//    each press / release. Top right: the window of the last one, in frames and milliseconds (2 decimals).
//  - "Show Results List": the 0-1:, 2:, 3: ... list on the left. It also counts up one by one: while counting
//    it grows with every finished window, while playing with every press / release you reach.
//
// Performance: nothing here is rebuilt every frame. Nodes are created once per level and kept as pointers,
// rings / list / text only change when something new happened, everything drawn in the level is split into
// chunks along x and only the chunks near the player are visible (re-checked only when the player enters
// another chunk).
#include "Common.hpp"
#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdio>
#include <map>

using namespace geode::prelude;

namespace kt::view {

namespace {
    constexpr float kChunk = 480.f;

    struct Bucket {
        char const* label;
        int lo, hi;
        ccColor3B col;
    };
    constexpr Bucket kBuckets[] = {
        {"0-1:", 0, 1, {255, 80, 80}},
        {"2:", 2, 2, {255, 160, 70}},
        {"3:", 3, 3, {255, 225, 90}},
        {"4:", 4, 4, {255, 255, 255}},
        {"5-6:", 5, 6, {120, 235, 120}},
        {"7-8:", 7, 8, {110, 215, 230}},
        {"9-12:", 9, 12, {130, 150, 255}},
        {"13-16:", 13, 16, {185, 120, 255}},
        {"17-24:", 17, 999, {255, 120, 210}},
    };
    constexpr int kBucketCount = sizeof(kBuckets) / sizeof(kBuckets[0]);

    int bucketOf(int w) {
        for (int b = 0; b < kBucketCount; ++b) if (w >= kBuckets[b].lo && w <= kBuckets[b].hi) return b;
        return kBucketCount - 1;
    }
    ccColor3B colorFor(int w) { return kBuckets[bucketOf(w)].col; }
    ccColor4F c4(ccColor3B c, float a = 1.f) { return {c.r / 255.f, c.g / 255.f, c.b / 255.f, a}; }

    // thick ring ("donut")
    void donut(CCDrawNode* n, CCPoint c, float r, float thick, ccColor4F col) {
        constexpr int seg = 20;
        CCPoint prev(c.x + r, c.y);
        for (int i = 1; i <= seg; ++i) {
            float a = 6.2831853f * i / seg;
            CCPoint p(c.x + r * std::cos(a), c.y + r * std::sin(a));
            n->drawSegment(prev, p, thick / 2.f, col);
            prev = p;
        }
    }

    CCLabelBMFont* label(CCNode* parent, std::string const& s, CCPoint pos, float scale, ccColor3B col, CCPoint anchor) {
        auto l = CCLabelBMFont::create(s.c_str(), "bigFont.fnt");
        l->setScale(scale);
        l->setColor(col);
        l->setAnchorPoint(anchor);
        l->setPosition(pos);
        parent->addChild(l);
        return l;
    }

    float tickMs() {
        auto& c = Cfg::get();
        return 1000.f / (c.tpsBypass ? c.tps : 240.f);
    }

    void drop(CCNode*& n) {
        if (n) n->removeFromParentAndCleanup(true);
        n = nullptr;
    }

    // ---- chunked node: one child per 480px of the level, only nearby chunks are visible ----
    struct Chunk {
        CCNode* node;
        CCDrawNode* draw;
    };
    struct Chunked {
        std::map<int, Chunk> chunks;
        bool dirty = true;
        int lastChunk = INT_MIN;
        Chunk& at(CCNode* root, float x) {
            int i = static_cast<int>(std::floor(x / kChunk));
            auto it = chunks.find(i);
            if (it != chunks.end()) return it->second;
            auto node = CCNode::create();
            auto draw = CCDrawNode::create();
            node->addChild(draw);
            root->addChild(node);
            dirty = true;
            return chunks[i] = Chunk{node, draw};
        }
        void clear() {
            chunks.clear();
            dirty = true;
            lastChunk = INT_MIN;
        }
        void cull(PlayLayer* pl) {
            float px = pl->m_player1 ? pl->m_player1->getPositionX() : 0.f;
            int cur = static_cast<int>(std::floor(px / kChunk));
            if (!dirty && cur == lastChunk) return;
            dirty = false;
            lastChunk = cur;
            int span = static_cast<int>(CCDirector::get()->getWinSize().width * 1.5f / kChunk) + 1;
            for (auto& [i, c] : chunks) c.node->setVisible(i >= cur - span && i <= cur + span);
        }
    };

    CCNode* newRoot(CCNode* parent, int z) {
        auto root = CCNode::create();
        parent->addChild(root, z);
        return root;
    }

    PlayLayer* g_layer = nullptr;

    // ---- the run's path ----
    CCNode* g_pathRoot = nullptr;
    Chunked g_pathCh;
    size_t g_pathSize = 0;
    CCPoint g_pathEnd;

    void updatePath(PlayLayer* pl, bool show) {
        auto& path = counter::pathPoints();
        if (!show || counter::baselineRunning() || path.size() < 2) {
            if (g_pathRoot) { drop(g_pathRoot); g_pathCh.clear(); }
            return;
        }
        // the path only changes when a replay is loaded or counted, so this almost never rebuilds
        if (!g_pathRoot || g_pathSize != path.size() || !g_pathEnd.equals(path.back())) {
            drop(g_pathRoot);
            g_pathCh.clear();
            g_pathRoot = newRoot(pl->m_objectLayer, 9996);
            ccColor4F line{1.f, 1.f, 1.f, 0.85f};
            for (size_t i = 1; i < path.size(); ++i) g_pathCh.at(g_pathRoot, path[i - 1].x).draw->drawSegment(path[i - 1], path[i], 1.1f, line);
            g_pathSize = path.size();
            g_pathEnd = path.back();
        }
        g_pathCh.cull(pl);
    }

    // ---- donut rings, added one by one ----
    CCNode* g_ringsRoot = nullptr;
    Chunked g_ringsCh;
    bool g_ringsCounting = false;
    size_t g_ringsShown = 0;
    int g_ringsVersion = -1;
    std::vector<counter::Cue> g_countList;     // counted so far (refreshed only when something got counted)
    int g_countListVersion = -1;

    void addRing(counter::Cue const& c) {
        if (c.pos.x < -1e8f) return;     // no position known (old replay): number only shows top right
        auto col = colorFor(c.window);
        auto& chunk = g_ringsCh.at(g_ringsRoot, c.pos.x);
        float r = c.release ? 6.f : 9.f;
        donut(chunk.draw, c.pos, r, c.release ? 2.6f : 3.6f, c4(col, 0.95f));
        label(chunk.node, std::to_string(c.window), c.pos + ccp(-(r + 4.f), 0.f), c.release ? 0.3f : 0.4f, col, {1.f, 0.5f});
    }

    void updateRings(PlayLayer* pl, bool show) {
        if (!show) {
            if (g_ringsRoot) { drop(g_ringsRoot); g_ringsCh.clear(); }
            return;
        }
        bool counting = counter::isCounting();
        std::vector<counter::Cue> const* list;
        size_t n;
        if (counting) {
            if (g_countListVersion != counter::version()) {
                g_countList = counter::countedCues();
                g_countListVersion = counter::version();
            }
            list = &g_countList;
            n = list->size();
        } else {
            list = &counter::cues();
            n = std::min(counter::cueCursor(), list->size());
        }
        bool reset = !g_ringsRoot || g_ringsCounting != counting || n < g_ringsShown ||
                     (!counting && g_ringsVersion != counter::version());
        if (reset) {
            drop(g_ringsRoot);
            g_ringsCh.clear();
            g_ringsRoot = newRoot(pl->m_objectLayer, 9998);
            g_ringsShown = 0;
            g_ringsCounting = counting;
            g_ringsVersion = counter::version();
        }
        for (size_t i = g_ringsShown; i < n; ++i) addRing((*list)[i]);
        g_ringsShown = n;
        g_ringsCh.cull(pl);
    }

    // ---- top right: the last reached window, 2 decimals ----
    CCLabelBMFont* g_last = nullptr;
    int g_lastShown = -2;
    int g_lastVersion = -1;

    void updateLast(PlayLayer* pl, bool show) {
        int idx = counter::lastCue();
        auto& all = counter::cues();
        if (!show || idx < 0 || idx >= static_cast<int>(all.size())) {
            if (g_last && g_last->isVisible()) g_last->setVisible(false);
            g_lastShown = -2;
            return;
        }
        if (!g_last) {
            auto win = CCDirector::get()->getWinSize();
            g_last = CCLabelBMFont::create("", "bigFont.fnt");
            g_last->setScale(0.45f);
            g_last->setAnchorPoint({1.f, 0.5f});
            g_last->setPosition({win.width - 10.f, win.height - 46.f});
            pl->m_uiLayer->addChild(g_last, 95);
            g_lastShown = -2;
        }
        if (!g_last->isVisible()) g_last->setVisible(true);
        if (idx == g_lastShown && g_lastVersion == counter::version()) return;
        g_lastShown = idx;
        g_lastVersion = counter::version();
        auto& c = all[idx];
        char buf[64];
        std::snprintf(buf, sizeof buf, "%s%df  %.2fms", c.release ? "rel " : "", c.window, c.window * tickMs());
        g_last->setString(buf);
        g_last->setColor(colorFor(c.window));
    }

    // ---- left side bucket list, counting up one by one ----
    CCNode* g_stats = nullptr;
    CCLabelBMFont* g_statNum[kBucketCount] = {};
    CCLabelBMFont* g_statPct = nullptr;
    CCLabelBMFont* g_statAvg = nullptr;
    long long g_statKey = -1;

    void updateStats(PlayLayer* pl, bool show) {
        if (!show) {
            if (g_stats) { drop(g_stats); g_statKey = -1; }
            return;
        }
        bool counting = counter::isCounting();
        // what is shown depends only on this key, so the labels are only touched when it changes
        long long key = counting ? (1LL << 40) + counter::version()
                                 : (static_cast<long long>(counter::version()) << 20) + static_cast<long long>(counter::cueCursor());
        if (g_stats && key == g_statKey) return;
        g_statKey = key;

        if (!g_stats) {
            g_stats = newRoot(pl->m_uiLayer, 90);
            auto win = CCDirector::get()->getWinSize();
            float y = win.height - 14.f;
            for (int b = 0; b < kBucketCount; ++b) {
                label(g_stats, kBuckets[b].label, {6.f, y}, 0.4f, kBuckets[b].col, {0.f, 0.5f});
                g_statNum[b] = label(g_stats, "0", {96.f, y}, 0.4f, kBuckets[b].col, {1.f, 0.5f});
                y -= 16.f;
            }
            g_statPct = label(g_stats, "0.00%", {6.f, 26.f}, 0.4f, {255, 255, 255}, {0.f, 0.5f});
            g_statAvg = label(g_stats, "0.00", {6.f, 10.f}, 0.4f, {255, 255, 255}, {0.f, 0.5f});
        }

        int counts[kBucketCount] = {};
        int total = 0, shown = 0;
        float sum = 0.f;
        auto add = [&](int w) {
            ++shown;
            sum += w;
            ++counts[bucketOf(w)];
        };
        for (auto& r : counter::results()) {
            ++total;
            if (r.relFrame >= 0) ++total;
            if (counting) {                 // while counting: every window finished so far
                if (r.counted) add(r.window());
                if (r.relCounted && r.relFrame >= 0) add(r.relWindow());
            }
        }
        if (!counting) {                    // while playing: only what you have reached in this attempt
            auto& all = counter::cues();
            size_t n = std::min(counter::cueCursor(), all.size());
            for (size_t i = 0; i < n; ++i) add(all[i].window);
        }

        char buf[48];
        for (int b = 0; b < kBucketCount; ++b) g_statNum[b]->setString(std::to_string(counts[b]).c_str());
        std::snprintf(buf, sizeof buf, "%.2f%%", total ? 100.f * shown / total : 0.f);
        g_statPct->setString(buf);
        std::snprintf(buf, sizeof buf, "%.2f", shown ? sum / shown : 0.f);
        g_statAvg->setString(buf);
    }

    // ---- while counting: trial paths + status text ----
    CCDrawNode* g_lines = nullptr;     // finished trials, redrawn only when a trial finishes
    CCDrawNode* g_cur = nullptr;       // running trial, only the new segments are appended
    CCLabelBMFont* g_text = nullptr;
    int g_linesSerial = -1;
    size_t g_curDrawn = 0;
    std::string g_textStr;

    void updateCounting(PlayLayer* pl, bool on) {
        if (!on) {
            if (g_lines || g_cur || g_text) {
                CCNode* a = g_lines; drop(a); g_lines = nullptr;
                CCNode* b = g_cur; drop(b); g_cur = nullptr;
                CCNode* t = g_text; drop(t); g_text = nullptr;
                g_linesSerial = -1;
                g_curDrawn = 0;
                g_textStr.clear();
            }
            return;
        }
        if (!g_lines) {
            g_lines = CCDrawNode::create();
            pl->m_objectLayer->addChild(g_lines, 9997);
            g_cur = CCDrawNode::create();
            pl->m_objectLayer->addChild(g_cur, 9997);
            auto win = CCDirector::get()->getWinSize();
            g_text = CCLabelBMFont::create("", "bigFont.fnt");
            g_text->setScale(0.45f);
            g_text->setAlignment(kCCTextAlignmentCenter);
            g_text->setPosition({win.width / 2.f, 48.f});
            pl->m_uiLayer->addChild(g_text, 95);
            g_linesSerial = -1;
            g_curDrawn = 0;
            g_textStr.clear();
        }
        if (g_linesSerial != counter::trialSerial()) {
            g_linesSerial = counter::trialSerial();
            g_lines->clear();
            for (auto& l : counter::trialLines()) {
                ccColor4F col = l.ok ? ccColor4F{1.f, 1.f, 1.f, 0.75f} : ccColor4F{1.f, 0.35f, 0.45f, 0.75f};
                for (size_t i = 1; i < l.pts.size(); ++i) g_lines->drawSegment(l.pts[i - 1], l.pts[i], 0.8f, col);
            }
        }
        auto& cur = counter::currentTrialPath();
        if (cur.size() < g_curDrawn) {
            g_cur->clear();
            g_curDrawn = 0;
        }
        for (size_t i = std::max<size_t>(1, g_curDrawn); i < cur.size(); ++i)
            g_cur->drawSegment(cur[i - 1], cur[i], 0.8f, {1.f, 1.f, 1.f, 0.95f});
        g_curDrawn = cur.size();

        auto t = counter::countingText();
        if (t != g_textStr) {
            g_textStr = t;
            g_text->setString(t.c_str());
        }
    }
}

void reset() {
    // the nodes belong to the old level and die with it - just forget the pointers
    g_layer = nullptr;
    g_pathRoot = nullptr; g_pathCh.clear(); g_pathSize = 0;
    g_ringsRoot = nullptr; g_ringsCh.clear(); g_ringsShown = 0; g_ringsVersion = -1; g_countListVersion = -1;
    g_last = nullptr; g_lastShown = -2;
    g_stats = nullptr; g_statPct = g_statAvg = nullptr; g_statKey = -1;
    for (auto& l : g_statNum) l = nullptr;
    g_lines = g_cur = nullptr; g_text = nullptr; g_linesSerial = -1; g_curDrawn = 0; g_textStr.clear();
}

// Two switches:
//  - "Counter overlay" (Display tab): path, donut rings, numbers, the top right value, the counting text
//  - "Show Results List" (Window counter tab): the 0-1:, 2:, 3: ... list on the left
void update(PlayLayer* pl) {
    if (pl != g_layer) {
        reset();
        g_layer = pl;
    }
    auto& c = Cfg::get();
    bool counting = counter::isCounting();
    updateCounting(pl, counting && c.counterOverlay);
    updatePath(pl, c.counterOverlay);
    updateRings(pl, c.counterOverlay);
    updateLast(pl, c.counterOverlay && !counting);
    updateStats(pl, c.counterList);
}

} // namespace kt::view
```

## src/Hud.cpp

```cpp
// Small info text (bottom right) and the big "3f" flash in the middle.
// Performance: the labels are kept as pointers and the text is only set again when it really changed
// (setting a bitmap font label rebuilds all its letters, which is expensive when done every frame).
#include "Common.hpp"
#include <cstdio>

using namespace geode::prelude;

namespace kt::hud {

namespace {
    CCLabelBMFont* g_info = nullptr;
    CCLabelBMFont* g_flash = nullptr;
    std::string g_infoText;
    PlayLayer* g_owner = nullptr;
}

void attach(PlayLayer* pl) {
    auto win = CCDirector::get()->getWinSize();
    g_owner = pl;

    g_info = CCLabelBMFont::create("", "bigFont.fnt");
    g_info->setScale(0.32f);
    g_info->setAnchorPoint({1.f, 0.f});          // bottom right, so it never covers the counter stats
    g_info->setAlignment(kCCTextAlignmentRight);
    g_info->setOpacity(190);
    g_info->setPosition({win.width - 8.f, 8.f});
    pl->m_uiLayer->addChild(g_info, 100);
    g_infoText.clear();

    g_flash = CCLabelBMFont::create("", "bigFont.fnt");
    g_flash->setScale(0.9f);
    g_flash->setOpacity(0);
    g_flash->setColor({77, 214, 164});
    g_flash->setPosition({win.width / 2.f, win.height * 0.78f});
    pl->m_uiLayer->addChild(g_flash, 100);
}

void detach() {
    g_info = nullptr;
    g_flash = nullptr;
    g_owner = nullptr;
    g_infoText.clear();
}

void update(PlayLayer* pl) {
    if (!g_info || pl != g_owner) return;
    auto& c = Cfg::get();
    std::string text;
    char buf[64];
    if (c.showTps) {
        std::snprintf(buf, sizeof buf, "TPS %.0f\n", speed::measuredTps());
        text += buf;
    }
    if (c.noclip) {
        std::snprintf(buf, sizeof buf, "Noclip %.1f%%\n", noclip::accuracy());
        text += buf;
    }
    if (counter::isRecording()) text += counter::statusText() + "\n";   // counting has its own big text
    if (text == g_infoText) return;
    g_infoText = text;
    g_info->setString(text.c_str());
}

void flash(std::string const& text) {
    if (!g_flash || !g_owner || PlayLayer::get() != g_owner) return;
    g_flash->stopAllActions();
    g_flash->setString(text.c_str());
    g_flash->setOpacity(255);
    g_flash->runAction(CCSequence::create(CCDelayTime::create(0.15f), CCFadeOut::create(0.45f), nullptr));
}

} // namespace kt::hud
```

## src/Menu.cpp

```cpp
// The toolkit menu: dark rounded panel, icon sidebar on the left, settings on the right.
// Built on FLAlertLayer directly (no Popup template) so it does not depend on the Popup API of a given Geode version.
#include "Common.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>

using namespace geode::prelude;

namespace kt::ui {

namespace {
    constexpr ccColor3B kPanel{22, 26, 36};
    constexpr ccColor3B kSide{16, 19, 27};
    constexpr ccColor3B kAccent{77, 214, 164};
    constexpr ccColor3B kMuted{120, 128, 145};
    constexpr ccColor3B kText{225, 230, 240};

    enum Tab { Cheats, Hitbox, Path, Display, Replay, Counter, TabCount };
    constexpr char const* kTabIcons[TabCount] = {"kt_tab_hacks.png", "kt_tab_hitbox.png", "kt_tab_path.png",
                                                  "kt_tab_display.png", "kt_tab_speed.png", "kt_tab_counter.png"};
    constexpr char const* kTabNames[TabCount] = {"Cheats", "Hitbox", "Pathfinder", "Display", "Replay", "Window counter"};

    bool g_open = false;

    void note(std::string const& msg) {
        Notification::create(msg.c_str(), NotificationIcon::Info, 1.8f)->show();
    }

    struct BoolRef { bool* v; bool restart; };
    struct SliderRef {
        float* f; int* i; float lo, hi;
        CCLabelBMFont* label;      // value text (plain sliders)
        TextInput* input;          // typed value (editable sliders)
        Slider* slider;
        char const* fmt;
    };

    // ---------------------------------------------------------------- name prompt (New replay)
    class NamePrompt : public FLAlertLayer {
    public:
        static NamePrompt* create(std::function<void(std::string const&)> cb) {
            auto r = new NamePrompt();
            r->m_cb = std::move(cb);
            if (r->setup()) { r->autorelease(); return r; }
            CC_SAFE_DELETE(r);
            return nullptr;
        }

    private:
        std::function<void(std::string const&)> m_cb;
        TextInput* m_input = nullptr;

        bool setup() {
            if (!FLAlertLayer::init(150)) return false;
            this->setTouchEnabled(true);
            this->setKeypadEnabled(true);
            m_noElasticity = true;

            m_mainLayer = CCLayer::create();
            this->addChild(m_mainLayer);
            m_buttonMenu = CCMenu::create();
            m_buttonMenu->setPosition({0.f, 0.f});
            m_mainLayer->addChild(m_buttonMenu, 10);

            auto win = CCDirector::get()->getWinSize();
            auto c = ccp(win.width / 2, win.height / 2);

            auto panel = CCScale9Sprite::create("square02b_001.png");
            panel->setContentSize({290.f, 150.f});
            panel->setColor(kPanel);
            panel->setOpacity(250);
            panel->setPosition(c);
            m_mainLayer->addChild(panel);

            auto title = CCLabelBMFont::create("New replay", "bigFont.fnt");
            title->setScale(0.5f);
            title->setColor(kAccent);
            title->setPosition(c + ccp(0.f, 50.f));
            m_mainLayer->addChild(title);

            m_input = TextInput::create(230.f, "Replay name");
            m_input->setPosition(c + ccp(0.f, 8.f));
            m_input->setMaxCharCount(24);
            m_input->setFilter("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 _-");
            m_mainLayer->addChild(m_input, 5);

            auto cancel = CCMenuItemSpriteExtra::create(
                ButtonSprite::create("Cancel", 90, true, "bigFont.fnt", "GJ_button_04.png", 24.f, 0.5f), this,
                menu_selector(NamePrompt::onCancel));
            cancel->setPosition(c + ccp(-62.f, -44.f));
            m_buttonMenu->addChild(cancel);
            auto create = CCMenuItemSpriteExtra::create(
                ButtonSprite::create("Create", 90, true, "bigFont.fnt", "GJ_button_01.png", 24.f, 0.5f), this,
                menu_selector(NamePrompt::onCreate));
            create->setPosition(c + ccp(62.f, -44.f));
            m_buttonMenu->addChild(create);

            this->show();
            return true;
        }

        void close() {
            this->setKeypadEnabled(false);
            this->removeFromParentAndCleanup(true);
        }
        void keyBackClicked() override { close(); }
        void onCancel(CCObject*) { close(); }
        void onCreate(CCObject*) {
            auto cb = m_cb;
            std::string name = m_input->getString();
            close();
            if (cb) cb(name);
        }
    };

    // ---------------------------------------------------------------- main menu
    class ToolkitMenu : public FLAlertLayer {
    public:
        static ToolkitMenu* create() {
            auto r = new ToolkitMenu();
            if (r && r->setup()) { r->autorelease(); return r; }
            CC_SAFE_DELETE(r);
            return nullptr;
        }
        ~ToolkitMenu() {
            g_open = false;
            Cfg::get().save();      // slider changes are saved however the menu gets closed
        }

    private:
        // the sidebar holds the 6 tab buttons in 2 columns x 3 rows, so each button can be big and easy to tap
        static constexpr float H = 300.f, SIDE = 150.f;
        float W = 600.f;                   // set from the screen width in setup()
        CCNode* m_content = nullptr;
        CCMenu* m_contentMenu = nullptr;
        std::vector<CCMenuItemSpriteExtra*> m_tabBtns;
        std::vector<BoolRef> m_bools;
        std::vector<SliderRef> m_sliders;
        CCLabelBMFont* m_status = nullptr;
        CCLabelBMFont* m_summary = nullptr;
        CCLabelBMFont* m_nameLabel = nullptr;
        CCLabelBMFont* m_cardName = nullptr;
        CCLabelBMFont* m_cardSub = nullptr;
        CCMenuItemToggler* m_radios[3] = {nullptr, nullptr, nullptr};
        int m_tab = Replay;
        float m_y = 0.f;

        bool setup() {
            if (!FLAlertLayer::init(110)) return false;
            this->setTouchEnabled(true);
            this->setKeypadEnabled(true);
            m_noElasticity = true;
            g_open = true;

            m_mainLayer = CCLayer::create();
            this->addChild(m_mainLayer);
            m_buttonMenu = CCMenu::create();
            m_buttonMenu->setPosition({0.f, 0.f});
            m_mainLayer->addChild(m_buttonMenu, 10);

            auto win = CCDirector::get()->getWinSize();
            W = std::clamp(win.width - 16.f, 500.f, 640.f);
            auto origin = ccp(win.width / 2 - W / 2, win.height / 2 - H / 2);

            auto panel = CCScale9Sprite::create("square02b_001.png");
            panel->setContentSize({W, H});
            panel->setColor(kPanel);
            panel->setOpacity(245);
            panel->setPosition(ccp(win.width / 2, win.height / 2));
            m_mainLayer->addChild(panel);

            auto side = CCScale9Sprite::create("square02b_001.png");
            side->setContentSize({SIDE, H - 12.f});
            side->setColor(kSide);
            side->setOpacity(255);
            side->setPosition(origin + ccp(SIDE / 2 + 6.f, H / 2));
            m_mainLayer->addChild(side);

            // sidebar tab buttons: big icons (64 px) with their name under them, 2 columns x 3 rows
            for (int i = 0; i < TabCount; ++i) {
                float cx = 6.f + (i % 2 == 0 ? SIDE * 0.25f + 2.f : SIDE * 0.75f - 2.f);
                float cy = H - 62.f - (i / 2) * 88.f;
                auto icon = modSprite(kTabIcons[i]);
                icon->setScale(0.5f);
                auto btn = CCMenuItemSpriteExtra::create(icon, this, menu_selector(ToolkitMenu::onTab));
                btn->setTag(i);
                btn->setPosition(origin + ccp(cx, cy));
                m_buttonMenu->addChild(btn);
                m_tabBtns.push_back(btn);
                auto name = CCLabelBMFont::create(kTabNames[i], "bigFont.fnt");
                name->setScale(0.22f);
                name->setColor(kMuted);
                name->setPosition(origin + ccp(cx, cy - 40.f));
                m_mainLayer->addChild(name, 6);
            }

            // close
            auto x = CCSprite::createWithSpriteFrameName("GJ_closeBtn_001.png");
            x->setScale(0.6f);
            auto close = CCMenuItemSpriteExtra::create(x, this, menu_selector(ToolkitMenu::onClose));
            close->setPosition(origin + ccp(W - 16.f, H - 16.f));
            m_buttonMenu->addChild(close);

            // the version shows which build is really installed (check it after every GitHub build)
            std::string titleText = "GD Toolkit " + Mod::get()->getVersion().toVString();
            auto title = CCLabelBMFont::create(titleText.c_str(), "goldFont.fnt");
            title->setScale(0.6f);
            title->setPosition(ccp(win.width / 2 + SIDE / 2, origin.y + H - 16.f));
            m_mainLayer->addChild(title, 6);

            m_content = CCNode::create();
            m_content->setPosition(origin + ccp(SIDE + 26.f, H - 40.f));
            m_mainLayer->addChild(m_content, 5);

            this->schedule(schedule_selector(ToolkitMenu::tick), 0.2f);
            rebuild();
            this->show();
            return true;
        }

        void keyBackClicked() override { onClose(nullptr); }
        void onClose(CCObject*) {
            Cfg::get().save();
            g_open = false;
            this->setKeypadEnabled(false);
            this->removeFromParentAndCleanup(true);
        }
        void onTab(CCObject* s) {
            m_tab = static_cast<CCNode*>(s)->getTag();
            rebuild();
        }

        // ---------- layout helpers ----------
        CCLabelBMFont* text(std::string const& s, float x, float y, float scale, ccColor3B col, CCNode* parent, float anchorX = 0.f) {
            auto l = CCLabelBMFont::create(s.c_str(), "bigFont.fnt");
            l->setScale(scale);
            l->setColor(col);
            l->setAnchorPoint({anchorX, 0.5f});
            l->setPosition({x, y});
            parent->addChild(l);
            return l;
        }

        float contentW() const { return std::max(320.f, W - SIDE - 56.f); }

        void addToggle(char const* label, bool& ref, bool restart = false) {
            text(label, 0.f, m_y, 0.38f, kText, m_content);
            auto t = CCMenuItemToggler::createWithStandardSprites(this, menu_selector(ToolkitMenu::onToggle), 0.55f);
            t->toggle(ref);
            t->setTag(static_cast<int>(m_bools.size()));
            t->setPosition({contentW(), m_y});
            m_contentMenu->addChild(t);
            m_bools.push_back({&ref, restart});
            m_y -= 30.f;
        }

        // editable = a small text box next to the slider where the exact value can be typed
        void addSlider(char const* label, float* f, int* i, float lo, float hi, char const* fmt, bool editable = false) {
            text(label, 0.f, m_y, 0.38f, kText, m_content);
            auto sl = Slider::create(this, menu_selector(ToolkitMenu::onSlider), 0.55f);
            float cur = f ? *f : static_cast<float>(*i);
            sl->setValue((cur - lo) / (hi - lo));
            sl->setPosition({contentW() - (editable ? 100.f : 78.f), m_y});
            size_t idx = m_sliders.size();
            sl->getThumb()->setTag(static_cast<int>(idx));
            m_content->addChild(sl);
            char buf[32];
            std::snprintf(buf, sizeof buf, fmt, cur);
            CCLabelBMFont* val = nullptr;
            TextInput* input = nullptr;
            if (editable) {
                input = TextInput::create(60.f, "");
                input->setScale(0.75f);
                input->setPosition({contentW() - 12.f, m_y});
                input->setFilter("0123456789.");
                input->setMaxCharCount(7);
                input->setString(buf);
                input->setCallback([this, idx](std::string const& s) { onTyped(idx, s); });
                m_content->addChild(input, 4);
            } else {
                val = text(buf, contentW() + 10.f, m_y, 0.32f, kAccent, m_content, 1.f);
            }
            m_sliders.push_back({f, i, lo, hi, val, input, sl, fmt});
            m_y -= 30.f;
        }

        void onTyped(size_t idx, std::string const& s) {
            if (idx >= m_sliders.size()) return;
            auto& r = m_sliders[idx];
            char* end = nullptr;
            float v = std::strtof(s.c_str(), &end);
            if (end == s.c_str()) return;            // nothing parseable yet
            v = std::clamp(v, r.lo, r.hi);
            if (r.i) *r.i = static_cast<int>(std::round(v));
            else *r.f = v;
            r.slider->setValue((v - r.lo) / (r.hi - r.lo));
            Cfg::get().save();
        }

        void onToggle(CCObject* s) {
            auto t = static_cast<CCMenuItemToggler*>(s);
            auto& r = m_bools[t->getTag()];
            *r.v = !t->isToggled();
            Cfg::get().save();
            if (r.restart) note("Applies after restarting the level");
        }

        void onSlider(CCObject* s) {
            float v = static_cast<SliderThumb*>(s)->getValue();
            auto& r = m_sliders[static_cast<CCNode*>(s)->getTag()];
            float val = r.lo + v * (r.hi - r.lo);
            char buf[32];
            if (r.i) { *r.i = static_cast<int>(std::round(val)); std::snprintf(buf, sizeof buf, r.fmt, static_cast<float>(*r.i)); }
            else { *r.f = std::round(val * 100.f) / 100.f; std::snprintf(buf, sizeof buf, r.fmt, *r.f); }
            if (r.label) r.label->setString(buf);
            if (r.input) r.input->setString(buf);
            refreshButton();
        }

        CCMenuItemSpriteExtra* addButton(char const* caption, float x, float y, SEL_MenuHandler cb, char const* tex, int width = 70) {
            auto spr = ButtonSprite::create(caption, width, true, "bigFont.fnt", tex, 24.f, 0.5f);
            auto b = CCMenuItemSpriteExtra::create(spr, this, cb);
            b->setPosition({x, y});
            m_contentMenu->addChild(b);
            return b;
        }

        // ---------- tabs ----------
        void rebuild() {
            m_content->removeAllChildrenWithCleanup(true);
            m_bools.clear();
            m_sliders.clear();
            m_status = m_summary = m_nameLabel = m_cardName = m_cardSub = nullptr;
            for (auto& r : m_radios) r = nullptr;
            m_contentMenu = CCMenu::create();
            m_contentMenu->setPosition({0.f, 0.f});
            m_content->addChild(m_contentMenu, 2);

            for (int i = 0; i < TabCount; ++i) {
                auto spr = static_cast<CCSprite*>(m_tabBtns[i]->getNormalImage());
                spr->setColor(i == m_tab ? kAccent : kMuted);
            }
            auto tabTitle = CCLabelBMFont::create(kTabNames[m_tab], "goldFont.fnt");
            tabTitle->setScale(0.55f);
            tabTitle->setAnchorPoint({0.f, 0.5f});
            m_content->addChild(tabTitle);
            m_y = -30.f;

            auto& c = Cfg::get();
            switch (m_tab) {
                case Cheats:
                    addToggle("Noclip", c.noclip);
                    addToggle("Safe mode (no submit)", c.safeMode);
                    addToggle("Speedhack", c.speedhack);
                    addSlider("Speed", &c.speed, nullptr, 0.01f, 10.f, "%.2f", true);
                    addToggle("Physical TPS (240+)", c.tpsBypass);
                    addSlider("TPS", &c.tps, nullptr, 240.f, 1000.f, "%.0f", true);
                    addToggle("Show TPS", c.showTps);
                    break;
                case Hitbox:
                    addToggle("Show hitboxes", c.hitboxes);
                    addToggle("Player hitbox", c.playerHitbox);
                    addToggle("Hitbox trail", c.hitboxTrail);
                    addToggle("Hitborders (NaN style)", c.hitborders);
                    addSlider("Trail length", nullptr, &c.trailLen, 5.f, 200.f, "%.0f");
                    addSlider("Fill opacity", &c.hitFill, nullptr, 0.f, 1.f, "%.2f");
                    addSlider("Border width", &c.hitBorder, nullptr, 0.5f, 4.f, "%.1f");
                    break;
                case Path:
                    addToggle("Pathfinder (experimental)", c.pathfinder);
                    addSlider("Length", nullptr, &c.pathLen, 10, 300, "%.0f");
                    text("green = hold   red = release", 0.f, m_y - 2.f, 0.3f, kMuted, m_content);
                    break;
                case Display:
                    addToggle("Layout mode (re-open level)", c.layoutMode, true);
                    addToggle("Practice music sync", c.practiceSync);
                    addToggle("Counter overlay", c.counterOverlay);
                    addToggle("Window cue on screen", c.counterHud);
                    addToggle("Window cue sound", c.counterSfx);
                    addSlider("Menu button size", &c.btnScale, nullptr, 0.3f, 1.2f, "%.2f");
                    text("drag the round button to move it", 0.f, m_y - 2.f, 0.3f, kMuted, m_content);
                    break;
                case Replay: buildReplay(); break;
                case Counter: buildCounter(); break;
                default: break;
            }
            tick(0.f);
        }

        // Replay tab: same layout as a bot menu - mode, replay picker, ignore inputs, file buttons.
        void buildReplay() {
            float cw = contentW();
            char const* names[3] = {"Disabled", "Record", "Playback"};
            float xs[3] = {2.f, 108.f, 214.f};
            for (int i = 0; i < 3; ++i) {
                auto t = CCMenuItemToggler::createWithStandardSprites(this, menu_selector(ToolkitMenu::onMode), 0.55f);
                t->setTag(i);
                t->setPosition({xs[i] + 12.f, m_y});
                m_contentMenu->addChild(t);
                m_radios[i] = t;
                text(names[i], xs[i] + 30.f, m_y, 0.36f, kText, m_content);
            }
            m_y -= 30.f;

            text("Replays", 0.f, m_y, 0.38f, kText, m_content);
            auto left = CCSprite::createWithSpriteFrameName("GJ_arrow_01_001.png");
            left->setScale(0.5f);
            auto lb = CCMenuItemSpriteExtra::create(left, this, menu_selector(ToolkitMenu::onPrev));
            lb->setPosition({cw - 178.f, m_y});
            m_contentMenu->addChild(lb);
            auto right = CCSprite::createWithSpriteFrameName("GJ_arrow_01_001.png");
            right->setScale(0.5f);
            right->setFlipX(true);
            auto rb = CCMenuItemSpriteExtra::create(right, this, menu_selector(ToolkitMenu::onNext));
            rb->setPosition({cw - 12.f, m_y});
            m_contentMenu->addChild(rb);
            m_nameLabel = text("", cw - 95.f, m_y, 0.32f, kAccent, m_content, 0.5f);
            m_y -= 30.f;

            addToggle("Ignore Inputs", Cfg::get().ignoreInputs);

            float bx = cw / 2.f;
            char const* tex = "GJ_button_04.png";
            addButton("New", bx, m_y, menu_selector(ToolkitMenu::onNew), tex, 230);
            m_y -= 28.f;
            addButton("Save", bx, m_y, menu_selector(ToolkitMenu::onSave), tex, 230);
            m_y -= 28.f;
            addButton("Load", bx, m_y, menu_selector(ToolkitMenu::onLoad), tex, 230);
            m_y -= 28.f;
            addButton("Delete", bx, m_y, menu_selector(ToolkitMenu::onDelete), tex, 230);
            m_y -= 28.f;
            addButton("Open Replays Folder", bx, m_y, menu_selector(ToolkitMenu::onFolder), tex, 230);
        }

        // Window counter tab, laid out like Antic's Counter: replay card, Count / Stop, Save On Finish, Copy Info.
        void buildCounter() {
            float cw = contentW();
            auto card = CCScale9Sprite::create("square02b_001.png");
            card->setContentSize({cw + 16.f, 40.f});
            card->setColor({34, 39, 52});
            card->setOpacity(255);
            card->setPosition({cw / 2.f, m_y - 10.f});
            m_content->addChild(card, 1);
            m_cardName = text("", 0.f, m_y - 2.f, 0.4f, {255, 255, 255}, m_content);
            m_cardSub = text("", 0.f, m_y - 19.f, 0.26f, kMuted, m_content);
            m_cardName->setZOrder(3);
            m_cardSub->setZOrder(3);
            addButton("Change", cw - 26.f, m_y - 10.f, menu_selector(ToolkitMenu::onChange), "GJ_button_02.png", 56);
            m_y -= 50.f;

            addButton("Count Macro", cw * 0.25f, m_y, menu_selector(ToolkitMenu::onCount), "GJ_button_01.png", 150);
            addButton("Stop", cw * 0.75f, m_y, menu_selector(ToolkitMenu::onCancel), "GJ_button_06.png", 150);
            m_y -= 32.f;
            addToggle("Save On Finish", Cfg::get().saveOnFinish);
            addToggle("Show Results List", Cfg::get().counterList);
            m_y += 2.f;
            addButton("Copy Info", cw / 2.f, m_y, menu_selector(ToolkitMenu::onCopy), "GJ_button_04.png", 316);
            m_y -= 26.f;

            m_status = text("", 0.f, m_y, 0.3f, kAccent, m_content);
            m_summary = text("", 0.f, m_y - 12.f, 0.26f, kText, m_content);
            m_summary->setAnchorPoint({0.f, 1.f});

            // other mods that hook the same game functions (bots, noclip, speedhack, practice fix): say which
            // ones are installed and what to turn off, since their features change the runs being counted
            auto loaded = [](char const* id) { return Loader::get()->isModLoaded(id); };
            std::string others;
            if (loaded("peony.silicate")) others += "Silicate ";
            if (loaded("thesillydoggo.qolmod")) others += "QOLMod ";
            if (loaded("anticroom.antics-counter")) others += "Antic's-Counter ";
            if (!others.empty()) {
                text("Also installed: " + others, 0.f, -H + 46.f, 0.22f, kAccent, m_content);
                text("While counting / recording, turn THEIR noclip, speedhack,", 0.f, -H + 37.f, 0.22f, kMuted, m_content);
                text("practice fix and bot (record/playback) off.", 0.f, -H + 28.f, 0.22f, kMuted, m_content);
            }
        }

        void tick(float) {
            if (m_status) m_status->setString(counter::statusText().c_str());
            if (m_summary) m_summary->setString(counter::summaryText().c_str());
            if (m_cardName) {
                auto n = counter::currentName();
                if (n.size() > 22) n = n.substr(0, 20) + "..";
                m_cardName->setString(n.c_str());
            }
            if (m_cardSub) m_cardSub->setString(counter::macroInfo().c_str());
            if (m_nameLabel) {
                auto n = counter::currentName();
                if (n.size() > 18) n = n.substr(0, 16) + "..";
                m_nameLabel->setString(n.c_str());
            }
            for (int i = 0; i < 3; ++i) {
                if (m_radios[i]) m_radios[i]->toggle(static_cast<int>(counter::mode()) == i);
            }
        }

        // ---------- actions ----------
        void onMode(CCObject* s) {
            counter::setMode(static_cast<counter::Mode>(static_cast<CCNode*>(s)->getTag()));
            tick(0.f);
        }
        void onPrev(CCObject*) { counter::selectReplay(-1); tick(0.f); }
        void onNext(CCObject*) { counter::selectReplay(1); tick(0.f); }
        void onNew(CCObject*) {
            NamePrompt::create([](std::string const& n) { note(counter::newReplay(n)); });
        }
        void onSave(CCObject*) { note(counter::saveReplay()); }
        void onLoad(CCObject*) { note(counter::loadReplay()); }
        void onDelete(CCObject*) {
            createQuickPopup("Delete replay", "Delete \"" + counter::currentName() + "\"?", "No", "Yes",
                             [](FLAlertLayer*, bool yes) {
                                 if (yes) note(counter::deleteReplay());
                             });
        }
        void onFolder(CCObject*) { counter::openReplaysFolder(); }
        void onCount(CCObject*) {
            if (!PlayLayer::get()) { note("Open this inside a level"); return; }
            counter::startCounting();
            note("Close the menu and resume - counting runs automatically");
        }
        void onCancel(CCObject*) {
            if (counter::isCounting()) counter::cancelCounting();
            else counter::setMode(counter::Mode::Disabled);      // also stops playback
            tick(0.f);
        }
        void onChange(CCObject*) {
            counter::selectReplay(1);
            note(counter::loadReplay());
            tick(0.f);
        }
        void onCopy(CCObject*) {
            auto info = counter::infoText();
            utils::clipboard::write(info.c_str());
            note("Copied to clipboard");
        }
    };
}

void open() {
    if (g_open) return;
    ToolkitMenu::create();
}

bool isOpen() { return g_open; }

} // namespace kt::ui
```

## src/FloatButton.cpp

```cpp
// The menu button: big, draggable, and present in every scene (it lives in Geode's OverlayManager).
// Tap = open the menu. Press and drag = move it; the position is remembered.
#include "Common.hpp"
#include <Geode/ui/OverlayManager.hpp>
#include <algorithm>
#include <cmath>

using namespace geode::prelude;

namespace kt::ui {

CCSprite* modSprite(char const* name) {
    std::string path(Mod::get()->expandSpriteName(name));
    return CCSprite::create(path.c_str());
}

namespace {
    constexpr char const* kId = "kbjgd.gd-toolkit/float-button";

    class FloatButton : public CCNode, public CCTouchDelegate {
    public:
        static FloatButton* create() {
            auto r = new FloatButton();
            if (r && r->setup()) { r->autorelease(); return r; }
            CC_SAFE_DELETE(r);
            return nullptr;
        }

        void applyScale() { m_icon->setScale(Cfg::get().btnScale); }
        void applyPos() {
            auto win = CCDirector::get()->getWinSize();
            this->setPosition({win.width * Cfg::get().btnX, win.height * Cfg::get().btnY});
        }

        void onEnter() override {
            CCNode::onEnter();
            CCTouchDispatcher::get()->addTargetedDelegate(this, -400, true);
        }
        void onExit() override {
            CCTouchDispatcher::get()->removeDelegate(this);
            CCNode::onExit();
        }

        bool ccTouchBegan(CCTouch* t, CCEvent*) override {
            if (!this->isVisible()) return false;
            auto p = this->convertTouchToNodeSpace(t);
            float radius = m_icon->getContentSize().width * m_icon->getScale() / 2.f;
            if (std::sqrt(p.x * p.x + p.y * p.y) > radius) return false;
            m_down = true;
            m_drag = false;
            m_start = t->getLocation();
            return true;
        }

        void ccTouchMoved(CCTouch* t, CCEvent*) override {
            if (!m_down) return;
            auto loc = t->getLocation();
            float dx = loc.x - m_start.x, dy = loc.y - m_start.y;
            if (!m_drag && std::sqrt(dx * dx + dy * dy) > 10.f) m_drag = true;
            if (!m_drag) return;
            auto win = CCDirector::get()->getWinSize();
            auto np = this->getParent()->convertToNodeSpace(loc);
            np.x = std::clamp(np.x, 10.f, win.width - 10.f);
            np.y = std::clamp(np.y, 10.f, win.height - 10.f);
            this->setPosition(np);
        }

        void ccTouchEnded(CCTouch*, CCEvent*) override {
            if (!m_down) return;
            m_down = false;
            if (m_drag) {
                auto win = CCDirector::get()->getWinSize();
                auto& c = Cfg::get();
                c.btnX = this->getPositionX() / win.width;
                c.btnY = this->getPositionY() / win.height;
                c.save();
            } else if (!isOpen()) {
                open();
            }
        }

        void ccTouchCancelled(CCTouch*, CCEvent*) override { m_down = false; }

        // Hidden while a level is being played (normal or practice) and while the menu is open.
        // In a level it shows up again when you pause and on the level-complete screen.
        void tick(float) {
            bool hide = isOpen();
            if (auto pl = PlayLayer::get()) {
                bool finished = pl->m_hasCompletedLevel || pl->m_levelEndAnimationStarted;
                bool playing = !pl->m_isPaused && !finished;
                hide = hide || playing || counter::isCounting();
            }
            if (hide && m_down) m_down = false;
            this->setVisible(!hide);
        }

    private:
        CCSprite* m_icon = nullptr;
        bool m_down = false, m_drag = false;
        CCPoint m_start;

        bool setup() {
            if (!CCNode::init()) return false;
            m_icon = modSprite("kt_menu.png");
            m_icon->setOpacity(225);
            this->addChild(m_icon);
            applyScale();
            applyPos();
            this->setID(kId);
            this->schedule(schedule_selector(FloatButton::tick), 0.1f);
            return true;
        }
    };

    FloatButton* find() {
        return static_cast<FloatButton*>(OverlayManager::get()->getChildByID(kId));
    }
}

void spawnButton() {
    if (find()) return;
    if (auto b = FloatButton::create()) OverlayManager::get()->addChild(b, 1000);
}

void refreshButton() {
    if (auto b = find()) b->applyScale();
}

} // namespace kt::ui
```

