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
//              2) for every click: load the checkpoint before it and
//                   - shift the click (with its release) earlier / later by 1,2,3... frames  -> click window
//                   - shift only the release earlier / later                                 -> release window
//                 A shift "works" if the player survives until the NEXT click. window = early + late + 1.
//                 No level restarts, so the attempt counter stays at 1 and it is fast.
//              3) when done, the level restarts from the beginning.
//  - Play:     while you (or the bot) play, every click / release shows its window one by one as you reach it.
// Replays are text files in <save dir>/replays/<name>.ktr:
//   1 inputs "frame:down:p1;"            2 click results "frame:early:late:counted;"
//   3 click positions "x,y;"             4 path "x,y;"
//   5 release results "early:late:counted;"   6 release positions "x,y;"
#include "Common.hpp"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <sstream>

using namespace geode::prelude;

namespace kt::counter {

namespace {
    constexpr int kMaxSide = 12;         // search at most 12 frames each way -> window up to 25f
    constexpr float kCountSpeed = 8.f;   // replay speed while counting
    constexpr float kNoPos = -1e9f;
    constexpr size_t kMaxLines = 16;     // trial lines kept on screen for the current click

    enum class Phase { Idle, Baseline, Trial };
    // sides of one click: 0 click earlier, 1 click later, 2 release earlier, 3 release later
    enum Side { DownEarly = 0, DownLate = 1, RelEarly = 2, RelLate = 3 };

    struct Click {
        int down = 0;        // index into macro
        int rel = -1;        // index of the matching release (-1 = held to the end)
        int checkFrame = 0;  // frame of the next click: surviving until here = success
    };

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
        std::vector<int> cpFrame;
        size_t nextCp = 0;
        std::vector<CCPoint> trialPath;      // path of the running trial
        std::vector<TrialLine> lines;        // finished trials of the current click

        std::vector<std::string> names;      // replay files on disk
        int sel = 0;

        bool armed = false, recording = false;
        Phase phase = Phase::Idle;
        bool needReset = false, trialDead = false, completed = false, injecting = false;
        bool restartAfter = false;           // put the player back at the level start on the next frame
        int stepFrame = 0;                   // frame number of the physics step being processed right now

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
    int frameOf(GJBaseGameLayer* l) { return l->m_gameState.m_currentProgress; }
    bool hasPos(CCPoint const& p) { return p.x > kNoPos / 2; }

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
        s.nextCp = 0;
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
            c.checkFrame = last + 90;
            for (int j = i + 1; j < static_cast<int>(s.macro.size()); ++j) {
                if (c.rel < 0 && !s.macro[j].down && s.macro[j].p1 == in.p1) c.rel = j;
                if (s.macro[j].down) { c.checkFrame = s.macro[j].frame; break; }
            }
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

    // ---------- trial construction ----------
    // click = -1 builds the untouched macro (baseline). Returns false if the shift is impossible
    // (it would reorder inputs, or the release would come before its press / after the next click).
    bool makeTrial(int click, int off, int side) {
        s.trial = s.macro;
        if (click < 0) {
            s.checkFrame = (s.macro.empty() ? 0 : s.macro.back().frame) + 90;
            return true;
        }
        auto& c = s.clicks[click];
        bool releaseOnly = side >= RelEarly;
        if (releaseOnly) {
            if (c.rel < 0) return false;
            s.trial[c.rel].frame += off;
            if (s.trial[c.rel].frame <= s.trial[c.down].frame) return false;
            if (s.trial[c.rel].frame >= c.checkFrame) return false;
        } else {
            s.trial[c.down].frame += off;
            if (c.rel >= 0) s.trial[c.rel].frame += off;
            if (s.trial[c.down].frame >= c.checkFrame) return false;
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
        s.dir = DownEarly;
        s.off = -1;
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
                if (s.clicks[s.k].rel >= 0) { s.dir = RelEarly; s.off = -1; }
                else advanceClick();
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
        stop("Done - " + std::to_string(s.limit) + " clicks counted");
        FMODAudioEngine::sharedEngine()->playEffect(std::string("ding_9.wav"_spr), 1.f, 1.f, 1.f);
    }

    void scheduleNext() {
        while (s.k < s.limit) {
            if (makeTrial(s.k, s.off, s.dir)) { s.needReset = true; return; }
            nextSide();     // impossible shift counts as a fail
        }
        complete();
    }

    void finish(bool ok) {
        if (s.phase == Phase::Baseline) {
            if (!ok) {
                // The replay dies somewhere: still count every click whose next click comes before that point.
                int usable = 0;
                while (usable < static_cast<int>(s.clicks.size()) && s.clicks[usable].checkFrame < s.deathFrame) ++usable;
                if (usable == 0) { stop("The replay dies before its first click - record a longer run"); return; }
                s.limit = usable;
            }
            s.phase = Phase::Trial;
            s.k = 0; s.dir = DownEarly; s.off = -1;
        } else {
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

void onLevelEnter(PlayLayer*) {
    if (active()) stop("Stopped");
    s.restartAfter = false;      // a fresh level is already at the start
    s.armed = s.recording = false;
    if (s.mode == Mode::Record) {  // Record chosen before entering: start recording from this level's start
        s.armed = true;
        s.restartAfter = true;
    }
    releaseCps();
    s.lines.clear();
    s.trialPath.clear();
    refreshList();
    s.cursor = 0;
    s.cueCursor = 0;
    s.lastCue = -1;
    ++s.version;
}

bool filterInput(bool, int, bool) {
    if (active()) return s.injecting;
    if (playing() && Cfg::get().ignoreInputs) return s.injecting;
    return true;
}

// Recording and playback use the SAME frame number: the one taken at the start of the physics step
// (beforeStep). Playback presses at that same point of the same step, so the bot repeats the run exactly.
void onInput(GJBaseGameLayer*, bool down, int button, bool p1) {
    if (s.recording && !s.injecting && button == 1) s.macro.push_back({s.stepFrame, down, p1});
}

void beforeStep(GJBaseGameLayer* l) {
    int f = frameOf(l);
    s.stepFrame = f;
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
                if (auto cp = pl->createCheckpoint()) { cp->retain(); s.cps[s.nextCp] = cp; }
            }
            ++s.nextCp;
        }
    } else if (s.phase == Phase::Trial && f % 2 == 0 && l->m_player1) {
        s.trialPath.push_back(l->m_player1->getPosition());
    }

    if (active() || playing()) {
        s.injecting = true;
        while (s.cursor < s.trial.size() && s.trial[s.cursor].frame <= f) {
            size_t index = s.cursor++;
            auto& in = s.trial[index];
            if (baseline) {
                auto p = in.p1 ? l->m_player1 : l->m_player2;
                if (p && in.down && index < s.downToClick.size() && s.downToClick[index] >= 0)
                    s.clickPos[s.downToClick[index]] = p->getPosition();
                if (p && !in.down && index < s.relToClick.size() && s.relToClick[index] >= 0)
                    s.relPos[s.relToClick[index]] = p->getPosition();
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

bool onDeath(PlayLayer* pl) {
    if (!active()) return false;
    if (!s.trialDead) s.deathFrame = frameOf(pl);
    s.trialDead = true;
    return true;
}

bool onComplete(PlayLayer*) {
    if (s.recording) {
        s.recording = false;
        s.armed = false;
        buildClicks();
        buildCues();
        s.path.clear();
        autosave();
        ++s.version;
        s.msg = "Recorded " + std::to_string(s.clicks.size()) + " clicks" + (hasSelection() ? " (saved)" : "");
        return false;       // let the real completion happen
    }
    if (active()) { s.completed = true; return true; }
    return false;
}

// Called after every level reset AND every checkpoint load.
void onReset(PlayLayer* pl) {
    int f = frameOf(pl);
    if (s.armed && f <= 1) {
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
        pl->resetLevelFromStart();
        return;
    }
    if (!active()) return;
    if (s.needReset) {
        s.needReset = false;
        bool useCp = s.phase == Phase::Trial && s.k < static_cast<int>(s.cps.size()) && s.cps[s.k];
        if (useCp) pl->loadFromCheckpoint(s.cps[s.k]);
        else pl->resetLevelFromStart();
        return;
    }
    int f = frameOf(pl);
    if (s.completed) finish(true);
    else if (s.trialDead) finish(false);
    else if (f >= s.checkFrame && s.checkFrame > 0) finish(true);
}

// ================= UI-facing API =================

Mode mode() { return s.mode; }

void setMode(Mode m) {
    if (active()) return;
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
    s.cps.assign(s.clicks.size(), nullptr);
    s.cpFrame.resize(s.clicks.size());
    for (size_t i = 0; i < s.clicks.size(); ++i) {
        s.cpFrame[i] = std::max(0, s.macro[s.clicks[i].down].frame - kMaxSide - 2);
    }
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

float speedFactor() { return active() ? kCountSpeed : 1.f; }

void cancelCounting() { if (active()) stop("Stopped"); }

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
    return "Counting.. " + std::to_string(s.k) + "/" + std::to_string(n) + "\nETA " + e;
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
    o << "Replay: " << currentName() << "\n" << summaryText() << "\nAll clicks:\n";
    for (size_t i = 0; i < s.results.size(); ++i) {
        auto& r = s.results[i];
        if (!r.counted) continue;
        o << "#" << (i + 1) << "  frame " << r.frame << "  click " << r.window() << "f (-" << r.early << " / +" << r.late << ")";
        if (r.relCounted) o << "  release " << r.relWindow() << "f (-" << r.relEarly << " / +" << r.relLate << ")";
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
