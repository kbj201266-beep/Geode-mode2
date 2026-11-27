// Window frame counter + replay files.
//
// Modes (like a bot menu):  Disabled / Record / Playback
//  - Record:   restart the level once; from then on every click is stored. Play it in PRACTICE MODE: when you
//              die and respawn at a checkpoint, the replay is cut back to the checkpoint frame and recording
//              continues (nothing is reset). Finish the level to end the recording.
//  - Playback: the replay is played back by the mod. "Ignore Inputs" blocks your own clicks meanwhile.
//  - Count:    1) one normal run of the replay from the level start. It records the player's path, where every
//                 click happened, and saves a checkpoint a few frames before every click.
//              2) for every click: load the checkpoint before it, shift the click earlier / later by 1,2,3...
//                 frames, and check whether the player survives until the NEXT click. No level restarts, so
//                 the attempt counter stays at 1 and it is fast. window = early + late + 1.
// Replays are text files in <save dir>/replays/<name>.ktr:
//   line 1 inputs "frame:down:p1;"   line 2 results "frame:early:late:counted;"
//   line 3 click positions "x,y;"     line 4 path "x,y;"
#include "Common.hpp"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <map>
#include <sstream>

using namespace geode::prelude;

namespace kt::counter {

namespace {
    constexpr int kMaxSide = 12;         // search at most 12 frames each way -> window up to 25f
    constexpr float kCountSpeed = 4.f;   // replay speed while counting
    constexpr float kNoPos = -1e9f;
    constexpr size_t kMaxLines = 40;     // trial lines kept on screen for the current click

    enum class Phase { Idle, Baseline, Trial };

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
        std::map<int, int> cues;             // click frame -> window, for play-time display
        std::vector<int> downToClick;        // macro index -> click index (-1 = not a click)
        std::vector<CCPoint> clickPos;       // where each click happened (from the counting run)
        std::vector<CCPoint> path;           // player path from the counting run
        int lastPathFrame = -1;
        int version = 0;                     // bumps whenever the overlay must redraw

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

        std::vector<Input> trial;            // what is injected (macro for playback, shifted macro for counting)
        size_t cursor = 0;
        int checkFrame = 0;
        int k = 0, dir = 0, off = 0;
        std::chrono::steady_clock::time_point t0;
        std::string msg = "Idle";
    } s;

    bool active() { return s.phase != Phase::Idle; }
    bool playing() { return s.mode == Mode::Playback && !s.macro.empty() && !active(); }
    int frameOf(GJBaseGameLayer* l) { return l->m_gameState.m_currentProgress; }

    void syncTrial() { s.trial = s.macro; }
    void syncCursor(int frame) {
        s.cursor = std::lower_bound(s.trial.begin(), s.trial.end(), frame,
                                    [](Input const& i, int f) { return i.frame < f; }) - s.trial.begin();
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

    void buildCues() {
        s.cues.clear();
        for (auto& r : s.results) if (r.counted) s.cues[r.frame] = r.window();
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
        s.downToClick.assign(s.macro.size(), -1);
        for (size_t i = 0; i < s.clicks.size(); ++i) s.downToClick[s.clicks[i].down] = static_cast<int>(i);
        s.clickPos.assign(s.clicks.size(), CCPoint{kNoPos, kNoPos});
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
        while (lines.size() < 4) lines.emplace_back();

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
        else {
            if (s.k < static_cast<int>(s.results.size())) s.results[s.k].counted = true;
            ++s.k; s.dir = 0; s.off = -1;
            s.lines.clear();
            ++s.version;      // a click got its final window: redraw rings / stats
        }
    }

    float desiredScale() {
        auto& c = Cfg::get();
        return c.speedhack ? c.speed : 1.f;
    }

    void stop(std::string const& why) {
        s.phase = Phase::Idle;
        s.needReset = false;
        s.msg = why;
        s.lines.clear();
        s.trialPath.clear();
        releaseCps();
        syncTrial();
        ++s.version;
        CCDirector::get()->getScheduler()->setTimeScale(desiredScale());
    }

    void complete() {
        buildCues();
        if (Cfg::get().saveOnFinish) autosave();
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
            if (!ok) { stop("Replay does not clear the level - record it again"); return; }
            s.phase = Phase::Trial;
            s.k = 0; s.dir = 0; s.off = -1;
        } else {
            if (!s.trialPath.empty()) {
                s.lines.push_back({s.trialPath, ok});
                if (s.lines.size() > kMaxLines) s.lines.erase(s.lines.begin());
            }
            auto& r = s.results[s.k];
            if (ok) {
                (s.dir == 0 ? r.early : r.late)++;
                s.off += (s.dir == 0 ? -1 : 1);
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
    s.armed = s.recording = false;
    if (s.mode == Mode::Record) s.mode = Mode::Disabled;
    releaseCps();
    s.lines.clear();
    s.trialPath.clear();
    refreshList();
    s.cursor = 0;
    ++s.version;
}

bool filterInput(bool, int, bool) {
    if (active()) return s.injecting;
    if (playing() && Cfg::get().ignoreInputs) return s.injecting;
    return true;
}

void onInput(GJBaseGameLayer* l, bool down, int button, bool p1) {
    if (s.recording && !s.injecting && button == 1) s.macro.push_back({frameOf(l), down, p1});
}

void beforeStep(GJBaseGameLayer* l) {
    if (active() && s.needReset) return;      // the old run is still going until the reset happens
    int f = frameOf(l);
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
            if (baseline && in.down && index < s.downToClick.size() && s.downToClick[index] >= 0) {
                auto p = in.p1 ? l->m_player1 : l->m_player2;
                if (p) s.clickPos[s.downToClick[index]] = p->getPosition();
            }
            l->handleButton(in.down, 1, in.p1);
        }
        s.injecting = false;
    }
    if (!active() && !s.recording && Cfg::get().counterHud) {
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
    s.trialDead = false;
    s.completed = false;
    s.lastPathFrame = -1;
    if (active()) s.trialPath.clear();
}

void update(PlayLayer* pl) {
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
        s.armed = true;
        s.msg = "Restart the level to start recording (practice mode + checkpoints are fine)";
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
    releaseCps();
    s.cps.assign(s.clicks.size(), nullptr);
    s.cpFrame.resize(s.clicks.size());
    for (size_t i = 0; i < s.clicks.size(); ++i) {
        s.cpFrame[i] = std::max(0, s.macro[s.clicks[i].down].frame - kMaxSide - 2);
    }
    s.path.clear();
    s.lines.clear();
    s.trialPath.clear();
    s.lastPathFrame = -1;
    s.phase = Phase::Baseline;
    makeTrial(-1, 0);
    s.needReset = true;
    s.t0 = std::chrono::steady_clock::now();
    s.msg = "Counting";
    ++s.version;
    CCDirector::get()->getScheduler()->setTimeScale(kCountSpeed);
}

void cancelCounting() { if (active()) stop("Stopped"); }

bool isRecording() { return s.recording || s.armed; }
bool isCounting() { return active(); }

std::string countingText() {
    if (!active()) return "";
    int n = static_cast<int>(s.clicks.size());
    if (s.phase == Phase::Baseline) return "Preparing.. 0/" + std::to_string(n) + "\nETA ~?";
    float el = std::chrono::duration<float>(std::chrono::steady_clock::now() - s.t0).count();
    std::string e = s.k > 0 ? eta(el / s.k * (n - s.k)) : std::string("~?");
    return "Counting.. " + std::to_string(s.k) + "/" + std::to_string(n) + "\nETA " + e;
}

std::string statusText() {
    if (s.armed) return "Waiting - restart the level to start recording";
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
    return state + "  -  " + std::to_string(s.macro.size()) + " inputs at 240 TPS";
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
    std::snprintf(buf, sizeof buf, "%d clicks   average window %.1f f\nTightest inputs:\n",
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
        o << "#" << (i + 1) << "  frame " << r.frame << "  " << r.window() << "f  (-" << r.early << " / +" << r.late << ")\n";
    }
    return o.str();
}

std::vector<Result> const& results() { return s.results; }

std::vector<ClickView> clickViews() {
    std::vector<ClickView> out;
    for (size_t i = 0; i < s.results.size() && i < s.clickPos.size(); ++i) {
        if (!s.results[i].counted || s.clickPos[i].x <= kNoPos / 2) continue;
        out.push_back({s.clickPos[i], s.results[i].window()});
    }
    return out;
}

std::vector<CCPoint> const& pathPoints() { return s.path; }
std::vector<TrialLine> const& trialLines() { return s.lines; }
std::vector<CCPoint> const& currentTrialPath() { return s.trialPath; }
int version() { return s.version; }

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
    s.clickPos.clear(); s.path.clear(); s.downToClick.clear();
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
