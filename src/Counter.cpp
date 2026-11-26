// Window frame counter.
//
// 1. Record: arm recording, restart the level, play it through. The last complete attempt is kept as the macro
//    (stored per level in the mod's saved values).
// 2. Count: the macro is replayed from the level start. For every click the click is shifted earlier / later by
//    1,2,3... frames. A shift "works" if the player survives until the NEXT click of the macro. The number of
//    working shifts on each side is the window: window = early + late + 1 (the original frame itself).
// 3. Play: the window of each click is shown (and a ding plays, pitch = window size) when you reach that frame.
#include "Common.hpp"
#include <chrono>
#include <cstdio>
#include <map>
#include <sstream>

using namespace geode::prelude;

namespace kt::counter {

namespace {
    constexpr int kMaxSide = 6;          // search at most 6 frames each way -> window up to 13f
    constexpr float kCountSpeed = 4.f;   // replay speed while counting

    enum class Phase { Idle, Baseline, Trial };

    struct Click {
        int down = 0;        // index into macro
        int rel = -1;        // index of the matching release (-1 = held to the end)
        int checkFrame = 0;  // frame of the next click: surviving until here = success
    };

    struct S {
        std::vector<Input> macro;
        std::vector<Click> clicks;
        std::vector<Result> results;
        std::map<int, int> cues;             // click frame -> window, for play-time display
        int levelId = 0;

        bool armed = false, recording = false;
        Phase phase = Phase::Idle;
        bool needReset = false, trialDead = false, completed = false, injecting = false;

        std::vector<Input> trial;
        size_t cursor = 0;
        int checkFrame = 0;
        int k = 0, dir = 0, off = 0;
        std::chrono::steady_clock::time_point t0;
        std::string msg = "Idle";
    } s;

    bool active() { return s.phase != Phase::Idle; }
    std::string key(char const* p) { return std::string(p) + std::to_string(s.levelId); }

    int frameOf(GJBaseGameLayer* l) { return l->m_gameState.m_currentProgress; }

    // ---------- persistence ----------
    void save() {
        std::ostringstream m, r;
        for (auto& i : s.macro) m << i.frame << ':' << i.down << ':' << i.p1 << ';';
        for (auto& x : s.results) r << x.frame << ':' << x.early << ':' << x.late << ';';
        Mod::get()->setSavedValue<std::string>(key("macro_"), m.str());
        Mod::get()->setSavedValue<std::string>(key("res_"), r.str());
    }

    template <class F>
    void parse(std::string const& str, F&& f) {
        size_t pos = 0;
        while (pos < str.size()) {
            auto end = str.find(';', pos);
            if (end == std::string::npos) break;
            int a, b, c;
            if (std::sscanf(str.substr(pos, end - pos).c_str(), "%d:%d:%d", &a, &b, &c) == 3) f(a, b, c);
            pos = end + 1;
        }
    }

    void buildCues() {
        s.cues.clear();
        for (size_t i = 0; i < s.results.size() && i < s.clicks.size(); ++i) {
            s.cues[s.results[i].frame] = s.results[i].window();
        }
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
        for (size_t i = 0; i < s.clicks.size(); ++i) s.results[i].frame = s.macro[s.clicks[i].down].frame;
    }

    float desiredScale() {
        auto& c = Cfg::get();
        return c.speedhack ? c.speed : 1.f;
    }

    // ---------- trial construction ----------
    // click = -1 builds the untouched macro (baseline). Returns false if the shift breaks the input order.
    bool makeTrial(int click, int off) {
        s.trial = s.macro;
        if (click < 0) {
            s.checkFrame = (s.macro.empty() ? 0 : s.macro.back().frame) + 90;
            return true;
        }
        auto& c = s.clicks[click];
        s.trial[c.down].frame += off;
        if (c.rel >= 0) s.trial[c.rel].frame += off;
        for (size_t i = 0; i < s.trial.size(); ++i) {
            if (s.trial[i].frame < 1) return false;
            if (i > 0 && s.trial[i].frame < s.trial[i - 1].frame) return false;
        }
        if (s.trial[c.down].frame >= c.checkFrame) return false;
        s.checkFrame = c.checkFrame;
        return true;
    }

    void nextSide() {
        if (s.dir == 0) { s.dir = 1; s.off = 1; }
        else { ++s.k; s.dir = 0; s.off = -1; }
    }

    void stop(std::string const& why) {
        s.phase = Phase::Idle;
        s.needReset = false;
        s.msg = why;
        CCDirector::get()->getScheduler()->setTimeScale(desiredScale());
    }

    void complete() {
        buildCues();
        save();
        stop("Done - " + std::to_string(s.results.size()) + " clicks counted");
        FMODAudioEngine::sharedEngine()->playEffect(std::string("ding_9.wav"_spr), 1.f, 1.f, 1.f);
    }

    void scheduleNext() {
        while (s.k < static_cast<int>(s.clicks.size())) {
            if (makeTrial(s.k, s.off)) { s.needReset = true; return; }
            nextSide();     // impossible shift (would reorder inputs) counts as a fail
        }
        complete();
    }

    void finish(bool ok) {
        if (s.phase == Phase::Baseline) {
            if (!ok) { stop("Macro does not clear the level - re-record it"); return; }
            s.phase = Phase::Trial;
            s.k = 0; s.dir = 0; s.off = -1;
        } else {
            auto& r = s.results[s.k];
            if (ok) {
                (s.dir == 0 ? r.early : r.late)++;
                s.off += (s.dir == 0 ? -1 : 1);
                if (std::abs(s.off) > kMaxSide) nextSide();
            } else {
                nextSide();
            }
        }
        scheduleNext();
    }
}

// ================= hook-facing API =================

void onLevelEnter(PlayLayer* pl) {
    s.levelId = pl->m_level ? pl->m_level->m_levelID.value() : 0;
    s.phase = Phase::Idle; s.armed = s.recording = false;
    s.macro.clear(); s.results.clear(); s.clicks.clear();
    parse(Mod::get()->getSavedValue<std::string>(key("macro_"), ""), [](int f, int d, int p) {
        s.macro.push_back({f, d != 0, p != 0});
    });
    buildClicks();
    auto saved = Mod::get()->getSavedValue<std::string>(key("res_"), "");
    size_t i = 0;
    parse(saved, [&](int f, int e, int l) {
        if (i < s.results.size() && s.results[i].frame == f) { s.results[i].early = e; s.results[i].late = l; }
        ++i;
    });
    buildCues();
    s.msg = s.macro.empty() ? "No macro yet" : std::to_string(s.clicks.size()) + " clicks loaded";
}

bool filterInput(bool, int, bool) {
    return !(active() && !s.injecting);   // real input is ignored while the counter drives the player
}

void onInput(GJBaseGameLayer* l, bool down, int button, bool p1) {
    if (s.recording && !s.injecting && button == 1) s.macro.push_back({frameOf(l), down, p1});
}

void beforeStep(GJBaseGameLayer* l) {
    int f = frameOf(l);
    if (active()) {
        s.injecting = true;
        while (s.cursor < s.trial.size() && s.trial[s.cursor].frame <= f) {
            auto& in = s.trial[s.cursor++];
            l->handleButton(in.down, 1, in.p1);
        }
        s.injecting = false;
        return;
    }
    if (!s.recording && Cfg::get().counterHud) {
        auto it = s.cues.find(f);
        if (it != s.cues.end()) {
            hud::flash(std::to_string(it->second) + "f");
            if (Cfg::get().counterSfx) {
                auto name = "ding_" + std::to_string(std::min(it->second, 12)) + ".wav";
                FMODAudioEngine::sharedEngine()->playEffect(
                    std::string(Mod::get()->expandSpriteName(name.c_str())), 1.f, 1.f, 1.f);
            }
        }
    }
}

bool onDeath(PlayLayer*) {
    if (!active()) return false;
    s.trialDead = true;
    return true;
}

bool onComplete(PlayLayer*) {
    if (s.recording) {
        s.recording = false;
        buildClicks();
        buildCues();
        save();
        s.msg = "Recorded " + std::to_string(s.clicks.size()) + " clicks";
        return false;       // let the real completion happen
    }
    if (active()) { s.completed = true; return true; }
    return false;
}

void onReset(PlayLayer*) {
    if (s.armed) { s.armed = false; s.recording = true; s.macro.clear(); s.msg = "Recording..."; }
    else if (s.recording) s.macro.clear();          // new attempt = new macro
    s.cursor = 0; s.trialDead = false; s.completed = false;
}

void update(PlayLayer* pl) {
    if (!active()) return;
    if (s.needReset) { s.needReset = false; pl->resetLevel(); return; }
    int f = frameOf(pl);
    if (s.completed) finish(true);
    else if (s.trialDead) finish(false);
    else if (f >= s.checkFrame && s.checkFrame > 0) finish(true);
}

// ================= UI-facing API =================

void armRecording() {
    if (active()) return;
    s.armed = true;
    s.msg = "Restart the level to start recording";
}

void stopRecording() {
    s.armed = s.recording = false;
    s.msg = "Recording stopped";
}

void startCounting() {
    if (s.macro.empty()) { s.msg = "No macro - record a run first"; return; }
    s.armed = s.recording = false;
    buildClicks();
    s.phase = Phase::Baseline;
    makeTrial(-1, 0);
    s.needReset = true;
    s.t0 = std::chrono::steady_clock::now();
    s.msg = "Counting";
    CCDirector::get()->getScheduler()->setTimeScale(kCountSpeed);
}

void cancelCounting() { if (active()) stop("Cancelled"); }

void clearAll() {
    if (active()) stop("Cancelled");
    s.macro.clear(); s.clicks.clear(); s.results.clear(); s.cues.clear();
    s.armed = s.recording = false;
    save();
    s.msg = "Cleared";
}

bool isRecording() { return s.recording || s.armed; }
bool isCounting() { return active(); }

std::string statusText() {
    if (s.recording) return "Recording... " + std::to_string(s.macro.size()) + " inputs";
    if (active()) {
        int n = static_cast<int>(s.clicks.size());
        if (s.phase == Phase::Baseline) return "Checking that the macro clears the level...";
        float el = std::chrono::duration<float>(std::chrono::steady_clock::now() - s.t0).count();
        float eta = s.k > 0 ? el / s.k * (n - s.k) : 0.f;
        char buf[160];
        std::snprintf(buf, sizeof buf, "Counting click %d/%d   ETA %d:%02d", std::min(s.k + 1, n), n,
                      static_cast<int>(eta) / 60, static_cast<int>(eta) % 60);
        return buf;
    }
    return s.msg;
}

std::string summaryText() {
    bool any = false;
    for (auto& r : s.results) if (r.early + r.late > 0) { any = true; break; }
    if (s.results.empty() || !any) return "No counted windows yet";
    float sum = 0.f;
    std::vector<size_t> idx(s.results.size());
    for (size_t i = 0; i < idx.size(); ++i) { idx[i] = i; sum += s.results[i].window(); }
    std::sort(idx.begin(), idx.end(), [](size_t a, size_t b) { return s.results[a].window() < s.results[b].window(); });
    std::ostringstream o;
    char buf[96];
    std::snprintf(buf, sizeof buf, "%d clicks   average window %.1f f\nTightest inputs:\n",
                  static_cast<int>(s.results.size()), sum / s.results.size());
    o << buf;
    for (size_t n = 0; n < std::min<size_t>(5, idx.size()); ++n) {
        auto& r = s.results[idx[n]];
        std::snprintf(buf, sizeof buf, "#%d  frame %d   %df  (-%d / +%d)\n", static_cast<int>(idx[n]) + 1, r.frame,
                      r.window(), r.early, r.late);
        o << buf;
    }
    return o.str();
}

} // namespace kt::counter
