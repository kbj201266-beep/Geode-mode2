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
