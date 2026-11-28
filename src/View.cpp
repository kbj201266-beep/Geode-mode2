// Window counter overlay (Antic's Counter / NaN Counter look):
//  - while counting: the paths of the trials (white = survived, red = died), "Counting.. 10/26 / ETA ~51s",
//    and a donut ring for every press / release the moment its window is finished - one by one.
//  - while playing (yourself or the bot): the run's path, and the donut rings appear one by one as you reach
//    each press / release. Top right: the window of the last one, in frames and milliseconds (2 decimals).
//  - "Show Results List": the 0-1:, 2:, 3: ... list on the left. It also counts up one by one: while counting
//    it grows with every finished window, while playing with every press / release you reach.
//
// Performance: nothing here is rebuilt every frame. Nodes are created once per level and kept as pointers,
// rings / list / text only change when something new happened, everything drawn in the level is split into
// chunks along x and only the chunks near the player are visible (re-checked only when the player enters
// another chunk).
#include "Common.hpp"
#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdio>
#include <map>

using namespace geode::prelude;

namespace kt::view {

namespace {
    constexpr float kChunk = 480.f;

    struct Bucket {
        char const* label;
        int lo, hi;
        ccColor3B col;
    };
    constexpr Bucket kBuckets[] = {
        {"0-1:", 0, 1, {255, 80, 80}},
        {"2:", 2, 2, {255, 160, 70}},
        {"3:", 3, 3, {255, 225, 90}},
        {"4:", 4, 4, {255, 255, 255}},
        {"5-6:", 5, 6, {120, 235, 120}},
        {"7-8:", 7, 8, {110, 215, 230}},
        {"9-12:", 9, 12, {130, 150, 255}},
        {"13-16:", 13, 16, {185, 120, 255}},
        {"17-24:", 17, 999, {255, 120, 210}},
    };
    constexpr int kBucketCount = sizeof(kBuckets) / sizeof(kBuckets[0]);

    int bucketOf(int w) {
        for (int b = 0; b < kBucketCount; ++b) if (w >= kBuckets[b].lo && w <= kBuckets[b].hi) return b;
        return kBucketCount - 1;
    }
    ccColor3B colorFor(int w) { return kBuckets[bucketOf(w)].col; }
    ccColor4F c4(ccColor3B c, float a = 1.f) { return {c.r / 255.f, c.g / 255.f, c.b / 255.f, a}; }

    // thick ring ("donut")
    void donut(CCDrawNode* n, CCPoint c, float r, float thick, ccColor4F col) {
        constexpr int seg = 20;
        CCPoint prev(c.x + r, c.y);
        for (int i = 1; i <= seg; ++i) {
            float a = 6.2831853f * i / seg;
            CCPoint p(c.x + r * std::cos(a), c.y + r * std::sin(a));
            n->drawSegment(prev, p, thick / 2.f, col);
            prev = p;
        }
    }

    CCLabelBMFont* label(CCNode* parent, std::string const& s, CCPoint pos, float scale, ccColor3B col, CCPoint anchor) {
        auto l = CCLabelBMFont::create(s.c_str(), "bigFont.fnt");
        l->setScale(scale);
        l->setColor(col);
        l->setAnchorPoint(anchor);
        l->setPosition(pos);
        parent->addChild(l);
        return l;
    }

    float tickMs() {
        auto& c = Cfg::get();
        return 1000.f / (c.tpsBypass ? c.tps : 240.f);
    }

    void drop(CCNode*& n) {
        if (n) n->removeFromParentAndCleanup(true);
        n = nullptr;
    }

    // ---- chunked node: one child per 480px of the level, only nearby chunks are visible ----
    struct Chunk {
        CCNode* node;
        CCDrawNode* draw;
    };
    struct Chunked {
        std::map<int, Chunk> chunks;
        bool dirty = true;
        int lastChunk = INT_MIN;
        Chunk& at(CCNode* root, float x) {
            int i = static_cast<int>(std::floor(x / kChunk));
            auto it = chunks.find(i);
            if (it != chunks.end()) return it->second;
            auto node = CCNode::create();
            auto draw = CCDrawNode::create();
            node->addChild(draw);
            root->addChild(node);
            dirty = true;
            return chunks[i] = Chunk{node, draw};
        }
        void clear() {
            chunks.clear();
            dirty = true;
            lastChunk = INT_MIN;
        }
        void cull(PlayLayer* pl) {
            float px = pl->m_player1 ? pl->m_player1->getPositionX() : 0.f;
            int cur = static_cast<int>(std::floor(px / kChunk));
            if (!dirty && cur == lastChunk) return;
            dirty = false;
            lastChunk = cur;
            int span = static_cast<int>(CCDirector::get()->getWinSize().width * 1.5f / kChunk) + 1;
            for (auto& [i, c] : chunks) c.node->setVisible(i >= cur - span && i <= cur + span);
        }
    };

    CCNode* newRoot(CCNode* parent, int z) {
        auto root = CCNode::create();
        parent->addChild(root, z);
        return root;
    }

    PlayLayer* g_layer = nullptr;

    // ---- the run's path ----
    CCNode* g_pathRoot = nullptr;
    Chunked g_pathCh;
    size_t g_pathSize = 0;
    CCPoint g_pathEnd;

    void updatePath(PlayLayer* pl, bool show) {
        auto& path = counter::pathPoints();
        if (!show || counter::baselineRunning() || path.size() < 2) {
            if (g_pathRoot) { drop(g_pathRoot); g_pathCh.clear(); }
            return;
        }
        // the path only changes when a replay is loaded or counted, so this almost never rebuilds
        if (!g_pathRoot || g_pathSize != path.size() || !g_pathEnd.equals(path.back())) {
            drop(g_pathRoot);
            g_pathCh.clear();
            g_pathRoot = newRoot(pl->m_objectLayer, 9996);
            ccColor4F line{1.f, 1.f, 1.f, 0.85f};
            for (size_t i = 1; i < path.size(); ++i) g_pathCh.at(g_pathRoot, path[i - 1].x).draw->drawSegment(path[i - 1], path[i], 1.1f, line);
            g_pathSize = path.size();
            g_pathEnd = path.back();
        }
        g_pathCh.cull(pl);
    }

    // ---- donut rings, added one by one ----
    CCNode* g_ringsRoot = nullptr;
    Chunked g_ringsCh;
    bool g_ringsCounting = false;
    size_t g_ringsShown = 0;
    int g_ringsVersion = -1;
    std::vector<counter::Cue> g_countList;     // counted so far (refreshed only when something got counted)
    int g_countListVersion = -1;

    void addRing(counter::Cue const& c) {
        if (c.pos.x < -1e8f) return;     // no position known (old replay): number only shows top right
        auto col = colorFor(c.window);
        auto& chunk = g_ringsCh.at(g_ringsRoot, c.pos.x);
        float r = c.release ? 6.f : 9.f;
        donut(chunk.draw, c.pos, r, c.release ? 2.6f : 3.6f, c4(col, 0.95f));
        label(chunk.node, std::to_string(c.window), c.pos + ccp(-(r + 4.f), 0.f), c.release ? 0.3f : 0.4f, col, {1.f, 0.5f});
    }

    void updateRings(PlayLayer* pl, bool show) {
        if (!show) {
            if (g_ringsRoot) { drop(g_ringsRoot); g_ringsCh.clear(); }
            return;
        }
        bool counting = counter::isCounting();
        std::vector<counter::Cue> const* list;
        size_t n;
        if (counting) {
            if (g_countListVersion != counter::version()) {
                g_countList = counter::countedCues();
                g_countListVersion = counter::version();
            }
            list = &g_countList;
            n = list->size();
        } else {
            list = &counter::cues();
            n = std::min(counter::cueCursor(), list->size());
        }
        bool reset = !g_ringsRoot || g_ringsCounting != counting || n < g_ringsShown ||
                     (!counting && g_ringsVersion != counter::version());
        if (reset) {
            drop(g_ringsRoot);
            g_ringsCh.clear();
            g_ringsRoot = newRoot(pl->m_objectLayer, 9998);
            g_ringsShown = 0;
            g_ringsCounting = counting;
            g_ringsVersion = counter::version();
        }
        for (size_t i = g_ringsShown; i < n; ++i) addRing((*list)[i]);
        g_ringsShown = n;
        g_ringsCh.cull(pl);
    }

    // ---- top right: the last reached window, 2 decimals ----
    CCLabelBMFont* g_last = nullptr;
    int g_lastShown = -2;
    int g_lastVersion = -1;

    void updateLast(PlayLayer* pl, bool show) {
        int idx = counter::lastCue();
        auto& all = counter::cues();
        if (!show || idx < 0 || idx >= static_cast<int>(all.size())) {
            if (g_last && g_last->isVisible()) g_last->setVisible(false);
            g_lastShown = -2;
            return;
        }
        if (!g_last) {
            auto win = CCDirector::get()->getWinSize();
            g_last = CCLabelBMFont::create("", "bigFont.fnt");
            g_last->setScale(0.45f);
            g_last->setAnchorPoint({1.f, 0.5f});
            g_last->setPosition({win.width - 10.f, win.height - 46.f});
            pl->m_uiLayer->addChild(g_last, 95);
            g_lastShown = -2;
        }
        if (!g_last->isVisible()) g_last->setVisible(true);
        if (idx == g_lastShown && g_lastVersion == counter::version()) return;
        g_lastShown = idx;
        g_lastVersion = counter::version();
        auto& c = all[idx];
        char buf[64];
        std::snprintf(buf, sizeof buf, "%s%df  %.2fms", c.release ? "rel " : "", c.window, c.window * tickMs());
        g_last->setString(buf);
        g_last->setColor(colorFor(c.window));
    }

    // ---- left side bucket list, counting up one by one ----
    CCNode* g_stats = nullptr;
    CCLabelBMFont* g_statNum[kBucketCount] = {};
    CCLabelBMFont* g_statPct = nullptr;
    CCLabelBMFont* g_statAvg = nullptr;
    long long g_statKey = -1;

    void updateStats(PlayLayer* pl, bool show) {
        if (!show) {
            if (g_stats) { drop(g_stats); g_statKey = -1; }
            return;
        }
        bool counting = counter::isCounting();
        // what is shown depends only on this key, so the labels are only touched when it changes
        long long key = counting ? (1LL << 40) + counter::version()
                                 : (static_cast<long long>(counter::version()) << 20) + static_cast<long long>(counter::cueCursor());
        if (g_stats && key == g_statKey) return;
        g_statKey = key;

        if (!g_stats) {
            g_stats = newRoot(pl->m_uiLayer, 90);
            auto win = CCDirector::get()->getWinSize();
            float y = win.height - 14.f;
            for (int b = 0; b < kBucketCount; ++b) {
                label(g_stats, kBuckets[b].label, {6.f, y}, 0.4f, kBuckets[b].col, {0.f, 0.5f});
                g_statNum[b] = label(g_stats, "0", {96.f, y}, 0.4f, kBuckets[b].col, {1.f, 0.5f});
                y -= 16.f;
            }
            g_statPct = label(g_stats, "0.00%", {6.f, 26.f}, 0.4f, {255, 255, 255}, {0.f, 0.5f});
            g_statAvg = label(g_stats, "0.00", {6.f, 10.f}, 0.4f, {255, 255, 255}, {0.f, 0.5f});
        }

        int counts[kBucketCount] = {};
        int total = 0, shown = 0;
        float sum = 0.f;
        auto add = [&](int w) {
            ++shown;
            sum += w;
            ++counts[bucketOf(w)];
        };
        for (auto& r : counter::results()) {
            ++total;
            if (r.relFrame >= 0) ++total;
            if (counting) {                 // while counting: every window finished so far
                if (r.counted) add(r.window());
                if (r.relCounted && r.relFrame >= 0) add(r.relWindow());
            }
        }
        if (!counting) {                    // while playing: only what you have reached in this attempt
            auto& all = counter::cues();
            size_t n = std::min(counter::cueCursor(), all.size());
            for (size_t i = 0; i < n; ++i) add(all[i].window);
        }

        char buf[48];
        for (int b = 0; b < kBucketCount; ++b) g_statNum[b]->setString(std::to_string(counts[b]).c_str());
        std::snprintf(buf, sizeof buf, "%.2f%%", total ? 100.f * shown / total : 0.f);
        g_statPct->setString(buf);
        std::snprintf(buf, sizeof buf, "%.2f", shown ? sum / shown : 0.f);
        g_statAvg->setString(buf);
    }

    // ---- while counting: trial paths + status text ----
    CCDrawNode* g_lines = nullptr;     // finished trials, redrawn only when a trial finishes
    CCDrawNode* g_cur = nullptr;       // running trial, only the new segments are appended
    CCLabelBMFont* g_text = nullptr;
    int g_linesSerial = -1;
    size_t g_curDrawn = 0;
    std::string g_textStr;

    void updateCounting(PlayLayer* pl, bool on) {
        if (!on) {
            if (g_lines || g_cur || g_text) {
                CCNode* a = g_lines; drop(a); g_lines = nullptr;
                CCNode* b = g_cur; drop(b); g_cur = nullptr;
                CCNode* t = g_text; drop(t); g_text = nullptr;
                g_linesSerial = -1;
                g_curDrawn = 0;
                g_textStr.clear();
            }
            return;
        }
        if (!g_lines) {
            g_lines = CCDrawNode::create();
            pl->m_objectLayer->addChild(g_lines, 9997);
            g_cur = CCDrawNode::create();
            pl->m_objectLayer->addChild(g_cur, 9997);
            auto win = CCDirector::get()->getWinSize();
            g_text = CCLabelBMFont::create("", "bigFont.fnt");
            g_text->setScale(0.45f);
            g_text->setAlignment(kCCTextAlignmentCenter);
            g_text->setPosition({win.width / 2.f, 48.f});
            pl->m_uiLayer->addChild(g_text, 95);
            g_linesSerial = -1;
            g_curDrawn = 0;
            g_textStr.clear();
        }
        if (g_linesSerial != counter::trialSerial()) {
            g_linesSerial = counter::trialSerial();
            g_lines->clear();
            for (auto& l : counter::trialLines()) {
                ccColor4F col = l.ok ? ccColor4F{1.f, 1.f, 1.f, 0.75f} : ccColor4F{1.f, 0.35f, 0.45f, 0.75f};
                for (size_t i = 1; i < l.pts.size(); ++i) g_lines->drawSegment(l.pts[i - 1], l.pts[i], 0.8f, col);
            }
        }
        auto& cur = counter::currentTrialPath();
        if (cur.size() < g_curDrawn) {
            g_cur->clear();
            g_curDrawn = 0;
        }
        for (size_t i = std::max<size_t>(1, g_curDrawn); i < cur.size(); ++i)
            g_cur->drawSegment(cur[i - 1], cur[i], 0.8f, {1.f, 1.f, 1.f, 0.95f});
        g_curDrawn = cur.size();

        auto t = counter::countingText();
        if (t != g_textStr) {
            g_textStr = t;
            g_text->setString(t.c_str());
        }
    }
}

void reset() {
    // the nodes belong to the old level and die with it - just forget the pointers
    g_layer = nullptr;
    g_pathRoot = nullptr; g_pathCh.clear(); g_pathSize = 0;
    g_ringsRoot = nullptr; g_ringsCh.clear(); g_ringsShown = 0; g_ringsVersion = -1; g_countListVersion = -1;
    g_last = nullptr; g_lastShown = -2;
    g_stats = nullptr; g_statPct = g_statAvg = nullptr; g_statKey = -1;
    for (auto& l : g_statNum) l = nullptr;
    g_lines = g_cur = nullptr; g_text = nullptr; g_linesSerial = -1; g_curDrawn = 0; g_textStr.clear();
}

// Two switches:
//  - "Counter overlay" (Display tab): path, donut rings, numbers, the top right value, the counting text
//  - "Show Results List" (Window counter tab): the 0-1:, 2:, 3: ... list on the left
void update(PlayLayer* pl) {
    if (pl != g_layer) {
        reset();
        g_layer = pl;
    }
    auto& c = Cfg::get();
    bool counting = counter::isCounting();
    updateCounting(pl, counting && c.counterOverlay);
    updatePath(pl, c.counterOverlay);
    updateRings(pl, c.counterOverlay);
    updateLast(pl, c.counterOverlay && !counting);
    updateStats(pl, c.counterList);
}

} // namespace kt::view
