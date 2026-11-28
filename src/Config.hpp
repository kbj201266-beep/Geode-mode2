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
