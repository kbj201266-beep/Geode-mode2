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

// Layout mode: background and ground stay, decoration hidden, every object with a hitbox painted white, no glow.
// The object loop only runs when the setting actually changed (or a new level opened), never on every death.
// Turning it off restores the original state.
namespace kt::layout {
namespace {
    constexpr int kWhite = 1011;
    struct Orig {
        bool hide;
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

void apply(PlayLayer* pl) {
    if (g_owner != pl) {
        g_owner = pl;
        g_orig.clear();
        g_applied = false;
    }
    bool any = Cfg::get().layoutMode;
    if (any == g_applied) return;          // nothing changed: no work at all
    if (!pl->m_objects) return;
    bool hideDeco = any, white = any, noGlow = any;

    for (auto obj : CCArrayExt<GameObject*>(pl->m_objects)) {
        if (!obj) continue;
        bool deco = isDeco(obj), solid = hasHitbox(obj);
        if (!deco && !solid) continue;
        if (!g_orig.count(obj)) {
            if (!any) continue;
            g_orig[obj] = {obj->m_isHide, obj->m_baseColor ? obj->m_baseColor->m_colorID : 0,
                           obj->m_detailColor ? obj->m_detailColor->m_colorID : 0,
                           obj->m_glowSprite ? obj->m_glowSprite->isVisible() : false};
        }
        auto& o = g_orig[obj];
        // back to the original first, then apply whatever is on now
        obj->m_isHide = o.hide;
        if (obj->m_baseColor) obj->m_baseColor->m_colorID = o.base;
        if (obj->m_detailColor) obj->m_detailColor->m_colorID = o.detail;
        if (obj->m_glowSprite) obj->m_glowSprite->setVisible(o.glow);

        if (hideDeco && deco) {
            obj->m_isHide = true;
            obj->setVisible(false);
        }
        if (white && solid) {
            if (obj->m_baseColor) obj->m_baseColor->m_colorID = kWhite;
            if (obj->m_detailColor) obj->m_detailColor->m_colorID = kWhite;
        }
        if (noGlow && obj->m_glowSprite) obj->m_glowSprite->setVisible(false);
    }
    if (!any) g_orig.clear();
    g_applied = any;
}
}
