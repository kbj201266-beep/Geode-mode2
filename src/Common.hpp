#pragma once
#include <Geode/Geode.hpp>
#include <string>
#include <vector>
#include "Config.hpp"

// Public surface of every feature module. Hooks live in Hooks.cpp and only call these.
namespace kt {

namespace hitbox {
    void update(PlayLayer*);   // draws hitboxes / borders / trail each frame
    void reset();              // clears trail history
}

namespace path {
    void update(PlayLayer*);   // pathfinder: simulates a dummy player forward
    bool simulating();         // true while the dummy is being stepped
    void onSimDeath();
    void reset();
}

namespace noclip {
    void onHit();              // called instead of dying
    void onStep();
    float accuracy();          // 0..100
    void reset();
}

namespace speed {
    void apply();              // speedhack time scale + music pitch
    double modifiedDelta(GJBaseGameLayer*, float dt);  // physical TPS
    void onStep();             // counts physics steps for the TPS readout
    float measuredTps();
}

namespace practice {
    void sync(PlayLayer*);     // re-seek music after a checkpoint respawn
}

namespace layout {
    bool isDeco(GameObject*);
    void apply(PlayLayer*);
}

namespace counter {
    struct Input {
        int frame;
        bool down;
        bool p1;
    };
    struct Result {
        int frame = 0;
        int early = 0;
        int late = 0;
        int window() const { return early + late + 1; }
    };

    void onLevelEnter(PlayLayer*);
    bool filterInput(bool down, int button, bool p1);              // false = swallow the real input
    void onInput(GJBaseGameLayer*, bool down, int button, bool p1);
    void beforeStep(GJBaseGameLayer*);                             // inject macro / show cues
    bool onDeath(PlayLayer*);                                      // true = swallow the death
    bool onComplete(PlayLayer*);                                   // true = swallow level complete
    void onReset(PlayLayer*);
    void update(PlayLayer*);                                       // state machine, once per frame

    void armRecording();
    void stopRecording();
    void startCounting();
    void cancelCounting();
    void clearAll();
    bool isRecording();
    bool isCounting();
    std::string statusText();
    std::string summaryText();
}

namespace hud {
    void attach(PlayLayer*);
    void update(PlayLayer*);
    void flash(std::string const& text);
}

namespace ui {
    void open();
    // Adds the toolkit button (own icon) to a menu.
    CCMenuItemSpriteExtra* makeButton(cocos2d::CCObject* target, cocos2d::SEL_MenuHandler cb, float scale);
}

} // namespace kt