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
        bool counted = false;
        int window() const { return early + late + 1; }
    };
    struct ClickView {
        cocos2d::CCPoint pos;    // where the click happened in the level
        int window;
    };

    enum class Mode { Disabled = 0, Record = 1, Playback = 2 };

    void onLevelEnter(PlayLayer*);
    bool filterInput(bool down, int button, bool p1);              // false = swallow the real input
    void onInput(GJBaseGameLayer*, bool down, int button, bool p1);
    void beforeStep(GJBaseGameLayer*);                             // inject macro / show cues
    bool onDeath(PlayLayer*);                                      // true = swallow the death
    bool onComplete(PlayLayer*);                                   // true = swallow level complete
    void onReset(PlayLayer*);                                      // after a reset AND after a checkpoint load
    void update(PlayLayer*);                                       // state machine, once per frame

    Mode mode();
    void setMode(Mode);
    void startCounting();
    void cancelCounting();
    bool isRecording();
    bool isCounting();
    std::string statusText();
    std::string summaryText();

    // data for the on-level overlay
    std::vector<Result> const& results();
    std::vector<ClickView> clickViews();
    std::vector<cocos2d::CCPoint> const& pathPoints();
    int version();               // changes whenever the overlay must be redrawn

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
    void update(PlayLayer*);   // window counter overlay: path, click rings, window numbers, stats
}

namespace hud {
    void attach(PlayLayer*);
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
