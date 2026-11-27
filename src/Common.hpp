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
    void onSimDeath(GameObject* obj);
    void reset();
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
    void apply(PlayLayer*);    // layout mode / ultra low detail mode (or undo them)
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
    float speedFactor();         // 8 while counting, otherwise 1
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
    void update(PlayLayer*);   // window counter overlay: path, donut rings, window numbers, stats
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
