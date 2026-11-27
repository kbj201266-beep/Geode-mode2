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
}

// Speedhack = every frame's delta time is multiplied (CCScheduler::update hook in Hooks.cpp). Reapplied every
// frame, so it keeps working after GD resets things, in menus too, and while the counting speed is on.
float factor() { return userSpeed() * counter::speedFactor(); }

void updatePitch() {
    float p = userSpeed();
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

// Layout mode: background and ground stay as they are, decoration is hidden, and every object with a hitbox
// is painted white (GD's built-in "White" color channel 1011). Done once per reset instead of hooking every
// setVisible call, which is what made it lag before. Turning it off restores the original state.
namespace kt::layout {
namespace {
    constexpr int kWhite = 1011;
    struct Orig {
        bool hide;
        int base;
        int detail;
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
    bool on = Cfg::get().layoutMode;
    if (!on && !g_applied) return;
    if (!pl->m_objects) return;

    for (auto obj : CCArrayExt<GameObject*>(pl->m_objects)) {
        if (!obj) continue;
        bool deco = isDeco(obj), solid = hasHitbox(obj);
        if (!deco && !solid) continue;
        if (on) {
            if (!g_orig.count(obj)) {
                g_orig[obj] = {obj->m_isHide, obj->m_baseColor ? obj->m_baseColor->m_colorID : 0,
                               obj->m_detailColor ? obj->m_detailColor->m_colorID : 0};
            }
            if (deco) {
                obj->m_isHide = true;
                obj->setVisible(false);
            } else {
                if (obj->m_baseColor) obj->m_baseColor->m_colorID = kWhite;
                if (obj->m_detailColor) obj->m_detailColor->m_colorID = kWhite;
            }
        } else {
            auto it = g_orig.find(obj);
            if (it == g_orig.end()) continue;
            obj->m_isHide = it->second.hide;
            if (obj->m_baseColor) obj->m_baseColor->m_colorID = it->second.base;
            if (obj->m_detailColor) obj->m_detailColor->m_colorID = it->second.detail;
        }
    }
    if (!on) g_orig.clear();
    g_applied = on;
}
}
