// noclip, speedhack + physical TPS, practice music sync, layout mode.
#include "Common.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>

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
    float g_lastScale = -1.f;
    int g_stepCount = 0;
    float g_tps = 0.f;
    std::chrono::steady_clock::time_point g_t0 = std::chrono::steady_clock::now();
}

void apply() {
    auto& c = Cfg::get();
    float s = c.speedhack ? std::clamp(c.speed, 0.05f, 10.f) : 1.f;
    if (s == g_lastScale) return;
    g_lastScale = s;
    CCDirector::get()->getScheduler()->setTimeScale(s);
    if (auto fmod = FMODAudioEngine::sharedEngine()) {
        if (fmod->m_backgroundMusicChannel) fmod->m_backgroundMusicChannel->setPitch(s);
    }
}

// Physical TPS. Returns a time slice made of whole ticks of length 1/tps and keeps the remainder in the
// game's own m_extraDelta. NOTE: verify in game with "Show TPS" - GD may still split the slice at 240 TPS.
double modifiedDelta(GJBaseGameLayer* layer, float dt) {
    double tick = 1.0 / std::clamp(static_cast<double>(Cfg::get().tps), 30.0, 2000.0);
    double warp = std::clamp(static_cast<double>(layer->m_gameState.m_timeWarp), 0.01, 1.0);
    tick *= warp;
    double total = static_cast<double>(dt) + layer->m_extraDelta;
    double steps = std::floor(total / tick);
    double used = steps * tick;
    layer->m_extraDelta = total - used;
    return used;
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
    double t = pl->m_gameState.m_levelTime;
    double offset = pl->m_levelSettings ? pl->m_levelSettings->m_songOffset : 0.0;
    unsigned ms = static_cast<unsigned>(std::max(0.0, (t + offset) * 1000.0));
    FMODAudioEngine::sharedEngine()->setMusicTimeMS(ms, true, 0);
}
}

namespace kt::layout {
bool isDeco(GameObject* o) {
    return o->m_objectType == GameObjectType::Decoration && !o->m_isTrigger;
}
void apply(PlayLayer* pl) {
    if (!Cfg::get().layoutMode) return;
    for (auto obj : CCArrayExt<GameObject*>(pl->m_objects)) {
        if (obj && isDeco(obj)) obj->setVisible(false);   // hooked setVisible keeps it hidden afterwards
    }
    if (pl->m_background) pl->m_background->setColor({40, 125, 255});
}
}